// protocol.js — 出站 JSON 严格对齐固件
// 见 protocols/protocol.cc 与 protocols/websocket_protocol.cc:220-271。
// 所有出站消息都带 session_id（来自 deviceState）。

import { sendText } from './ws.js';
import { deviceState } from './deviceState.js';

// 包一层 session_id；若尚未握手成功则带空字符串（与固件 init 状态一致）。
function withSession(obj) {
  return { session_id: deviceState.sessionId || '', ...obj };
}

// 镜像 websocket_protocol.cc:220-243 GetHelloMessage()
// frame_duration 60 = OPUS_FRAME_DURATION_MS（固件 16kHz mono 60ms 帧）
export function sendHello() {
  const hello = {
    type: 'hello',
    version: 1,
    transport: 'websocket',
    features: { mcp: true, aec: false },
    audio_params: {
      format: 'opus',
      sample_rate: 16000,
      channels: 1,
      frame_duration: 60,
    },
  };
  return sendText(hello);
}

// 镜像 protocol.cc:49 SendStartListening
export function sendStartListening(mode) {
  return sendText(withSession({ type: 'listen', state: 'start', mode }));
}

// 镜像 protocol.cc:69 SendStopListening
export function sendStopListening() {
  return sendText(withSession({ type: 'listen', state: 'stop' }));
}

// 镜像 protocol.cc:43 SendWakeWordDetected
export function sendDetect(text) {
  return sendText(withSession({ type: 'listen', state: 'detect', text }));
}

// 镜像 protocol.cc:34 SendAbortSpeaking
// reason: 'wake_word_detected' | undefined（kAbortReasonNone）
export function sendAbort(reason) {
  const obj = { type: 'abort' };
  if (reason) obj.reason = reason;
  return sendText(withSession(obj));
}

// 镜像 protocol.cc:74 SendMcpMessage —— payload 是 JSON-RPC 对象
export function sendMcpReply(payload) {
  return sendText(withSession({ type: 'mcp', payload }));
}
