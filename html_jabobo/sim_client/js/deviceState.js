// deviceState.js — 浏览器端 stand-in 的设备状态机
// 镜像 application.cc 的 device_state_ + listening_mode_ 二元状态。
// 见 plan: jabobo-main-html-jabobo-snuggly-shell.md §2

import { sendStartListening, sendAbort } from './protocol.js';
import { onStateChange } from './ui.js';

const VALID_STATES = ['idle', 'connecting', 'listening', 'speaking'];
const VALID_MODES = ['auto', 'manual', 'realtime', 'meeting'];

export const deviceState = {
  state: 'idle',
  listeningMode: 'auto',
  sessionId: '',
  visionUrl: '',
  mcpToolState: {
    volume: 50,
    brightness: 50,
    theme: 'light',
    battery: { level: 87, charging: false, discharging: true },
    network: { type: 'wifi', ssid: 'browser-rig', signal: -45 },
  },

  setSessionId(id) {
    this.sessionId = id || '';
    onStateChange();
  },

  // setState 镜像 application.cc:859 SetDeviceState 的副作用：
  // - 进入 listening：发 SendStartListening(currentMode)（除非已经 listening 同 mode）
  // - 进入 speaking：仅本地状态变化，不发 ws 帧
  // - 进入 idle：仅本地状态
  // 注意 same-state 守卫：如果 newState === this.state 直接 return，
  // 这就是固件 bug 3.5 的源头。setListeningMode 在跨模式时会绕过这条。
  setState(newState) {
    if (!VALID_STATES.includes(newState)) {
      console.warn('[deviceState] invalid state:', newState);
      return;
    }
    if (this.state === newState) {
      return;
    }
    const previous = this.state;
    this.state = newState;
    onStateChange();

    if (newState === 'listening') {
      // 镜像 application.cc:910 — 进 Listening 时无条件 SendStartListening(currentMode)
      sendStartListening(this.listeningMode);
    }
    // speaking / idle 不发 ws 帧；仅 UI / shouldSendUplink 受影响
  },

  // setListeningMode 镜像 application.cc:816 SetListeningMode：
  // 跨模式 + 已经 listening 时绕过 setState 的 same-state 守卫，
  // 直接发 SendStartListening(newMode)，告诉服务端模式变了。
  setListeningMode(mode) {
    if (!VALID_MODES.includes(mode)) {
      console.warn('[deviceState] invalid mode:', mode);
      return;
    }
    const previousMode = this.listeningMode;
    this.listeningMode = mode;

    if (this.state === 'listening' && previousMode !== mode) {
      // bug 3.5 的修复路径：直接 SendStartListening(newMode)
      sendStartListening(mode);
      onStateChange();
    } else if (this.state !== 'listening') {
      this.setState('listening');
    } else {
      // already listening 同 mode：no-op
      onStateChange();
    }
  },

  // audioUplink 在每帧 send 之前调一次。
  // 真机：Speaking 时停止上传 opus，除非是 realtime（CONFIG_USE_SERVER_AEC）。
  shouldSendUplink() {
    if (this.state === 'listening') return true;
    if (this.state === 'speaking' && this.listeningMode === 'realtime') return true;
    return false;
  },

  enterSpeaking() {
    this.setState('speaking');
  },

  // 镜像 application.cc:910 — TTS 结束时返回 Listening，
  // setState 内部会自动 SendStartListening(currentMode)，
  // 所以 meeting/realtime 模式会自动重发，对应 bug 3.5 的另一面。
  enterListening() {
    this.setState('listening');
  },

  enterIdle() {
    this.setState('idle');
  },

  sendAbort(reason) {
    sendAbort(reason);
    // 真机 abort 后回 Listening（除非用户切去 Idle）；这里保守只清 speaking
    if (this.state === 'speaking') {
      this.setState('listening');
    }
  },
};

// 调试方便：暴露到 window
window.deviceState = deviceState;
