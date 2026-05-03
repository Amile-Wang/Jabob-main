// audioDownlink.js — 接收服务端 binary opus 帧，解码并通过 Web Audio 播放
// 流式：每帧到达即入队；累计 0.2s 后开始播放，避免短促爆音。
// tts.start / tts.stop 由 main.js 路由到 deviceState.enterSpeaking/Listening，
// 所以本模块只管解码 + 播放，不管状态机。

import { log } from './ui.js';

const SAMPLE_RATE = 16000; // 服务端 hello 回会带 audio_params.sample_rate；
                            // 浏览器端 AudioContext 一旦 new 出来就锁了，统一 16k
const CHANNELS = 1;
const FRAME_SIZE = 960;
const MIN_PLAY_SAMPLES = SAMPLE_RATE * 0.2; // 200ms 起播
const FADE_MS = 0.02;

let audioContext = null;
let opusDecoder = null;
let pendingFrames = []; // 已收但未解码的 Uint8Array opus 帧
let pcmQueue = [];      // 已解码 float32 sample
let isPlaying = false;
let endOfStream = false;

function ensureModuleInstance() {
  if (typeof window.ModuleInstance === 'undefined') {
    if (typeof window.Module !== 'undefined') {
      window.ModuleInstance = window.Module.instance || window.Module;
    } else {
      throw new Error('libopus.js 未加载');
    }
  }
  return window.ModuleInstance;
}

function ensureAudioContext() {
  if (audioContext) return audioContext;
  audioContext = new (window.AudioContext || window.webkitAudioContext)({
    sampleRate: SAMPLE_RATE,
    latencyHint: 'interactive',
  });
  return audioContext;
}

// 让外部（main.js 在 Connect 按钮的 user gesture 内）触发一次 resume，
// 否则 autoplay 政策下 AudioContext 一直 suspended，server 发再多 opus 帧也没声音。
export async function primeForPlayback() {
  const ctx = ensureAudioContext();
  if (ctx.state === 'suspended') {
    try {
      await ctx.resume();
      log(`[downlink] AudioContext resumed (state=${ctx.state})`, 'success');
    } catch (e) {
      log(`[downlink] AudioContext resume failed: ${e.message}`, 'error');
    }
  }
  // 顺便把解码器初始化掉，避免第一帧到达时再 lazy init 增加首字节延迟
  try {
    ensureDecoder();
  } catch (e) {
    log(`[downlink] decoder pre-init: ${e.message}`, 'warning');
  }
}

function ensureDecoder() {
  if (opusDecoder) return opusDecoder;
  const mod = ensureModuleInstance();
  const channels = CHANNELS;
  const decoderSize = mod._opus_decoder_get_size(channels);
  const decoderPtr = mod._malloc(decoderSize);
  if (!decoderPtr) throw new Error('opus_decoder: malloc failed');
  const err = mod._opus_decoder_init(decoderPtr, SAMPLE_RATE, channels);
  if (err < 0) {
    mod._free(decoderPtr);
    throw new Error(`opus_decoder_init failed: ${err}`);
  }
  opusDecoder = {
    mod,
    decoderPtr,
    decode(opusBytes) {
      const m = this.mod;
      const inPtr = m._malloc(opusBytes.length);
      m.HEAPU8.set(opusBytes, inPtr);
      const outPtr = m._malloc(FRAME_SIZE * 2);
      const samples = m._opus_decode(this.decoderPtr, inPtr, opusBytes.length, outPtr, FRAME_SIZE, 0);
      let int16 = null;
      if (samples > 0) {
        int16 = new Int16Array(samples);
        int16.set(m.HEAP16.subarray(outPtr >> 1, (outPtr >> 1) + samples));
      }
      m._free(inPtr);
      m._free(outPtr);
      return int16;
    },
  };
  log('[downlink] opus decoder ready', 'success');
  return opusDecoder;
}

function int16ToFloat32(int16) {
  const out = new Float32Array(int16.length);
  for (let i = 0; i < int16.length; i++) {
    out[i] = int16[i] / (int16[i] < 0 ? 0x8000 : 0x7fff);
  }
  return out;
}

// 服务端发来一个二进制 opus 帧（v1 raw 格式 —— 我们 hello 没声明 version=2/3）。
export function enqueueBinaryFrame(arrayBuffer) {
  pendingFrames.push(new Uint8Array(arrayBuffer));
  endOfStream = false;
  // 第一帧到达：起 decode loop
  if (!isPlaying && pcmQueue.length === 0) {
    decodeAndMaybePlay().catch((e) => log(`[downlink] decode loop error: ${e.message}`, 'error'));
  }
}

// 服务端发 tts.stop 或 0 字节空帧时调一次：标记本段结束，
// 把任何还没攒够 MIN_PLAY_SAMPLES 阈值的残余样本立刻播出去（避免短句"你好"被吞）。
export function signalEndOfStream() {
  endOfStream = true;
  // 把还没解码的帧解掉
  if (pendingFrames.length > 0) {
    decodeAndMaybePlay().catch((e) => log(`[downlink] eos drain decode: ${e.message}`, 'warning'));
    return;
  }
  // 没在播但有 PCM —— 立刻播残余（绕过 MIN_PLAY_SAMPLES 阈值）
  if (!isPlaying && pcmQueue.length > 0) {
    log(`[downlink] eos: drain ${pcmQueue.length} residual samples`, 'debug');
    playNextChunk();
  }
}

async function decodeAndMaybePlay() {
  ensureAudioContext();
  if (audioContext.state === 'suspended') {
    try { await audioContext.resume(); } catch (_) {}
  }
  let dec;
  try {
    dec = ensureDecoder();
  } catch (e) {
    log(`[downlink] decoder init failed: ${e.message}`, 'error');
    return;
  }
  while (pendingFrames.length > 0) {
    const frame = pendingFrames.shift();
    if (!frame || frame.length === 0) continue;
    let int16;
    try {
      int16 = dec.decode(frame);
    } catch (e) {
      log(`[downlink] opus_decode error: ${e.message}`, 'error');
      continue;
    }
    if (!int16) continue;
    const f32 = int16ToFloat32(int16);
    for (let i = 0; i < f32.length; i++) pcmQueue.push(f32[i]);
  }
  if (!isPlaying && pcmQueue.length >= MIN_PLAY_SAMPLES) {
    playNextChunk();
  } else if (!isPlaying && endOfStream && pcmQueue.length > 0) {
    // 末尾不足阈值也要播完
    playNextChunk();
  }
}

function playNextChunk() {
  if (isPlaying) return;
  if (pcmQueue.length === 0) return;
  isPlaying = true;
  const ctx = ensureAudioContext();
  // 一次最多取 1s
  const take = Math.min(pcmQueue.length, SAMPLE_RATE);
  const chunk = pcmQueue.splice(0, take);
  const buf = ctx.createBuffer(CHANNELS, chunk.length, SAMPLE_RATE);
  buf.copyToChannel(new Float32Array(chunk), 0);
  const source = ctx.createBufferSource();
  source.buffer = buf;
  const gain = ctx.createGain();
  gain.gain.setValueAtTime(0, ctx.currentTime);
  gain.gain.linearRampToValueAtTime(1, ctx.currentTime + FADE_MS);
  if (buf.duration > FADE_MS * 2) {
    gain.gain.setValueAtTime(1, ctx.currentTime + buf.duration - FADE_MS);
    gain.gain.linearRampToValueAtTime(0, ctx.currentTime + buf.duration);
  }
  source.connect(gain);
  gain.connect(ctx.destination);
  source.onended = () => {
    isPlaying = false;
    // 还有数据继续播；否则等更多
    if (pcmQueue.length > 0) {
      playNextChunk();
    } else if (pendingFrames.length > 0) {
      decodeAndMaybePlay().catch(() => {});
    }
  };
  source.start();
}

// 真清队列 —— 用于 abort（用户打断）或 tts.start 前清掉上一轮残余。
// 注意：tts.stop 不要调这个，tts.stop 走 signalEndOfStream（让残余播完）。
export function flush() {
  pendingFrames = [];
  pcmQueue = [];
  endOfStream = false;
}
