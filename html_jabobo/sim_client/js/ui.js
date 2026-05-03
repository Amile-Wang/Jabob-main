// ui.js — DOM 渲染：状态 pill / 工具状态 inspector / 日志 / 对话流
// 不持有业务状态，只读 deviceState 渲染。main.js 负责事件 → 调 deviceState。

import { deviceState } from './deviceState.js';

const STATE_COLOR = {
  idle: '#888',
  connecting: '#e0a000',
  listening: '#2faa55',
  speaking: '#3070d0',
};

let domRefs = null;

function getRefs() {
  if (domRefs) return domRefs;
  domRefs = {
    statePill: document.getElementById('statePill'),
    sessionChip: document.getElementById('sessionChip'),
    modeSelect: document.getElementById('modeSelect'),
    abortBtn: document.getElementById('abortBtn'),
    recordBtn: document.getElementById('recordBtn'),
    stopBtn: document.getElementById('stopBtn'),
    wsStatus: document.getElementById('wsStatus'),
    inspector: document.getElementById('toolInspector'),
    logPanel: document.getElementById('logPanel'),
    convo: document.getElementById('conversation'),
  };
  return domRefs;
}

export function onStateChange() {
  const r = getRefs();
  if (!r.statePill) return;
  r.statePill.textContent = deviceState.state;
  r.statePill.style.background = STATE_COLOR[deviceState.state] || '#888';
  r.sessionChip.textContent = deviceState.sessionId
    ? `session: ${deviceState.sessionId.slice(0, 12)}`
    : 'session: —';
  if (r.modeSelect && r.modeSelect.value !== deviceState.listeningMode) {
    r.modeSelect.value = deviceState.listeningMode;
  }
  // Abort 仅在 speaking 态启用
  if (r.abortBtn) r.abortBtn.disabled = deviceState.state !== 'speaking';
  refreshToolInspector();
}

export function refreshToolInspector() {
  const r = getRefs();
  if (!r.inspector) return;
  const s = deviceState.mcpToolState;
  r.inspector.innerHTML = `
    <div><b>volume</b>: ${s.volume}</div>
    <div><b>brightness</b>: ${s.brightness}</div>
    <div><b>theme</b>: ${s.theme}</div>
    <div><b>battery</b>: ${s.battery.level}%
      ${s.battery.charging ? '⚡' : (s.battery.discharging ? '↓' : '')}</div>
    <div><b>visionUrl</b>: ${deviceState.visionUrl || '<span style="color:#aaa">(unset)</span>'}</div>
    <div><b>state</b>: ${deviceState.state} / <b>mode</b>: ${deviceState.listeningMode}</div>
  `;
}

export function setWsStatus(text, color) {
  const r = getRefs();
  if (r.wsStatus) {
    r.wsStatus.textContent = text;
    r.wsStatus.style.color = color || '#888';
  }
}

const LEVEL_COLOR = {
  debug: '#888',
  info: '#222',
  success: '#1b8a3a',
  warning: '#c08000',
  error: '#c33',
};

export function log(message, level = 'info') {
  const r = getRefs();
  // 即便 DOM 还没 ready 也 console
  console.log(`[${level}] ${message}`);
  if (!r.logPanel) return;
  const line = document.createElement('div');
  const ts = new Date();
  const hh = String(ts.getHours()).padStart(2, '0');
  const mm = String(ts.getMinutes()).padStart(2, '0');
  const ss = String(ts.getSeconds()).padStart(2, '0');
  const ms = String(ts.getMilliseconds()).padStart(3, '0');
  line.textContent = `${hh}:${mm}:${ss}.${ms} [${level}] ${message}`;
  line.style.color = LEVEL_COLOR[level] || '#222';
  line.style.fontSize = '12px';
  line.style.fontFamily = 'monospace';
  line.style.whiteSpace = 'pre-wrap';
  r.logPanel.appendChild(line);
  // 限制行数避免 DOM 爆
  while (r.logPanel.childNodes.length > 800) {
    r.logPanel.removeChild(r.logPanel.firstChild);
  }
  r.logPanel.scrollTop = r.logPanel.scrollHeight;
}

export function appendConvo(role, text) {
  const r = getRefs();
  if (!r.convo) return;
  const div = document.createElement('div');
  div.className = `msg msg-${role}`;
  div.textContent = text;
  r.convo.appendChild(div);
  r.convo.scrollTop = r.convo.scrollHeight;
}

export function clearConvo() {
  const r = getRefs();
  if (r.convo) r.convo.innerHTML = '';
}
