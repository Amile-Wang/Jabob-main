// ws.js — WebSocket 连接管理
// 浏览器 WS API 不支持设置自定义 header，所以 device-id / client-id 走 query param
// （同 test_page.html:1418）。Authorization Bearer token 同样无法在浏览器设置，
// 真机走 header，浏览器只能省略或塞进 query —— 服务端默认 auth 关闭时不影响。

import { log } from './ui.js';

let ws = null;
let listeners = {
  onText: null,    // (jsonString, parsed) => void
  onBinary: null,  // (ArrayBuffer) => void
  onOpen: null,    // () => void
  onClose: null,   // (closeEvent) => void
};

export function setListeners(next) {
  listeners = { ...listeners, ...next };
}

export function getReadyState() {
  return ws ? ws.readyState : WebSocket.CLOSED;
}

export function isOpen() {
  return ws !== null && ws.readyState === WebSocket.OPEN;
}

// url: ws://host:port/xiaozhi/v1 或经 nginx 的 ws://host/ws
// extras: { deviceId, clientId, token }
export function connect(url, extras) {
  if (ws && ws.readyState !== WebSocket.CLOSED) {
    log('[ws] already connected/connecting, ignoring connect()', 'warning');
    return;
  }

  let connUrl;
  try {
    connUrl = new URL(url);
  } catch (e) {
    log(`[ws] invalid URL: ${url}`, 'error');
    return;
  }

  if (extras.deviceId) connUrl.searchParams.set('device-id', extras.deviceId);
  if (extras.clientId) connUrl.searchParams.set('client-id', extras.clientId);
  // 浏览器无法设 Authorization header；如果配了 token，塞进 query 让服务端自取
  if (extras.token) connUrl.searchParams.set('token', extras.token);

  log(`[ws] connecting: ${connUrl.toString()}`, 'info');
  const t0 = performance.now();
  ws = new WebSocket(connUrl.toString());
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    log(`[ws] open (${(performance.now() - t0).toFixed(1)}ms)`, 'success');
    if (listeners.onOpen) listeners.onOpen();
  };

  ws.onclose = (ev) => {
    log(`[ws] close code=${ev.code} reason="${ev.reason || ''}" clean=${ev.wasClean}`, 'warning');
    if (ev.code === 1006) {
      log('[ws] code=1006 — TCP/握手失败（后端未监听 / nginx 未转发 Upgrade / 路径错误 / 拒绝）', 'error');
    }
    if (listeners.onClose) listeners.onClose(ev);
    ws = null;
  };

  ws.onerror = (err) => {
    log(`[ws] error type=${err.type}`, 'error');
  };

  ws.onmessage = (event) => {
    if (typeof event.data === 'string') {
      let parsed = null;
      try {
        parsed = JSON.parse(event.data);
      } catch (e) {
        log(`[ws<=] non-json text: ${event.data.slice(0, 200)}`, 'warning');
      }
      if (event.data.length < 400) {
        log(`[ws<=] text: ${event.data}`, 'debug');
      } else {
        log(`[ws<=] text(${event.data.length}B): ${event.data.slice(0, 200)}…`, 'debug');
      }
      if (listeners.onText) listeners.onText(event.data, parsed);
    } else if (event.data instanceof ArrayBuffer) {
      // 不打印每个二进制帧的 log；audioDownlink 会按需打
      if (listeners.onBinary) listeners.onBinary(event.data);
    } else if (event.data instanceof Blob) {
      // 某些浏览器实现可能传 Blob —— 转 ArrayBuffer 再分发
      event.data.arrayBuffer().then((ab) => {
        if (listeners.onBinary) listeners.onBinary(ab);
      });
    }
  };
}

export function disconnect() {
  if (ws) {
    ws.close();
    ws = null;
  }
}

export function sendText(payload) {
  if (!isOpen()) {
    log('[ws=>] dropped text (not open)', 'warning');
    return false;
  }
  const s = typeof payload === 'string' ? payload : JSON.stringify(payload);
  if (s.length < 400) {
    log(`[ws=>] text: ${s}`, 'debug');
  } else {
    log(`[ws=>] text(${s.length}B): ${s.slice(0, 200)}…`, 'debug');
  }
  ws.send(s);
  return true;
}

export function sendBinary(arrayBufferOrTyped) {
  if (!isOpen()) return false;
  ws.send(arrayBufferOrTyped);
  return true;
}
