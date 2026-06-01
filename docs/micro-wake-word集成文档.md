# micro-wake-word 唤醒词引擎集成文档

## 1. 集成概述

micro-wake-word 是 Open Home Foundation 维护的开源唤醒词框架（[OHF-Voice/micro-wake-word](https://github.com/OHF-Voice/micro-wake-word)），ESPHome / Home Assistant 生态默认方案。本项目通过新增 `CONFIG_USE_MICRO_WAKE_WORD` 后端引入，与现有 AFE / ESP / CUSTOM / DSPOTTER 四个后端**互斥共存**，编译期由 Kconfig 单选。

### 关键特性
- **开源 Apache-2.0**：去 trial 依赖（DSPOTTER 是 Cyberon 试用静态库），可商用
- **TFLite Micro 后端**：用 Espressif 官方 `esp-tflite-micro` + `esp-nn` 加速，原生支持 ESP32-S3 / P4 SIMD
- **流式推理**：每 30 ms 出 3 帧 spectrogram + 多次 wake-word 推理，连续多窗超阈值才触发
- **可自训自定义唤醒词**："嗨 Jabobo" 这种中文唤醒词长期可走 micro-wake-word 训练管线（Piper sample generator + 流式 MixConv）自训
- **运行时不依赖外部 license** （DSPOTTER 需要 NVS license 分区做设备绑定，micro-wake-word 不需要）

### 当前状态（2.0.6+ 分支，前端配置自训已上线）
- 默认模型：自训 **Hi Jabra** 流式模型 `stream_state_internal_quant.tflite`（约 62 KB，INT8 量化）
- **前端 Dashboard 配置自训唤醒词已全链路打通**（主路径，2026-05 上线）：用户在前端输入任意中/英文唤醒词文本 → backend `sync-config` → 自动跑 `train_wakeword.py` + `deploy_wakeword.py` → 覆盖 `stream_state_internal_quant.tflite` + 生成 `wake_word_config.h` → 下一次 firmware build 即生效。详见 §7.1。
- 中文唤醒词已支持：[`_run_wakeword_training`](../../jabobo-backend/app/routes/jabobo_config.py) 根据是否含汉字自动选 voice 集（英文 `lessac/amy/joe/alan/norman/libritts_r`、中文 `chaowen/huayan/xiao_ya`）。`wakeword train/trained_models/` 下已有 hi_jabra / hi_ryder / hi_tianhao / hi_elva 等多套自训模型沉淀
- 模型与 esphome/micro-wake-word v2 框架同源 —— 前端 `audio_preprocessor_int8.tflite` / op resolver 清单全部复用，**只换最末端的 wake-word 模型本身**
- **模型输出量化为 int8**（不同于 hey_jarvis 老 PoC 模型的 uint8），运行时按 `scale + zero_point` 反量化到 [0,1] 概率再缩放到 0-255 跟滑窗对齐；详见 [micro_wake_word.cc](../main/audio/wake_words/micro_wake_word.cc) `RunFrame()` 中对 `kTfLiteInt8` 的分支
- **`Start()` 必须 `resource_vars_->ResetAll()`**：streaming 模型靠 TF Resource Variables 持久化 hidden state，上一次会话残留会污染本次首批推理 → 漏检 / 误检
- DSPOTTER 代码 / `dspotter` 分区 / `HeyJabra_Lv3_Enc1_pack_WithTxt.bin` 模型文件**都不删**，作为回退路径（切换方法见 §5）
- 此前的 PoC 模型 `hey_jarvis.tflite` / `hey_jarvis.json` 已移除（v2 模型生态内手工换模型方法见 §7.2）

## 2. 文件结构

```
main/audio/wake_words/
├── micro_wake_word.h           # MicroWakeWord 子类声明（继承 WakeWord 抽象基类）
├── micro_wake_word.cc          # 推理调度 + 滑窗触发 + Opus 重放
├── micro_features/
│   ├── audio_preprocessor_int8_model_data.h  # 前端 .tflite 嵌入 C 数组（54 KB）
│   ├── micro_features_generator.h            # 单帧前端推理接口
│   ├── micro_features_generator.cc           # GenerateFeature 实现
│   └── micro_model_settings.h                # 16 kHz / 30 ms 窗 / 10 ms stride / 40 维
└── models/
    ├── stream_state_internal_quant.tflite    # Hi Jabra wake-word 流式模型（约 62 KB，INT8）
    ├── HeyJabra_Lv3_Enc1_pack_WithTxt.bin    # DSpotter 回退用模型，CONFIG_USE_DSPOTTER_WAKE_WORD=y 时才嵌入
    └── LICENSE.micro-wake-word-models.txt    # 模型框架 Apache-2.0 license 副本（同源）
```

`audio_preprocessor_int8_model_data.h` 与 `micro_features_generator.{h,cc}` 改编自 `managed_components/espressif__esp-tflite-micro/examples/micro_speech/main/`，license header 保留 Apache-2.0，stride 从 20 ms 改回 10 ms 对齐 micro-wake-word 训练参数。

## 3. 推理框架来源

```
esp-tflite-micro 1.3.5    ← Espressif IDF managed component
   ├── tensorflow/lite/micro/   ← TFLite Micro 上游 vendor（解释器、内存分配器、op kernel）
   ├── third_party/             ← gemmlowp / ruy / kissfft 等数值库
   ├── signal/src/              ← FFT / PCAN / filter_bank / log 的 C 实现
   ├── signal/micro/kernels/    ← 上面 DSP 算子注册成 TFLite op kernel
   │   （AddRfft / AddPCAN / AddFilterBank 这些"音频特化 op"）
   └── examples/micro_speech/   ← 我们抄前端代码的来源（Apache-2.0）

esp-nn 1.2.0              ← Espressif NN 加速库
                            （ESP32-S3 / P4 SIMD/PIE 矢量指令加速 INT8 Conv2D / DepthwiseConv2D）

esp-dsp                   ← FFT / IIR / FIR 矢量加速（signal 库的依赖）
```

引入方式（`main/idf_component.yml`）：

```yaml
espressif/esp-tflite-micro: ^1.3.5
```

`esp-nn` 由 `esp-tflite-micro` 内部依赖自动拉，**不需要单独声明**。

## 4. 检测原理

```
麦克风 PCM (16 kHz int16, AFE 处理后)
       │
       ▼ wake_word_->Feed(480 samples = 30 ms)
┌──────────────────────────────────────┐
│ MicroWakeWord::Feed (audio_processor │
│  task 上下文，push 模型，无独立线程) │
│                                      │
│ 1. 拼 800-sample buffer：            │
│    history_pcm_(320) + new(480)      │
│                                      │
│ 2. 按 stride 10 ms 滑出 3 个 30 ms  │
│    窗口（offset 0/160/320）          │
│                                      │
│ 3. 每窗跑一次前端 interpreter：      │
│    audio_preprocessor.tflite         │
│      ├─ 16 kHz PCM → 40 维 int8     │
│      └─ PCAN / AGC / log / mel       │
│      → 1 帧 spectrogram              │
│                                      │
│ 4. 每帧累积进 wake-word interp 输入  │
│    ([1, S, 40, 1]，S 由模型决定)，   │
│    满 S 帧调一次 invoke：            │
│    stream_state_internal_quant.tflite│
│      → 1 个 int8 量化值 → 反量化     │
│        到 0-255 概率                 │
│                                      │
│ 5. 滑窗聚合（window_size=5）：       │
│    sum(recent_probs) > cutoff*N      │
│    且通过 cool-off 才触发            │
│                                      │
│ 6. 触发 → Stop() → callback         │
│         (last_detected_wake_word_)   │
└──────────────────────────────────────┘
       │
       ▼ AudioService 的 on_wake_word_detected 回调
       │
       ▼ application.cc OnWakeWordDetected
       │   状态机：Idle → Connecting → Listening
       │
       ▼ protocol_->SendWakeWordDetected(text)
            JSON: {"type":"listen","state":"detect","text":"Hi Jabra",...}
```

阈值参数有**两个来源**，优先级：deploy header > Kconfig。

**Kconfig**（默认值兜底，`menuconfig` 入口在 `Component config → Audio & Hardware`）：

| Kconfig | 默认 | 用途 |
|---|---|---|
| `MICRO_WAKE_WORD_DISPLAY` | `"Hi Jabra"` | 触发时回调里携带的文本 + 服务端 JSON 字段。**只在没跑过 deploy 时生效** |
| `MICRO_WAKE_WORD_THRESHOLD_X100` | `50` | 概率阈值（百分比）。Hi Jabra 实测 50 适合；hey_jarvis 等 esphome PoC 模型 manifest 推荐 97 |
| `MICRO_WAKE_WORD_WINDOW_SIZE` | `5` | 滑窗大小，连续 N 帧加和超过 cutoff×N 才触发 |

**`wake_word_config.h`**（由 [`deploy_wakeword.py`](../../wakeword%20train/deploy_wakeword.py) 自动生成，位于 `main/audio/wake_words/wake_word_config.h`，**不入库**）：

| 宏 | 来源字段 | 行为 |
|---|---|---|
| `WAKE_WORD_DISPLAY_NAME` | `wake_word_config.json` 的 `display_name` | 覆盖 `CONFIG_MICRO_WAKE_WORD_DISPLAY` |
| `WAKE_WORD_THRESHOLD_X100` | `wake_word_config.json` 的 `threshold_x100`（默认 50） | 覆盖 `CONFIG_MICRO_WAKE_WORD_THRESHOLD_X100` |
| `WAKE_WORD_WINDOW_SIZE` | `wake_word_config.json` 的 `window_size`（默认 5） | 覆盖 `CONFIG_MICRO_WAKE_WORD_WINDOW_SIZE` |

[micro_wake_word.cc](../main/audio/wake_words/micro_wake_word.cc) 用 `__has_include("wake_word_config.h")` 条件 include；header 存在则在 ctor 末尾 override 三个值，不存在则保留 Kconfig 默认。所以：

- **手工换 .tflite（§7.2 路径）**：menuconfig 改 Kconfig，rebuild。
- **前端配置自训（§7.1 主路径）**：用户输入 → deploy 自动写 header + 覆盖 .tflite，rebuild。不用动 Kconfig，但**必须** rebuild 否则新 header 编不进 binary。

## 5. 编译开关 / 切换流程

四个后端 Kconfig 互斥（单向 `depends on`，单选生效）：

```
USE_MICRO_WAKE_WORD
    depends on (ESP32S3 || ESP32P4) && SPIRAM
        && (!USE_AFE_WAKE_WORD)
        && (!USE_CUSTOM_WAKE_WORD)
        && (!USE_DSPOTTER_WAKE_WORD)
```

切换到 mww：

```bash
cd /home/azureuser/tianhao/my_code/Jabobo/jabobo-main
source /home/azureuser/esp/v5.4.3/esp-idf/export.sh

# 选项 A：menuconfig 交互
idf.py menuconfig
# 取消 USE_AFE_WAKE_WORD / USE_DSPOTTER_WAKE_WORD，勾选 USE_MICRO_WAKE_WORD

# 选项 B：直接改 sdkconfig
sed -i 's/^CONFIG_USE_DSPOTTER_WAKE_WORD=y/# CONFIG_USE_DSPOTTER_WAKE_WORD is not set/' sdkconfig
echo 'CONFIG_USE_MICRO_WAKE_WORD=y' >> sdkconfig
echo 'CONFIG_MICRO_WAKE_WORD_DISPLAY="Hi Jabra"' >> sdkconfig
echo 'CONFIG_MICRO_WAKE_WORD_THRESHOLD_X100=50' >> sdkconfig
echo 'CONFIG_MICRO_WAKE_WORD_WINDOW_SIZE=5' >> sdkconfig

# 触发 reconfigure（EMBED_FILES 改变 / Ninja 不会自动重 configure）
touch main/CMakeLists.txt && idf.py reconfigure
idf.py build
```

回滚到 DSPOTTER：反向操作（关 mww，开 DSPOTTER）即可，**不需要删任何代码或文件**。

## 6. 模型文件要求

### 6.1 硬约束（不满足固件加载会失败）

| 项 | 要求 |
|---|---|
| 容器格式 | FlatBuffer（`.tflite`） |
| Schema 版本 | 等于 `TFLITE_SCHEMA_VERSION`（esp-tflite-micro 1.3.5 → 3） |
| 量化 | 全 INT8（`tf.lite.OpsSet.TFLITE_BUILTINS_INT8`），输入 int16；**输出 int8 或 uint8 都行** —— `RunFrame()` 看 `output->type` 自动选反量化路径 |
| 架构 | **流式**（用 TF Resource Variables 持久化 state；输入 shape `[1, stride, 40, 1]`，输出 `[1, 1]`）|
| Op 集合 | 只能用 `MicroWakeWord::RegisterStreamingOps` 注册的 20 个（详见 `micro_wake_word.cc:RegisterStreamingOps`）|
| 输入特征维度 | 40（与前端 `audio_preprocessor_int8.tflite` 输出对齐）|
| 采样率 | 16 kHz |
| 窗口 / stride | 30 ms / 10 ms（前端模型 baked-in）|

### 6.2 不接受的模型类型

| 类型 | 原因 |
|---|---|
| float32 / float16 | esp-nn 不加速 float；op resolver 没注册 float kernel |
| 输入 raw PCM 的端到端模型 | 没接前端，输入维度不对 |
| 用 LSTM / GRU 标准 RNN op 实现状态 | op 集合没注册 LSTM 系列；mww 用 Resource Variables 是为了避开这个 |
| 输入采样率 ≠ 16 kHz | 前端 .tflite 写死 16 kHz |
| 用 TFLite Select Ops（部分 TF 算子） | TFLite Micro 不支持 |
| ONNX / PyTorch / Keras 原始模型 | 必须先转成 INT8 量化 .tflite |

### 6.3 Op 集合（20 个）

| 类别 | Op |
|---|---|
| 流式 state 机制 | `CallOnce` `VarHandle` `ReadVariable` `AssignVariable` |
| 张量整形 | `Reshape` `StridedSlice` `Concatenation` `Pad` `Pack` `SplitV` |
| 卷积 | `Conv2D` `DepthwiseConv2D` |
| 全连接 | `FullyConnected` |
| 池化 | `AveragePool2D` `MaxPool2D` `Mean` |
| 算术 | `Mul` `Add` |
| 激活 / 输出 | `Logistic`（sigmoid） `Quantize` |

模型用了清单外 op → `AllocateTensors()` 报 `Didn't find op for builtin opcode 'XXX'` → `LoadWakeWordModel` 返回 false → wake_word 是 nullptr → 设备启动正常但 wake word 完全失效（不会 crash）。

回退方案：用 `tflite::AllOpsResolver`（多约 80 KB code size），但 PoC 阶段没必要。

### 6.4 内存预算

```
tensor_arena (PSRAM):
   起点常量 kBaseTensorArenaSize: 22860 bytes（沿用 hey_jarvis manifest 值，Hi Jabra 模型同源够用）
   probe 序列:             [22860 → 45720]，16 字节对齐，AllocateTensors 失败自动翻倍重试一次
   实际占用:               启动日志 "arena=XX used=YY"

variable_arena (PSRAM):    1024 bytes 固定（kVariableArenaSize）
最多 resource variables:   20（kMaxResourceVariables）
preprocessor_arena (内存): 16 KB 静态分配（micro_features_generator.cc）

模型本体 .tflite:           Hi Jabra (stream_state_internal_quant.tflite) ≈ 62 KB
                            preprocessor (audio_preprocessor_int8) ≈ 54 KB
                            两者都通过 EMBED_FILES 嵌入 binary，不占运行时内存
```

## 7. 替换 / 升级模型流程

两条并行路径，按场景挑：

| 路径 | 何时用 | 是否动 Kconfig/CMakeLists |
|---|---|---|
| **§7.1 前端配置自训（主路径）** | 用户/产品要换唤醒词文本，做任意中/英文自训 | 否，basename 永远是 `stream_state_internal_quant.tflite` |
| **§7.2 手工换 .tflite（备用）** | 在 ESPHome v2 模型生态里换 PoC 模型，或要保留旧模型 basename 做对比 | 是，需手工改 asm symbol / CMakeLists / Kconfig |
| **§7.3 跨训练框架（不推荐）** | openWakeWord / Speechbrain 等异构模型 | 是，且要重写前端 |

### 7.1 前端配置自训（主路径）

**链路总览**：

```
前端 Dashboard 输入唤醒词文本（任意中/英文，如 "嘿小捷" / "Hello Ryder"）
  │
  ▼ POST /api/user/sync-config（jabobo-backend, port 8007）
  │
  ▼ jabobo_config.py: _normalize_wake_word(raw)
  │   "嘿小捷"        → "嘿_小捷"    （code name，仅用于目录/文件名）
  │   "Hello Ryder"  → "hello_ryder"
  │   raw 原文同时保存到 wake_word_raw_text，作为 train_wakeword.py 的 --text 参数
  │   （TTS 真听到的音，保留原始语调控制）
  │
  ▼ DB 写 user_personas: wake_word_text=<code>, wake_word_model_status=1（training）
  │
  ▼ asyncio.create_task(_run_wakeword_training(code, device_id, raw))
  │
  ▼ 1. 检查 wakeword train/trained_models/<code>/<code>.tflite 是否存在
  │     已存在 → 跳到 deploy
  │     不存在 → 训练（含中英文自动选 voice）：
  │       a. conda run -n wakeword python train_wakeword.py <code> \
  │            --generate-samples --text "<raw>" --max-samples 250 \
  │            --voices <ZH_VOICES 或 EN_VOICES> \
  │            --length-scales 0.7 0.85 1.0 1.15 1.3
  │       b. conda run -n wakeword python train_wakeword.py <code>
  │
  ▼ 2. conda run -n wakeword python deploy_wakeword.py <code>
  │     ├─ 复制 trained_models/<code>/<code>.tflite
  │     │    →   jabobo-main/main/audio/wake_words/models/stream_state_internal_quant.tflite
  │     └─ 写 jabobo-main/main/audio/wake_words/wake_word_config.h
  │           ├─ #define WAKE_WORD_DISPLAY_NAME  "<display from json>"
  │           ├─ #define WAKE_WORD_THRESHOLD_X100 <int>
  │           └─ #define WAKE_WORD_WINDOW_SIZE   <int>
  │
  ▼ DB 更新 wake_word_model_status：1（ready）或 2（failed）
  │
  ▼ ⚠️ 设备真正用上新唤醒词还需要：
       1) 重新 build 固件（`idf.py build`，详见 §5 节硬规矩）
       2) 走 OTA 推到在野设备（详见 jabobo-dev skill 的 OTA 章节）
```

**关键事实**：

1. **basename 不变**：deploy 永远写到 `stream_state_internal_quant.tflite`，所以 [micro_wake_word.cc](../main/audio/wake_words/micro_wake_word.cc) 里的 asm symbol `_binary_stream_state_internal_quant_tflite_start/_end` 和 [main/CMakeLists.txt](../main/CMakeLists.txt) 里的 `MWW_MODEL_FILES` 都**不用动**。
2. **`wake_word_config.h` 真生效**：[micro_wake_word.cc](../main/audio/wake_words/micro_wake_word.cc) 文件顶部用 `__has_include` 条件 include 它；ctor 末尾用 header 里的宏 override Kconfig 默认值。所以前端配的 display 名 / 阈值 / window 真会被固件念出来，不是死代码。具体见 §4 的「两个来源」表。
3. **必须 rebuild firmware**：deploy 只到「覆盖文件 + 写 header」为止，**不动正在跑的固件**。EMBED_FILES 是编译时打包到 binary 里的，所以 deploy 完必须重新 `idf.py build` + OTA 才能让设备用上新模型。这是一个常见误解，CI/部署链路里别假设「训练成功 = 设备已升级」。
4. **`wake_word_config.h` 不入库**：jabobo-main 不是 git repo，但即便是也不会入库；新拉的工作树没这个文件，[micro_wake_word.cc](../main/audio/wake_words/micro_wake_word.cc) 用 `__has_include` 守卫，没 header 时回落到 Kconfig 默认，编译不会失败。

**前端字段**（前端 Dashboard 的 `wake_word_text` 输入框 + sync 按钮已实现，详见 jabobo-dev skill「加唤醒词自动训练 + 部署」段）。

**状态查询**：`GET /api/user/wake-word-status?jabobo_id=<MAC>` 返回 `{wake_word_text, model_status, task_status, task_message, elapsed_seconds, start_time}`。`model_status` 取值：`0=未训练 / 1=训练中或已 ready / 2=失败`。

**训练日志**：`wakeword train/_training_logs/<code>_<timestamp>.log`，包含 generate-samples + train + deploy 三段 stdout/stderr。失败时 grep 这个文件。

### 7.2 手工换 .tflite（备用，ESPHome v2 模型生态内）

不走前端自训，在 [esphome/micro-wake-word-models v2](https://github.com/esphome/micro-wake-word-models/tree/main/models/v2) 里挑现成模型（同一套前端参数，**只需换文件**）。下面以从当前 `stream_state_internal_quant.tflite` 换回 `hey_jarvis.tflite` 为例：

```bash
# 1. 拷新模型
cp /tmp/mww-models/models/v2/hey_jarvis.tflite \
   main/audio/wake_words/models/

# 2. 改三处源码（这条路径要动 basename，所以 asm symbol / CMakeLists 都要同步）
# (a) micro_wake_word.cc 里的 asm symbol 名（_binary_<basename>_start/_end）
sed -i 's/_binary_stream_state_internal_quant_tflite/_binary_hey_jarvis_tflite/g' \
    main/audio/wake_words/micro_wake_word.cc

# (b) main/CMakeLists.txt 里的 MWW_MODEL_FILES
sed -i 's|stream_state_internal_quant.tflite|hey_jarvis.tflite|' main/CMakeLists.txt

# (c) Kconfig 默认 + sdkconfig 当前值
# Kconfig: MICRO_WAKE_WORD_DISPLAY default 改成 "Hey Jarvis"
# sdkconfig: CONFIG_MICRO_WAKE_WORD_DISPLAY="Hey Jarvis"
#            CONFIG_MICRO_WAKE_WORD_THRESHOLD_X100=90  （hey_jarvis manifest 推荐 97）

# (d) 如果之前跑过前端自训留下了 wake_word_config.h，要删掉，
#     否则它的宏会 override 你刚改的 Kconfig（见 §4 的两来源表）
rm -f main/audio/wake_words/wake_word_config.h

# 3. 看新模型 manifest（若有），对应改 Kconfig 阈值
#   probability_cutoff / sliding_window_size / tensor_arena_size 各模型不同
#   必要时改 MICRO_WAKE_WORD_THRESHOLD_X100 / WINDOW_SIZE，或改代码 kBaseTensorArenaSize

# 4. 强制 reconfigure（EMBED_FILES 变了，Ninja 不会自动 re-configure）
touch main/CMakeLists.txt && idf.py reconfigure && idf.py build
```

> **注意输出量化兼容性**：如果新模型输出是 int8（如 Hi Jabra）vs uint8（如 hey_jarvis），代码已经在 `RunFrame()` 里 `if (output->type == kTfLiteInt8) ... else ...` 兼容，不用改源码。

### 7.3 跨训练框架（不推荐）

例如 openWakeWord、Speechbrain 等。问题：

- **前端参数不一致** → 必须重写前端，工程量极大
- **模型架构 op 可能超出当前 20 op resolver** → 报错/失败
- **量化方案可能不对齐** → 输入/输出 dtype 跟代码假设不一致

如果一定要跨框架，**建议训练侧先对齐 micro-wake-word 的前端参数**（16 kHz / 30 ms / 10 ms / 40 维 / PCAN 80.0/0.95/log shift 6），否则不能复用本固件代码。

## 8. 训练参数 vs 替换的关系

micro-wake-word "替换"分两个层级，**两层都对齐**才能换：

### 层级 1：前端参数 baked-in（硬约束）

audio_preprocessor.tflite 里的所有数值都是模型权重 baked-in：

| 参数 | 值 | 替换时能不能改 |
|---|---|---|
| 采样率 | 16 kHz | ❌ 不能 |
| 窗口长度 | 30 ms (480 samples) | ❌ 不能 |
| stride | 10 ms (160 samples) | ❌ 不能 |
| 频谱维度 | 40 | ❌ 不能 |
| FFT 点数 / mel 边界 | 125-7500 Hz / 40 bin | ❌ 不能 |
| PCAN gain offset / strength | 80.0 / 0.95 | ❌ 不能 |
| log scale shift | 6 | ❌ 不能 |

新模型训练侧改了任意一项 → 前端必须换 → 往往要重新移植代码。

### 层级 2：模型架构 / 阈值（软约束）

| Manifest 字段 | 我代码里对应 | 替换时怎么办 |
|---|---|---|
| `tensor_arena_size`（22860） | `kBaseTensorArenaSize` 常量 | 显著大改源码常量 |
| `probability_cutoff`（0.97） | Kconfig `MICRO_WAKE_WORD_THRESHOLD_X100` | menuconfig 改 |
| `sliding_window_size`（5） | Kconfig `MICRO_WAKE_WORD_WINDOW_SIZE` | menuconfig 改 |
| `feature_step_size`（10ms） | `micro_model_settings.h:kFeatureStrideMs` | 必须 = 10（前端 baked-in 决定）|
| 模型 input shape `[1, S, 40, 1]` | `interpreter->input(0)->dims->data[1]` 动态读 | 自适应不用改 |

### 一句话总结

**前端是硬合约，模型是软合约。** 在 ESPHome v2 模型生态内换（同前端，模型架构差异）只动文件名 + manifest 几个数；跨训练框架要重新对齐前端参数才行。

## 9. 端到端验证

### 9.1 编译期验证

```bash
# bin 大小检查（自动跑）
grep "Jabob.bin binary size" build/log/*.log
# 期望：Jabob.bin binary size 0xXXXXXX bytes. Smallest app partition is 0x700000 bytes.

# 版本号嵌入
strings build/Jabob.bin | grep -E "^2\.[0-9]+\.[0-9]+$" | head

# 模型嵌入符号 + 长度（length 应等于 .tflite 文件大小）
nm build/esp-idf/main/libmain.a | grep "_binary_stream_state_internal_quant_tflite\|stream_state_internal_quant_tflite_length"
# 期望（Hi Jabra 模型）：
#   0000f360 R _binary_stream_state_internal_quant_tflite_end
#   00000000 R _binary_stream_state_internal_quant_tflite_start
#   0000f360 R stream_state_internal_quant_tflite_length    ← 0xF360 = 62304 bytes ✓

# sdkconfig 板子型号没漂（reconfigure 偶发会重置 board_type）
grep "CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO" sdkconfig
```

### 9.2 烧录后串口期望日志

```
I (xxx) MicroWakeWord: MicroWakeWord initialized: model=62304 bytes, arena=22864 bytes (PSRAM), wake_word="Hi Jabra", cutoff=127/255, window=5
I (xxx) MicroWakeWord: Wake word interpreter allocated, arena=22864 used=YYYYY
I (xxx) MicroWakeWord: Wake word model input stride=S dims=4D
I (xxx) MicroWakeWord: MicroWakeWord started

# 对设备说 "Hi Jabra"：
I (xxx) MicroWakeWord: Wake word DETECTED: prob=180/255 avg=155/255 cutoff=127 window=5
I (xxx) MicroWakeWord: MicroWakeWord stopped
I (xxx) Application: OnWakeWordDetected: Hi Jabra
```

> `cutoff=127/255` 来自默认 `MICRO_WAKE_WORD_THRESHOLD_X100=50` → `50 * 255 / 100 = 127`。

### 9.3 服务端验证

```bash
pm2 logs jabobo-server --lines 100 | grep -i wake
# 期望：收到 listen detect 帧
#   {"type":"listen","state":"detect","text":"Hi Jabra", ...}
```

## 10. 风险点 / 排障

### 10.1 烧录后说 Hi Jabra 完全无反应

最大可能（按概率排序）：
1. **没 ResetAll**：streaming 模型 hidden state 残留 → 首次 Start 后头几秒漏检。检查 `Start()` 里有没有 `resource_vars_->ResetAll()`（[micro_wake_word.cc](../main/audio/wake_words/micro_wake_word.cc)）
2. **输出量化分支没走对**：Hi Jabra 输出是 int8，如果 `RunFrame()` 还是直接读 `output->data.uint8[0]`，得到的概率全错 → 几乎永远不会触发。检查 `if (output->type == kTfLiteInt8)` 那段
3. **前端 spectrogram 跟训练侧 `pymicro_features.MicroFrontend` 输出不完全对齐** —— 量化误差或 stride 错位

排查顺序：

1. 串口看 `Wake word interpreter allocated, arena=...` —— 没看到 → 模型加载失败，看上面的 ERROR
2. 看 `prob=` 输出（在 `RunFrame` invoke 后临时加 `ESP_LOGI(TAG, "prob=%u float=%.3f", prob, prob_float)`，正常情况安静时 prob ≈ 0-30，说唤醒词时短暂冲到 150+）—— 如果说话时 prob 始终在低位，再考虑前端不对齐
3. 暂时把 `MICRO_WAKE_WORD_THRESHOLD_X100` 调低到 30 + window 调到 3 看是否触发
4. 跟训练侧 evaluate 脚本对比相同 wav 文件输出概率 —— 如果训练侧能触发但设备不能，就是前端 / 量化路径问题

### 10.2 编译报 `_binary_stream_state_internal_quant_tflite_start undefined`

**症状**：链接阶段 undefined symbol `_binary_*_start`。
**原因**：`EMBED_FILES` 加了新文件但 Ninja 没重新生成符号（CMake `file(GLOB)` 类似，是 configure 时刻 snapshot）；或者改了 `micro_wake_word.cc` 里的 asm symbol 但 `CMakeLists.txt` 里的 `MWW_MODEL_FILES` 文件名没同步。
**修复**：

```bash
# 1. 确认两边文件名一致
grep _binary_ main/audio/wake_words/micro_wake_word.cc
grep MWW_MODEL_FILES main/CMakeLists.txt
# 两者的 <basename> 必须完全一致 (basename 中的非字母数字字符会被替成下划线)

# 2. 强制 reconfigure
touch main/CMakeLists.txt && idf.py reconfigure && idf.py build
```

### 10.3 reconfigure 后 `CONFIG_BOARD_TYPE_BREAD_*` 漂回 default

**症状**：`grep BOARD_TYPE sdkconfig` 看到不是 `_TIANHAO_UAC=y`。
**修复**：

```bash
git checkout HEAD -- sdkconfig
# 然后 menuconfig 单独勾 mww（不动 board）
idf.py menuconfig
```

### 10.4 OTA 推到设备没生效

**症状**：OTA 推完版本号没变。
**原因**：`current_version == 期望要烧的版本号` → [ota.cc:437](../main/ota.cc) 的 `if (memcmp(new_app_info.version, current_version) == 0) skip` 跳过。
**修复**：bump `CONFIG_APP_PROJECT_VER`（如 2.0.13 → 2.0.14-mww）后再编再推，并：

```bash
strings build/Jabob.bin | grep -E "^2\.[0-9]+" | head  # 验证版本号嵌入正确
```

### 10.5 推理太慢导致音频管线堆积

**症状**：串口刷 `SLOW frame` 警告，audio_queue 堆积。
**原因**：MicroWakeWord 在 audio_processor task 上下文同步跑（每 30 ms 跑 4 次 interpreter invoke），如果 ESP32-S3 esp-nn 加速没启用或 PSRAM 路径慢可能撑不住。
**排查**：

```bash
# 1. 验证 esp-nn 启用
grep CONFIG_NN_OPTIMIZED sdkconfig
# 期望 =y

# 2. 在 RunFrame 加计时 log，看单次 invoke 耗时
# 期望 < 5 ms（正常）；> 20 ms 就有问题
```

回退方案：把 wake-word 推理挪到独立 FreeRTOS task（参考 dspotter 的做法），用 queue 异步喂 spectrogram。

## 11. License 合规

| 来源 | License | 备注 |
|---|---|---|
| `esp-tflite-micro` 1.3.5 | Apache-2.0 | Espressif IDF managed component |
| `esp-nn` 1.2.0 | Apache-2.0 | esp-tflite-micro 依赖 |
| 抄自 `examples/micro_speech/` 的代码 | Apache-2.0 | 文件顶部保留原 header + SPDX |
| `audio_preprocessor_int8.tflite` 数据 | Apache-2.0 | 来自 esp-tflite-micro example |
| `stream_state_internal_quant.tflite` (Hi Jabra) | 自训模型 | 与 esphome/micro-wake-word v2 框架同源，前端 / op 集合复用；模型权重为本项目自训。框架 license 副本保留在 `models/LICENSE.micro-wake-word-models.txt` |

**没用 ESPHome 的 GPL 代码**——具体地，没拷 `esphome/components/micro_wake_word/{micro_wake_word.cpp, streaming_model.cpp}`（虽然 op resolver 清单参考了它，但代码独立重写）。

## 12. 不在范围内 / 后续工作

- **删除 DSPOTTER 代码**：保留作为回退路径，等 mww 在生产稳定后再做
- **多 wake word 同时启用**：当前每次编译只能选一个后端
- **VAD 模型并跑**：micro-wake-word 仓库提供 `vad.tflite`，可在 wake-word 推理前作为 gating 减少误触；当前没启用
- **自动 OTA 闭环**：当前前端自训完成只到「覆盖 .tflite + 写 header」，仍需人工 `idf.py build` + 手动推 OTA 才能让在野设备真用上。后续可考虑：训练成功后触发 backend CI build → 自动产出新版本 .bin → 自动 bump expected_version。目前的「手工 build + 手工 OTA」是有意保留的人工审核步骤（防止训出问题模型直接推到生产）
- **服务端运行时改动**：本集成完全端侧，xiaozhi-esp32-server 只接收 `SendWakeWordDetected` JSON 帧（路径已存在），无需改。jabobo-backend 侧仅负责训练编排（详见 §7.1）

> 历史项「训练侧 deploy 工具回流」已完成。`deploy_wakeword.py` 已在 jabobo-backend 的 `_run_wakeword_training` 链路里自动调用，并真正消费 `wake_word_config.h` 来覆盖 Kconfig 默认值。详见 §7.1 + §4。

## 13. 关键文件索引

| 路径 | 作用 |
|---|---|
| `main/idf_component.yml` | 引入 esp-tflite-micro 依赖 |
| `main/Kconfig.projbuild` | `USE_MICRO_WAKE_WORD` + `MICRO_WAKE_WORD_DISPLAY/THRESHOLD_X100/WINDOW_SIZE` 四个 config |
| `main/CMakeLists.txt` (elseif + MWW_MODEL_FILES + EMBED_FILES) | 编译条件 + 模型嵌入（当前嵌 `stream_state_internal_quant.tflite`）|
| `main/audio/wake_word.h` | 抽象基类（共用）|
| `main/audio/audio_service.cc` | wake_word 实例化 + Feed 入口（micro 已是一等公民，三处 `#if` 都带 `\|\| CONFIG_USE_MICRO_WAKE_WORD`）|
| `main/audio/wake_words/micro_wake_word.{h,cc}` | 子类实现，含 int8 输出反量化 + `Start()` ResetAll |
| `main/audio/wake_words/micro_features/` | 前端代码 + 数据（audio_preprocessor int8 模型 + GenerateFeature）|
| `main/audio/wake_words/models/` | 当前: `stream_state_internal_quant.tflite` (Hi Jabra 默认 / 或前端自训覆盖后的同名模型) + `HeyJabra_Lv3_Enc1_pack_WithTxt.bin` (DSpotter 回退) + LICENSE |
| `main/audio/wake_words/wake_word_config.h` | **由 `deploy_wakeword.py` 自动生成，不入库**。定义 `WAKE_WORD_DISPLAY_NAME` / `WAKE_WORD_THRESHOLD_X100` / `WAKE_WORD_WINDOW_SIZE`，在 [micro_wake_word.cc](../main/audio/wake_words/micro_wake_word.cc) 里被 `__has_include` 守卫消费，override Kconfig 默认（详见 §4） |
| `sdkconfig.defaults` | 默认开 micro_wakeword，注释里保留 DSpotter 切回方法 |
| `partitions/v1/16m_dspotter_native.csv` | 分区表（共用，未改动）|
| `../jabobo-backend/app/routes/jabobo_config.py` | 训练编排：`sync_config` 接前端唤醒词 → `_normalize_wake_word` → 异步 `_run_wakeword_training` → `train_wakeword.py` + `deploy_wakeword.py`（详见 §7.1）|
| `../wakeword train/` | 训练管线（conda env `wakeword`）：`train_wakeword.py` / `deploy_wakeword.py` / `wake_word_config.json` / `trained_models/<code>/<code>.tflite` / `_training_logs/` |
