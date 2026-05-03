// main.js — 入口：DOM 事件绑定 + ws 路由 + 配置持久化

import { deviceState } from './deviceState.js';
import { connect, disconnect, setListeners, isOpen } from './ws.js';
import { sendHello, sendDetect } from './protocol.js';
import { handleIncomingMcp } from './mcpSimulator.js';
import { beginRecord, stopRecord, isRecording } from './audioUplink.js';
import {
  enqueueBinaryFrame,
  signalEndOfStream,
  flush as flushDownlink,
  primeForPlayback,
} from './audioDownlink.js';
import { onStateChange, log, appendConvo, setWsStatus, refreshToolInspector } from './ui.js';

// ---------- 配置持久化 ----------
const LS = {
  wsUrl: 'simclient_wsUrl',
  deviceId: 'simclient_deviceId',
  clientId: 'simclient_clientId',
  token: 'simclient_token',
};

const DEFAULTS = {
  wsUrl: 'ws://51.107.185.69/ws/',
  deviceId: '80:b5:4e:e0:98:84',
  clientId: 'browser-test-rig-' + Math.random().toString(36).slice(2, 10),
  token: 'test-token',
};

function loadConfig() {
  return {
    wsUrl: localStorage.getItem(LS.wsUrl) || DEFAULTS.wsUrl,
    deviceId: localStorage.getItem(LS.deviceId) || DEFAULTS.deviceId,
    clientId: localStorage.getItem(LS.clientId) || DEFAULTS.clientId,
    token: localStorage.getItem(LS.token) || DEFAULTS.token,
  };
}

function saveConfig(cfg) {
  localStorage.setItem(LS.wsUrl, cfg.wsUrl);
  localStorage.setItem(LS.deviceId, cfg.deviceId);
  localStorage.setItem(LS.clientId, cfg.clientId);
  localStorage.setItem(LS.token, cfg.token);
}

function readFormConfig() {
  return {
    wsUrl: document.getElementById('wsUrl').value.trim(),
    deviceId: document.getElementById('deviceId').value.trim(),
    clientId: document.getElementById('clientId').value.trim(),
    token: document.getElementById('token').value.trim(),
  };
}

function fillForm(cfg) {
  document.getElementById('wsUrl').value = cfg.wsUrl;
  document.getElementById('deviceId').value = cfg.deviceId;
  document.getElementById('clientId').value = cfg.clientId;
  document.getElementById('token').value = cfg.token;
}

// ---------- WS 入站路由 ----------
function routeText(_raw, msg) {
  if (!msg || typeof msg !== 'object') return;
  const t = msg.type;
  if (t === 'hello') {
    if (msg.session_id) {
      deviceState.setSessionId(msg.session_id);
    }
    log(`hello reply session_id=${msg.session_id} audio_params=${JSON.stringify(msg.audio_params || {})}`, 'success');
    return;
  }
  if (t === 'tts') {
    // tts.start → speaking；sentence_start → 显示文本；stop → listening
    if (msg.state === 'start') {
      deviceState.enterSpeaking();
      // 清掉上一轮可能没播完的残余，避免相互踩踏
      flushDownlink();
      log('[tts] start', 'info');
    } else if (msg.state === 'sentence_start') {
      if (msg.text) appendConvo('server', msg.text);
      log(`[tts] sentence_start: ${msg.text || ''}`, 'debug');
    } else if (msg.state === 'sentence_end') {
      log(`[tts] sentence_end: ${msg.text || ''}`, 'debug');
    } else if (msg.state === 'stop') {
      log('[tts] stop', 'info');
      // 标记 EOS：让 pcmQueue 里残余的 <200ms 样本播完，而不是清掉
      signalEndOfStream();
      // 镜像 application.cc:910：tts.stop 后回 listening
      // 如果当前模式是 manual 且我们之前没真录，stay idle 更安全
      if (deviceState.state === 'speaking') {
        if (deviceState.listeningMode === 'manual' && !isRecording()) {
          deviceState.setState('idle');
        } else {
          deviceState.enterListening();
        }
      }
    }
    return;
  }
  if (t === 'stt') {
    if (msg.text) appendConvo('user', `[stt] ${msg.text}`);
    log(`[stt] ${msg.text}`, 'info');
    return;
  }
  if (t === 'llm') {
    log(`[llm] emotion=${msg.emotion} text=${msg.text}`, 'debug');
    // 不入对话流（emoji 会重复）
    return;
  }
  if (t === 'mcp') {
    if (msg.payload) {
      handleIncomingMcp(msg.payload);
    }
    return;
  }
  if (t === 'system') {
    log(`[system] ${JSON.stringify(msg)}`, 'warning');
    return;
  }
  if (t === 'iot') {
    log(`[iot] ${JSON.stringify(msg).slice(0, 200)}`, 'debug');
    return;
  }
  if (t === 'goodbye') {
    log('[goodbye] server requested close', 'warning');
    return;
  }
  log(`[ws<=] unhandled type=${t}: ${JSON.stringify(msg).slice(0, 200)}`, 'warning');
}

function routeBinary(buf) {
  // 服务端有时会送 0 字节作为结束标志
  if (buf.byteLength === 0) {
    signalEndOfStream();
    return;
  }
  enqueueBinaryFrame(buf);
}

// ---------- 事件绑定 ----------
function bindEvents() {
  document.getElementById('connectBtn').addEventListener('click', async () => {
    const cfg = readFormConfig();
    saveConfig(cfg);
    setWsStatus('connecting…', '#e0a000');
    deviceState.setState('connecting');
    // 利用本次 click 的 user gesture 把 AudioContext 唤醒，
    // 否则 autoplay 政策下 server 发的 opus 帧解码后没声音。
    await primeForPlayback();
    connect(cfg.wsUrl, cfg);
  });

  document.getElementById('disconnectBtn').addEventListener('click', () => {
    disconnect();
  });

  document.getElementById('modeSelect').addEventListener('change', (e) => {
    const newMode = e.target.value;
    // 选中即切（用户决策 3）—— 直接调 deviceState.setListeningMode
    deviceState.setListeningMode(newMode);
    log(`[ui] mode → ${newMode}`, 'info');
  });

  document.getElementById('recordBtn').addEventListener('click', () => {
    beginRecord();
  });

  document.getElementById('stopBtn').addEventListener('click', () => {
    stopRecord();
  });

  document.getElementById('abortBtn').addEventListener('click', () => {
    const reason = document.getElementById('abortReason').value || undefined;
    deviceState.sendAbort(reason);
    flushDownlink();
    log(`[ui] abort sent reason=${reason || '(none)'}`, 'info');
  });

  document.getElementById('detectBtn').addEventListener('click', () => {
    const text = document.getElementById('detectText').value.trim();
    if (!text) return;
    sendDetect(text);
    appendConvo('user', `[detect] ${text}`);
    document.getElementById('detectText').value = '';
  });
  document.getElementById('detectText').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') document.getElementById('detectBtn').click();
  });

  document.getElementById('enterMeetingBtn').addEventListener('click', () => {
    deviceState.setListeningMode('meeting');
    log('[ui] device-initiated: enter meeting', 'info');
  });
  document.getElementById('exitMeetingBtn').addEventListener('click', () => {
    deviceState.setListeningMode('auto');
    log('[ui] device-initiated: exit meeting', 'info');
  });
  document.getElementById('silenceBtn').addEventListener('click', () => {
    deviceState.enterIdle();
    log('[ui] device-initiated: silence_now', 'info');
  });

  document.getElementById('clearLogBtn').addEventListener('click', () => {
    document.getElementById('logPanel').innerHTML = '';
  });
}

// ---------- WS lifecycle ----------
setListeners({
  onOpen: () => {
    setWsStatus('connected', '#1b8a3a');
    sendHello();
    // hello reply 到达后 routeText 里 setSessionId；
    // 此时还没 record，不要进 listening。保持 connecting → 用户点 Record 才进 listening。
    // 但要把 state 推回 idle，否则 connecting pill 一直亮
    deviceState.setState('idle');
  },
  onClose: () => {
    setWsStatus('closed', '#c33');
    deviceState.setState('idle');
    deviceState.setSessionId('');
  },
  onText: routeText,
  onBinary: routeBinary,
});

// ---------- 启动 ----------
function showSecureContextBannerIfNeeded() {
  const hasMediaDevices = !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia);
  if (window.isSecureContext && hasMediaDevices) return;
  const banner = document.getElementById('secureCtxBanner');
  if (!banner) return;
  banner.style.display = '';
  document.getElementById('banner-origin').textContent = location.origin;
  document.getElementById('banner-origin-flag').textContent = location.origin;
}

function init() {
  fillForm(loadConfig());
  bindEvents();
  refreshToolInspector();
  onStateChange();
  showSecureContextBannerIfNeeded();
  log(`sim_client ready (secureContext=${window.isSecureContext}, mediaDevices=${!!navigator.mediaDevices})`, 'success');
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', init);
} else {
  init();
}
