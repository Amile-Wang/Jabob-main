// mcpSimulator.js — 浏览器端 MCP server 模拟，对齐 mcp_server.cc:32-228
// 处理三种入站方法：initialize / tools/list / tools/call
// 服务端期望的回包形状（mcp_handler.py:135-155, 371-384）：
//   initialize  → result.serverInfo {name, version}
//   tools/list  → result.tools[] {name, description, inputSchema}
//   tools/call  → result.content[0].text + result.isError

import { deviceState } from './deviceState.js';
import { sendMcpReply } from './protocol.js';
import { log, refreshToolInspector } from './ui.js';

// 工具列表逐字段对齐 mcp_server.cc:39-224。description 文案保持英文原样
// （固件就是英文，不要翻译，否则会影响 LLM 工具选择的命中率）。
const MCP_TOOLS = [
  {
    name: 'self.get_device_status',
    description:
      'Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n' +
      'Use this tool for: \n' +
      '1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n' +
      '2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'self.get_battery_status',
    description:
      'Provides the current battery status of the device, including battery level, charging status, and discharging status.\n' +
      'Use this tool when you need specific battery information.',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'self.audio_speaker.set_volume',
    description:
      'Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.',
    inputSchema: {
      type: 'object',
      properties: { volume: { type: 'integer', minimum: 0, maximum: 100 } },
      required: ['volume'],
    },
  },
  {
    name: 'self.screen.set_brightness',
    description: 'Set the brightness of the screen.',
    inputSchema: {
      type: 'object',
      properties: { brightness: { type: 'integer', minimum: 0, maximum: 100 } },
      required: ['brightness'],
    },
  },
  {
    name: 'self.camera.take_photo',
    description:
      'Take a photo and explain it. Use this tool after the user asks you to see something.\n' +
      'Args:\n' +
      '  `question`: The question that you want to ask about the photo.\n' +
      'Return:\n' +
      '  A JSON object that provides the photo information.',
    inputSchema: {
      type: 'object',
      properties: { question: { type: 'string' } },
      required: ['question'],
    },
  },
  {
    name: 'self.silence_now',
    description:
      'Immediately silence the device and enter standby mode with a 3-second sleep countdown. ' +
      'Use this tool specifically when the user says "shut up" "再见" "goodbye" or equivalent phrases.',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'self.set_meeting_assistant_mode',
    description:
      'Switch the device to meeting assistant mode. In this mode, the device will continuously transcribe audio without actively responding, ' +
      'making it suitable for meeting transcription scenarios.',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'self.exit_meeting_assistant_mode',
    description:
      'Exit meeting assistant mode and return to the normal conversational assistant mode. ' +
      'Use this tool when the user says "退出会议模式", "结束会议", "end meeting", "stop meeting" or equivalent phrases.',
    inputSchema: { type: 'object', properties: {} },
  },
];

// 分发表 —— 镜像 mcp_server.cc 各 lambda 的副作用
const MCP_DISPATCH = {
  'self.get_device_status': () => {
    const s = deviceState.mcpToolState;
    return {
      audio_speaker: { volume: s.volume },
      screen: { brightness: s.brightness, theme: s.theme },
      battery: s.battery,
      network: s.network,
      device_state: deviceState.state,
      listening_mode: deviceState.listeningMode,
    };
  },
  'self.get_battery_status': () => deviceState.mcpToolState.battery,
  'self.audio_speaker.set_volume': (args) => {
    const v = Number(args && args.volume);
    if (Number.isFinite(v) && v >= 0 && v <= 100) {
      deviceState.mcpToolState.volume = Math.round(v);
      refreshToolInspector();
      return true;
    }
    throw new Error('volume out of range');
  },
  'self.screen.set_brightness': (args) => {
    const b = Number(args && args.brightness);
    if (Number.isFinite(b) && b >= 0 && b <= 100) {
      deviceState.mcpToolState.brightness = Math.round(b);
      refreshToolInspector();
      return true;
    }
    throw new Error('brightness out of range');
  },
  'self.camera.take_photo': (args) => {
    const q = (args && args.question) || '';
    return {
      success: false,
      message: `no camera in browser test rig (vision_url=${deviceState.visionUrl || 'unset'}, question="${q}")`,
    };
  },
  'self.silence_now': () => {
    deviceState.enterIdle();
    return { status: 'success', message: 'Device silenced (browser test rig)' };
  },
  'self.set_meeting_assistant_mode': () => {
    deviceState.setListeningMode('meeting');
    return { status: 'success', message: 'Device switched to meeting assistant mode' };
  },
  'self.exit_meeting_assistant_mode': () => {
    deviceState.setListeningMode('auto');
    return { status: 'success', message: 'Device exited meeting assistant mode' };
  },
};

// 把工具返回值（boolean / string / object）统一包成
// MCP tools/call 的 content 数组形态，server 走 raw_result.content[0].text 取。
function wrapToolResult(value) {
  let text;
  if (typeof value === 'boolean') text = value ? 'true' : 'false';
  else if (typeof value === 'string') text = value;
  else if (value === undefined || value === null) text = 'null';
  else text = JSON.stringify(value);
  return { content: [{ type: 'text', text }], isError: false };
}

function wrapToolError(message) {
  return {
    content: [{ type: 'text', text: String(message) }],
    isError: true,
  };
}

// 入口：服务端推过来的 mcp 消息（payload 已 parse）
export function handleIncomingMcp(payload) {
  if (!payload || typeof payload !== 'object') return;
  if (payload.jsonrpc !== '2.0') {
    log(`[mcp] non-2.0 envelope: ${JSON.stringify(payload).slice(0, 200)}`, 'warning');
  }
  // notifications/* 服务端不期待回复
  if (typeof payload.method === 'string' && payload.method.startsWith('notifications/')) {
    return;
  }
  // 如果是 server 主动调用（带 method）—— initialize / tools/list / tools/call
  if (typeof payload.method === 'string') {
    handleServerCall(payload);
    return;
  }
  // 否则可能是 server 在回我们的 call 结果（罕见，但记录）
  if ('result' in payload || 'error' in payload) {
    log(`[mcp] server reply (we don't make calls yet): ${JSON.stringify(payload).slice(0, 200)}`, 'debug');
  }
}

function handleServerCall(payload) {
  const id = payload.id;
  const method = payload.method;
  const params = payload.params || {};

  if (method === 'initialize') {
    // 镜像 mcp_server.cc:308-319
    const caps = params.capabilities;
    if (caps && caps.vision && typeof caps.vision.url === 'string') {
      deviceState.visionUrl = caps.vision.url;
      refreshToolInspector();
    }
    sendMcpReply({
      jsonrpc: '2.0',
      id,
      result: {
        protocolVersion: '2024-11-05',
        capabilities: { tools: {} },
        serverInfo: { name: 'browser-test-rig', version: '0.1.0' },
      },
    });
    log(`[mcp] initialize id=${id} → replied serverInfo`, 'info');
    return;
  }

  if (method === 'tools/list') {
    // 镜像 mcp_server.cc:377 GetToolsList。我们一次性回完，不分页。
    sendMcpReply({
      jsonrpc: '2.0',
      id,
      result: { tools: MCP_TOOLS },
    });
    log(`[mcp] tools/list id=${id} → replied ${MCP_TOOLS.length} tools`, 'info');
    return;
  }

  if (method === 'tools/call') {
    const name = params.name;
    const args = params.arguments || {};
    const handler = MCP_DISPATCH[name];
    if (!handler) {
      sendMcpReply({ jsonrpc: '2.0', id, result: wrapToolError(`Unknown tool: ${name}`) });
      log(`[mcp] tools/call id=${id} name=${name} → unknown tool`, 'warning');
      return;
    }
    try {
      const ret = handler(args);
      sendMcpReply({ jsonrpc: '2.0', id, result: wrapToolResult(ret) });
      log(`[mcp] tools/call id=${id} name=${name} args=${JSON.stringify(args)} → ok`, 'info');
    } catch (e) {
      sendMcpReply({ jsonrpc: '2.0', id, result: wrapToolError(e.message || String(e)) });
      log(`[mcp] tools/call id=${id} name=${name} → error ${e.message}`, 'error');
    }
    return;
  }

  log(`[mcp] unsupported method "${method}" id=${id}`, 'warning');
  sendMcpReply({
    jsonrpc: '2.0',
    id,
    error: { code: -32601, message: `Method not implemented: ${method}` },
  });
}
