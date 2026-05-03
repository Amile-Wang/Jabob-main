// metrics.js — 端到端 latency 跟踪
// 一轮"对话"的时间分段：
//   T0  = max(最后一帧 opus 上传时间, 最后一次 detect 文字注入时间) ─ 即"用户输入完成"
//   T1  = stt 文字到达                                                 ─ 服务端 ASR 完成
//   T2  = 第一个 tts.sentence_start 文字到达                           ─ LLM 首字
//   T3  = 第一个 binary opus 帧到达                                    ─ TTS 首音
//   T4  = tts.stop 到达                                                ─ TTS 播完
//
// 每段差值含义：
//   T1-T0  ASR + 服务端调度（VAD 切句到识别文字落地）
//   T2-T1  LLM 首字（含 intent/RAG/工具调用）
//   T3-T2  TTS 首字到首音的合成 + 网络
//   T4-T3  TTS 全部送完
//   T4-T0  端到端总耗时

const TURN_INIT = () => ({
  trigger: null,    // 'voice' | 'detect'
  t0: null,
  t_stt: null,
  t_first_tts_text: null,
  t_first_tts_audio: null,
  t_tts_stop: null,
  user_text: null,
  asst_text: '',
});

let lastUplinkAt = 0;
let lastDetectAt = 0;
let current = TURN_INIT();
let onFinalize = null;  // (turn) => void —— 由 ui.js 注入用于追加汇总条

export function setOnTurnFinalize(cb) {
  onFinalize = cb;
}

// audioUplink 每发一帧 opus 调一次（轻量，不打 log）
export function markUplinkFrameSent(t = performance.now()) {
  lastUplinkAt = t;
}

// 用户点 Send detect 时调
export function markDetectInjected(t = performance.now()) {
  lastDetectAt = t;
  // detect 走的是文字直接注入；T0 立刻确定为这一刻，
  // 不依赖后续 stt 来推断（stt 在 detect 路径下回的就是注入文字本身）
  current = TURN_INIT();
  current.trigger = 'detect';
  current.t0 = t;
}

// stt 到达：开 turn（如果还没开），定 T1
export function markStt(text, t = performance.now()) {
  // 如果 turn 还没起（auto/realtime/meeting 模式 user 没显式触发 detect）
  // T0 推断为最后一帧 opus 上传时刻
  if (current.t0 == null) {
    current = TURN_INIT();
    current.trigger = 'voice';
    current.t0 = lastUplinkAt || t;  // 没上传过帧时退化为现在（不准但不崩）
  }
  current.t_stt = t;
  current.user_text = text;
  return delta('t0', 't_stt');
}

export function markFirstTtsText(text, t = performance.now()) {
  if (current.t_first_tts_text == null) {
    current.t_first_tts_text = t;
  }
  if (text) current.asst_text += (current.asst_text ? ' ' : '') + text;
  return delta('t0', 't_first_tts_text');
}

export function markTtsAudioFrame(t = performance.now()) {
  if (current.t_first_tts_audio == null && current.t0 != null) {
    current.t_first_tts_audio = t;
  }
}

export function markTtsStop(t = performance.now()) {
  if (current.t0 == null) return null;
  current.t_tts_stop = t;
  const finalized = { ...current };
  if (onFinalize) {
    try { onFinalize(finalized); } catch (e) { /* swallow */ }
  }
  // turn 结束 → 下一轮重开
  current = TURN_INIT();
  return finalized;
}

// 取从 T0 到指定字段的累计 ms（用于「+Nms」 inline 显示）
function delta(fromKey, toKey) {
  const a = current[fromKey];
  const b = current[toKey];
  if (a == null || b == null) return null;
  return Math.round(b - a);
}

export function getCurrent() {
  return current;
}

// 工具：把 ms 美化为 "480ms" / "1.24s"
export function fmtMs(ms) {
  if (ms == null) return '—';
  if (ms < 1000) return `${ms}ms`;
  return `${(ms / 1000).toFixed(2)}s`;
}

// 给 turn 计算各段 delta
export function summarize(turn) {
  const seg = (a, b) => (turn[a] != null && turn[b] != null) ? Math.round(turn[b] - turn[a]) : null;
  return {
    asr: seg('t0', 't_stt'),
    llm: seg('t_stt', 't_first_tts_text'),
    tts_first: seg('t_first_tts_text', 't_first_tts_audio'),
    tts_total: seg('t_first_tts_audio', 't_tts_stop'),
    end_to_end: seg('t0', 't_tts_stop'),
    end_to_first_audio: seg('t0', 't_first_tts_audio'),
  };
}
