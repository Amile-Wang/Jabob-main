# micro-wake-word 唤醒词引擎集成文档

## 1. 集成概述

micro-wake-word 是 Open Home Foundation 维护的开源唤醒词框架（[OHF-Voice/micro-wake-word](https://github.com/OHF-Voice/micro-wake-word)），ESPHome / Home Assistant 生态默认方案。本项目通过新增 `CONFIG_USE_MICRO_WAKE_WORD` 后端引入，与现有 AFE / ESP / CUSTOM / DSPOTTER 四个后端**互斥共存**，编译期由 Kconfig 单选。

### 关键特性
- **开源 Apache-2.0**：去 trial 依赖（DSPOTTER 是 Cyberon 试用静态库），可商用
- **TFLite Micro 后端**：用 Espressif 官方 `esp-tflite-micro` + `esp-nn` 加速，原生支持 ESP32-S3 / P4 SIMD
- **流式推理**：每 30 ms 出 3 帧 spectrogram + 多次 wake-word 推理，连续多窗超阈值才触发
- **可自训自定义唤醒词**："嗨 Jabobo" 这种中文唤醒词长期可走 micro-wake-word 训练管线（Piper sample generator + 流式 MixConv）自训
- **运行时不依赖外部 license** （DSPOTTER 需要 NVS license 分区做设备绑定，micro-wake-word 不需要）

### PoC 现状（v2.0.13 分支）
- 用 [esphome/micro-wake-word-models v2 hey_jarvis.tflite](https://github.com/esphome/micro-wake-word-models) 跑通固件链路
- 中文自训留下一阶段
- DSPOTTER 代码 / `dspotter` 分区 / `HeyJabra_Lv3_Enc1_pack_WithTxt.bin` 模型文件**都不删**，作为回退路径

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
    ├── hey_jarvis.tflite                     # wake-word 流式模型 v2（52 KB）
    ├── hey_jarvis.json                       # manifest（probability_cutoff=0.97 等）
    └── LICENSE.micro-wake-word-models.txt    # 模型仓库 Apache-2.0 license 副本
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
│    hey_jarvis.tflite                 │
│      → 1 个 uint8 概率（0-255）     │
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
            JSON: {"type":"listen","state":"detect","text":"Hey Jarvis",...}
```

阈值参数全部可调（`menuconfig` 入口在 `Component config → Audio & Hardware`）：

| Kconfig | 默认 | 用途 |
|---|---|---|
| `MICRO_WAKE_WORD_DISPLAY` | `"Hey Jarvis"` | 触发时回调里携带的文本 + 服务端 JSON 字段 |
| `MICRO_WAKE_WORD_THRESHOLD_X100` | `90` | 概率阈值（0-99 表示百分比；mww 模型 manifest 推荐 97）|
| `MICRO_WAKE_WORD_WINDOW_SIZE` | `5` | 滑窗大小，连续 N 帧加和超过 cutoff×N 才触发 |

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
echo 'CONFIG_MICRO_WAKE_WORD_DISPLAY="Hey Jarvis"' >> sdkconfig
echo 'CONFIG_MICRO_WAKE_WORD_THRESHOLD_X100=90' >> sdkconfig
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
| 量化 | 全 INT8（`tf.lite.OpsSet.TFLITE_BUILTINS_INT8`），输入 int16，输出 uint8 |
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
   manifest 写多少:        以 hey_jarvis 为例 = 22860 bytes
   probe 序列:             [22860 → 45720]，16 字节对齐
   实际占用:               启动日志 "arena=XX used=YY"

variable_arena (PSRAM):    1024 bytes 固定（STREAMING_MODEL_VARIABLE_ARENA_SIZE）
最多 resource variables:   20（kMaxResourceVariables）
preprocessor_arena (内存): 16 KB 静态分配（micro_features_generator.cc）

模型本体 .tflite:           hey_jarvis ≈ 52 KB / preprocessor ≈ 54 KB
                            两者都通过 EMBED_FILES 嵌入 binary，不占运行时内存
```

## 7. 替换 / 升级模型流程

### 7.1 ESPHome v2 模型生态内换（最简单）

[esphome/micro-wake-word-models v2](https://github.com/esphome/micro-wake-word-models/tree/main/models/v2) 里所有模型用同一套前端参数，**直接换文件就行**：

```bash
# 1. 拷新模型
cp /tmp/mww-models/models/v2/okay_nabu.tflite \
   main/audio/wake_words/models/

# 2. 改三处源码
# (a) micro_wake_word.cc 里的 asm symbol 名（按文件名生成）
sed -i 's/_binary_hey_jarvis_tflite/_binary_okay_nabu_tflite/g' \
    main/audio/wake_words/micro_wake_word.cc

# (b) main/CMakeLists.txt 里的 MWW_MODEL_FILES
sed -i 's|hey_jarvis.tflite|okay_nabu.tflite|' main/CMakeLists.txt

# (c) Kconfig 里的 display 文本（或运行时 menuconfig 改）
# CONFIG_MICRO_WAKE_WORD_DISPLAY="Okay Nabu"

# 3. 看新模型 manifest，对应改 Kconfig
cat main/audio/wake_words/models/okay_nabu.json
# 看 probability_cutoff / sliding_window_size / tensor_arena_size 是否变
# 必要时改 MICRO_WAKE_WORD_THRESHOLD_X100 / WINDOW_SIZE，或改代码 kBaseTensorArenaSize

# 4. 强制 reconfigure（EMBED_FILES 变了，Ninja 不会自动 re-configure）
touch main/CMakeLists.txt && idf.py reconfigure && idf.py build
```

### 7.2 自训中文 "嗨 Jabobo"

走 micro-wake-word 仓库的训练管线（README 第 38 行 `piper-sample-generator` + 训练 notebook），训出 INT8 流式 .tflite，**前端参数全部用 micro-wake-word 默认**。然后按 7.1 流程替换。

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
nm build/esp-idf/main/libmain.a | grep "_binary_hey_jarvis_tflite\|hey_jarvis_tflite_length"
# 期望：
#   0000cc30 R _binary_hey_jarvis_tflite_end
#   00000000 R _binary_hey_jarvis_tflite_start
#   0000cc30 R hey_jarvis_tflite_length      ← 0xCC30 = 52272 bytes ✓

# sdkconfig 板子型号没漂（reconfigure 偶发会重置 board_type）
grep "CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO" sdkconfig
```

### 9.2 烧录后串口期望日志

```
I (xxx) MicroWakeWord: MicroWakeWord initialized: model=52272 bytes, arena=22864 bytes (PSRAM), wake_word="Hey Jarvis", cutoff=229/255, window=5
I (xxx) MicroWakeWord: Wake word interpreter allocated, arena=22864 used=YYYYY
I (xxx) MicroWakeWord: Wake word model input stride=S dims=4D
I (xxx) MicroWakeWord: MicroWakeWord started

# 对设备说 "Hey Jarvis"：
I (xxx) MicroWakeWord: Wake word DETECTED: prob=247/255 avg=235/255 cutoff=229 window=5
I (xxx) MicroWakeWord: MicroWakeWord stopped
I (xxx) Application: OnWakeWordDetected: Hey Jarvis
```

### 9.3 服务端验证

```bash
pm2 logs jabobo-server --lines 100 | grep -i wake
# 期望：收到 listen detect 帧
#   {"type":"listen","state":"detect","text":"Hey Jarvis", ...}
```

## 10. 风险点 / 排障

### 10.1 烧录后说 Hey Jarvis 完全无反应

最大可能：**前端 spectrogram 跟训练侧 `pymicro_features.MicroFrontend` 输出不完全对齐**——量化误差或 stride 错位让模型接收的输入不正确。

排查顺序：

1. 串口看 `Wake word interpreter allocated, arena=...` —— 没看到 → 模型加载失败，看上面的 ERROR
2. 看 `prob=` 输出（在阈值高的地方加临时 INFO log 打印每帧 prob，正常情况安静时 prob ≈ 0-50）—— 如果说话时 prob 始终在低位，几乎可以确认前端不对齐
3. 暂时把 `MICRO_WAKE_WORD_THRESHOLD_X100` 调低到 50 + window 调到 3 看是否触发
4. 跟 ESPHome 那边相同 hey_jarvis 模型对比能不能触发——如果 ESPHome 触发但我们不触发，就是前端问题

### 10.2 编译报 `_binary_hey_jarvis_tflite_start undefined`

**症状**：链接阶段 undefined symbol `_binary_*_start`。
**原因**：`EMBED_FILES` 加了新文件但 Ninja 没重新生成符号（CMake `file(GLOB)` 类似，是 configure 时刻 snapshot）。
**修复**：

```bash
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
| `hey_jarvis.tflite` (v2) | Apache-2.0 | 来自 esphome/micro-wake-word-models，副本在 `models/LICENSE.micro-wake-word-models.txt` |

**没用 ESPHome 的 GPL 代码**——具体地，没拷 `esphome/components/micro_wake_word/{micro_wake_word.cpp, streaming_model.cpp}`（虽然 op resolver 清单参考了它，但代码独立重写）。

## 12. 不在范围内 / 后续工作

- **删除 DSPOTTER 代码**：保留作为回退路径，等 mww 在生产稳定后再做
- **多 wake word 同时启用**：当前每次编译只能选一个后端
- **VAD 模型并跑**：micro-wake-word 仓库提供 `vad.tflite`，可在 wake-word 推理前作为 gating 减少误触；当前 PoC 没启用
- **自训中文"嗨 Jabobo"模型**：走 micro-wake-word 训练管线 + Piper sample generator 中文 voice，单独立项
- **服务端改动**：本集成完全端侧，服务端只接收 `SendWakeWordDetected` JSON 帧（路径已存在），无需改

## 13. 关键文件索引

| 路径 | 作用 |
|---|---|
| `main/idf_component.yml` | 引入 esp-tflite-micro 依赖 |
| `main/Kconfig.projbuild` (L481-507) | `USE_MICRO_WAKE_WORD` 等四个 config |
| `main/CMakeLists.txt` (elseif + MWW_MODEL_FILES + EMBED_FILES) | 编译条件 + 模型嵌入 |
| `main/audio/wake_word.h` | 抽象基类（共用）|
| `main/audio/audio_service.cc` | wake_word 实例化 + Feed 入口 |
| `main/audio/wake_words/micro_wake_word.{h,cc}` | 子类实现 |
| `main/audio/wake_words/micro_features/` | 前端代码 + 数据 |
| `main/audio/wake_words/models/` | 模型文件 + manifest + license |
| `partitions/v1/16m_dspotter_native.csv` | 分区表（共用，未改动）|
