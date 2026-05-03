// audioUplink.js — getUserMedia → PCM → Opus(16kHz mono 60ms) → WS binary
// 关键修复（plan §2 / 雷 7）：每帧 send 前先 deviceState.shouldSendUplink() 判断，
// Speaking 态停止上传（除 realtime 模式）。

import { deviceState } from './deviceState.js';
import { sendBinary, isOpen } from './ws.js';
import { sendStartListening, sendStopListening } from './protocol.js';
import { log } from './ui.js';

const SAMPLE_RATE = 16000;
const CHANNELS = 1;
const FRAME_SIZE = 960; // 60ms @ 16kHz

let audioContext = null;
let mediaStream = null;
let audioSource = null;
let workletNode = null;
let pcmBacklog = new Int16Array(0);
let recording = false;
let opusEncoder = null;
let framesSent = 0;
let framesGated = 0;

// AudioWorklet 内联代码：把麦克风 float32 → int16 → 60ms 帧 postMessage 回主线程
const PROCESSOR_CODE = `
class PcmFramerProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.frameSize = 960;
    this.buffer = new Int16Array(this.frameSize);
    this.bufferIndex = 0;
    this.active = false;
    this.port.onmessage = (e) => {
      if (e.data && e.data.cmd === 'start') this.active = true;
      else if (e.data && e.data.cmd === 'stop') {
        this.active = false;
        if (this.bufferIndex > 0) {
          this.port.postMessage({ type: 'frame', buffer: this.buffer.slice(0, this.bufferIndex) });
          this.bufferIndex = 0;
        }
      }
    };
  }
  process(inputs) {
    if (!this.active) return true;
    const input = inputs[0] && inputs[0][0];
    if (!input) return true;
    for (let i = 0; i < input.length; i++) {
      if (this.bufferIndex >= this.frameSize) {
        this.port.postMessage({ type: 'frame', buffer: this.buffer.slice(0) });
        this.bufferIndex = 0;
      }
      let s = input[i] * 32767;
      if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
      this.buffer[this.bufferIndex++] = s | 0;
    }
    return true;
  }
}
registerProcessor('pcm-framer-processor', PcmFramerProcessor);
`;

function ensureModuleInstance() {
  if (typeof window.ModuleInstance === 'undefined') {
    if (typeof window.Module !== 'undefined') {
      window.ModuleInstance = window.Module.instance || window.Module;
    } else {
      throw new Error('libopus.js 未加载（window.Module/ModuleInstance 缺失）');
    }
  }
  return window.ModuleInstance;
}

function ensureEncoder() {
  if (opusEncoder) return opusEncoder;
  const mod = ensureModuleInstance();
  const channels = CHANNELS;
  const sampleRate = SAMPLE_RATE;
  const application = 2048; // OPUS_APPLICATION_VOIP
  const encoderSize = mod._opus_encoder_get_size(channels);
  const encoderPtr = mod._malloc(encoderSize);
  if (!encoderPtr) throw new Error('opus_encoder: malloc failed');
  const err = mod._opus_encoder_init(encoderPtr, sampleRate, channels, application);
  if (err < 0) {
    mod._free(encoderPtr);
    throw new Error(`opus_encoder_init failed: ${err}`);
  }
  // 与 test_page.html 保持一致的 CTL：bitrate / complexity / DTX
  mod._opus_encoder_ctl(encoderPtr, 4002, 16000); // OPUS_SET_BITRATE
  mod._opus_encoder_ctl(encoderPtr, 4010, 5);     // OPUS_SET_COMPLEXITY
  mod._opus_encoder_ctl(encoderPtr, 4016, 1);     // OPUS_SET_DTX
  opusEncoder = {
    mod,
    encoderPtr,
    maxPacket: 4000,
    encode(int16Frame) {
      const m = this.mod;
      const pcmPtr = m._malloc(int16Frame.length * 2);
      m.HEAP16.set(int16Frame, pcmPtr >> 1);
      const outPtr = m._malloc(this.maxPacket);
      const len = m._opus_encode(this.encoderPtr, pcmPtr, FRAME_SIZE, outPtr, this.maxPacket);
      let result = null;
      if (len > 0) {
        result = new Uint8Array(len);
        result.set(m.HEAPU8.subarray(outPtr, outPtr + len));
      }
      m._free(pcmPtr);
      m._free(outPtr);
      return result;
    },
  };
  log('[uplink] opus encoder ready (16kHz/60ms/voip/bitrate=16k/dtx)', 'success');
  return opusEncoder;
}

async function ensureAudioGraph() {
  if (audioContext) return;

  // 浏览器策略：在非 secure context（http:// 非 localhost）下 mediaDevices 直接 undefined
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
    const isSecure = window.isSecureContext;
    const origin = location.origin;
    const msg = `mediaDevices 不可用（isSecureContext=${isSecure}, origin=${origin}）。\n` +
      `浏览器仅在 https / localhost / 127.0.0.1 / file:// 下允许麦克风。三个绕开方式：\n` +
      `  1) SSH tunnel：ssh -L 8888:51.107.185.69:80 <vm> → 改用 http://localhost:8888/test/sim_client/\n` +
      `  2) 改 nginx 加 https 自签证书\n` +
      `  3) Chrome 启动加 flag：--unsafely-treat-insecure-origin-as-secure="${origin}" --user-data-dir=/tmp/chrome-insecure\n` +
      `验证 TTS 链路无需麦克风：用「注入 wake / detect 文字」面板发一句话即可走完整 LLM+TTS 路径。`;
    throw new Error(msg);
  }

  audioContext = new (window.AudioContext || window.webkitAudioContext)({
    sampleRate: SAMPLE_RATE,
    latencyHint: 'interactive',
  });

  try {
    mediaStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: true,
        noiseSuppression: true,
        sampleRate: SAMPLE_RATE,
        channelCount: CHANNELS,
      },
    });
  } catch (e) {
    // 拆开 audioContext 让下次 retry 干净
    try { audioContext.close(); } catch (_) {}
    audioContext = null;
    if (e.name === 'NotAllowedError') {
      throw new Error('麦克风权限被拒绝。点击地址栏左侧的锁/感叹号图标 → 重置权限 → 再试。');
    }
    if (e.name === 'NotFoundError') {
      throw new Error('未找到麦克风设备。检查系统音频输入。');
    }
    throw new Error(`getUserMedia 失败: ${e.name}: ${e.message}`);
  }
  const blob = new Blob([PROCESSOR_CODE], { type: 'application/javascript' });
  const url = URL.createObjectURL(blob);
  await audioContext.audioWorklet.addModule(url);
  URL.revokeObjectURL(url);
  workletNode = new AudioWorkletNode(audioContext, 'pcm-framer-processor');
  workletNode.port.onmessage = (e) => {
    if (e.data && e.data.type === 'frame') {
      handlePcmFrame(e.data.buffer);
    }
  };
  audioSource = audioContext.createMediaStreamSource(mediaStream);
  audioSource.connect(workletNode);
  // worklet 不需要 connect destination，但 Chrome 老版本对 silent 处理不一致；
  // 用 zero-gain 节点保证 process() 被调度
  const zero = audioContext.createGain();
  zero.gain.value = 0;
  workletNode.connect(zero);
  zero.connect(audioContext.destination);
  log(`[uplink] audio graph ready @${audioContext.sampleRate}Hz`, 'info');
}

function handlePcmFrame(int16Buf) {
  if (!recording) return;
  // pcmBacklog 拼接（worklet 已按 960 切分，但留 backlog 作为防御）
  if (pcmBacklog.length === 0 && int16Buf.length === FRAME_SIZE) {
    encodeAndSend(int16Buf);
    return;
  }
  const merged = new Int16Array(pcmBacklog.length + int16Buf.length);
  merged.set(pcmBacklog);
  merged.set(int16Buf, pcmBacklog.length);
  let cursor = 0;
  while (merged.length - cursor >= FRAME_SIZE) {
    encodeAndSend(merged.slice(cursor, cursor + FRAME_SIZE));
    cursor += FRAME_SIZE;
  }
  pcmBacklog = merged.slice(cursor);
}

function encodeAndSend(int16Frame) {
  // 关键 gating：speaking 时停手（除 realtime）
  if (!deviceState.shouldSendUplink()) {
    framesGated++;
    return;
  }
  if (!isOpen()) return;
  let opusBytes;
  try {
    opusBytes = ensureEncoder().encode(int16Frame);
  } catch (e) {
    log(`[uplink] opus encode error: ${e.message}`, 'error');
    return;
  }
  if (!opusBytes || opusBytes.length === 0) return;
  sendBinary(opusBytes.buffer);
  framesSent++;
  if (framesSent % 50 === 0) {
    log(`[uplink] sent=${framesSent} gated=${framesGated} mode=${deviceState.listeningMode}`, 'debug');
  }
}

// 用户点 Record。按当前 listeningMode 开 listening + 启动 worklet。
export async function beginRecord() {
  if (recording) return;
  try {
    await ensureAudioGraph();
    ensureEncoder();
  } catch (e) {
    log(`[uplink] init failed: ${e.message}`, 'error');
    return;
  }
  if (audioContext.state === 'suspended') {
    try { await audioContext.resume(); } catch (_) {}
  }
  recording = true;
  framesSent = 0;
  framesGated = 0;
  pcmBacklog = new Int16Array(0);
  workletNode.port.postMessage({ cmd: 'start' });
  // setState('listening') 内部会 sendStartListening(currentMode)
  deviceState.setState('listening');
  log(`[uplink] begin record mode=${deviceState.listeningMode}`, 'success');
}

// 用户点 Stop。manual 模式发 listen state=stop；其他模式只本地停采集，
// listening 状态保持（与固件 SetListeningMode 行为一致 —— 手动 stop 才退 Listening）。
export function stopRecord() {
  if (!recording) return;
  recording = false;
  if (workletNode) {
    workletNode.port.postMessage({ cmd: 'stop' });
  }
  if (deviceState.listeningMode === 'manual') {
    sendStopListening();
    deviceState.setState('idle');
  }
  log(`[uplink] stop record (sent=${framesSent} gated=${framesGated})`, 'info');
}

export function isRecording() {
  return recording;
}
