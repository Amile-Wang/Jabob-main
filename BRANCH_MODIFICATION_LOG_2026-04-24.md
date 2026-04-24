# 分支修改日志（2026-04-23 ~ 2026-04-24）

本文档总结本分支在最近两天围绕天豪板双麦改造、主 AFE 处理链调整、唤醒词链路切换、调试日志增强以及构建问题处理所做的改动。

## 1. 目标概述

本轮改动的主目标是：

- 将原单麦 PDM 输入改为双麦 PDM 输入。
- 实现推荐方案：双麦进入 AFE 前处理，主发送仍保持单声道增强语音。
- 将测试链路、部分唤醒词链路和调试链路改成可观察双通道行为。
- 为双麦硬件联调补充原始驱动层日志、应用层统计日志和 AFE 初始化判定日志。
- 处理 `srmodels` 打包目录权限导致的构建失败问题。

## 2. 已落地代码改动

### 2.1 PDM 双麦采集改造

修改文件：

- [main/audio/codecs/no_audio_codec.cc](main/audio/codecs/no_audio_codec.cc)

关键改动：

- `NoAudioCodecSimplexI2sPdm` 的 `input_channels_` 从单通道切到 `2`。
- PDM RX slot 从 `I2S_SLOT_MODE_MONO` 改为 `I2S_SLOT_MODE_STEREO`。
- 显式设置 `pdm_rx_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_BOTH`。
- 保留 I2S 扬声器输出不变，形成 `I2S Speaker + PDM Stereo Mic` 组合。

新增调试能力：

- 启动时打印 `Simplex I2S/PDM stereo input channels created, slot_mask=...`。
- 在 `Read()` 中新增原始偶/奇位统计日志：
  - `Raw PDM stereo diagnostic: even[...] odd[...] samples=...`

用途：

- 用于区分问题出在原始驱动读取阶段，还是出在 AudioService / AFE / 重采样阶段。

### 2.2 主 AFE 处理链切换到推荐方案

修改文件：

- [main/audio/processors/afe_audio_processor.cc](main/audio/processors/afe_audio_processor.cc)

关键改动：

- `afe_config_init(..., NULL, AFE_TYPE_VC, ...)` 改为 `afe_config_init(..., models, AFE_TYPE_SR, ...)`。
- AEC 模式从 `AEC_MODE_VOIP_HIGH_PERF` 改为 `AEC_MODE_SR_HIGH_PERF`。
- 双麦输入时启用 `afe_config->se_init = (codec_->input_channels() > 1)`。
- 如果存在参考通道，则继续强制开启 `aec_init`。
- `GetFeedSize()` 返回值从单通道 `feed_chunksize` 改为 `feed_chunksize * input_channels()`，以匹配当前双通道输入缓冲。

新增日志：

- `AFE init verdict: input_format=..., input_channels=..., se_init=..., aec_init=..., vad_init=..., ns_init=..., models=...`
- 双麦模式下额外提示：
  - `Stereo mic test enabled: observe AudioService stereo test logs...`

效果：

- 主处理链已具备“双麦输入 + AFE SR 前处理 + 单声道增强输出”的推荐方案形态。
- 主发送链和协议握手仍保持单声道，不做激进式双声道上传改造。

### 2.3 AudioService 双通道路由与测试编码支持

修改文件：

- [main/audio/audio_service.cc](main/audio/audio_service.cc)
- [main/audio/audio_service.h](main/audio/audio_service.h)

关键改动：

- 新增 `testing_opus_encoder_`，按 `codec_->input_channels()` 创建测试专用 Opus 编码器。
- 测试链路不再把双通道数据提前降成单通道，改为保留双通道 PCM 进入测试编码队列。
- `OpusCodecTask()` 中对测试任务改用 `testing_opus_encoder_` 编码。
- `AudioInputTask()` 内部的 `testing_data` / `processor_data` / `wake_word_data` 分发更明确，避免双通道路径在测试环节被提前裁掉。

兼容性处理：

- 修正 `ReadAudioData()` 中 `data.size()` 与 `samples` 的类型比较，避免无符号/有符号比较问题。

### 2.4 双麦统计日志增强

修改文件：

- [main/audio/audio_service.cc](main/audio/audio_service.cc)

关键新增：

- 每 50 次输入打印一次总样本调试信息：
  - `Total samples: ..., max value: ..., min value: ...`
- 新增双通道测试日志：
  - `CH0/CH1 avg/peak/min/max/step`
- 对 `CH0` / `CH1` 标签增加 ANSI 蓝色显示，便于串口观察。
- 新增“通道异常/遮挡提示”：
  - `CHx looks stuck or nearly constant`
  - `CHx likely blocked or weaker`

后续修正：

- 最初统计方式直接使用绝对值平均，容易被直流偏置带偏。
- 当前版本已改为“先按通道减去均值，再计算 avg / peak / step”，并额外打印：
  - `mean=%ld`

效果：

- 当前日志更适合用于双麦硬件联调、遮挡测试和偏置判断。

### 2.5 唤醒词链路调整

修改文件：

- [main/audio/wake_words/custom_wake_word.cc](main/audio/wake_words/custom_wake_word.cc)
- [main/audio/wake_words/dspotter_wake_word.cc](main/audio/wake_words/dspotter_wake_word.cc)
- [main/audio/wake_words/afe_wake_word.cc](main/audio/wake_words/afe_wake_word.cc)

`CustomWakeWord` 关键改动：

- 使用 `AFE_TYPE_SR`。
- 关闭 WakeNet：
  - `wakenet_init = false`
  - `wakenet_model_name = nullptr`
  - `wakenet_model_name_2 = nullptr`
- 保留并使用 MultiNet 做命令词检测。
- `Feed()` 增加运行状态保护：仅在检测事件已启动时才真正喂数据。
- 移除运行期显式 `disable_wakenet()` 的旧逻辑，改为初始化阶段直接不启用。

`DSpotterWakeWord` 关键改动：

- `GetFeedSize()` 改为按 `codec_->input_channels()` 计算。
- `EncodeWakeWordData()` 中 Opus 编码器通道数从固定 `1` 改为按当前输入通道创建。

`AfeWakeWord` 修正：

- 修复错误的函数签名污染，恢复正确的 `OnWakeWordDetected(...)`。
- `Feed()` 增加检测状态保护，避免未启动时向 AFE 持续灌数据。

### 2.6 sdkconfig 侧配置切换

修改文件：

- [sdkconfig](sdkconfig)

关键改动：

- 分区表从 `16m_dspotter_native.csv` 切换到 `16m_custom_wakeword.csv`。
- 唤醒词引擎从 `DSpotter` 切换到 `CustomWakeWord`。
- 自定义唤醒词文本设置为：
  - `CONFIG_CUSTOM_WAKE_WORD="ni hao jie bao bao"`
  - `CONFIG_CUSTOM_WAKE_WORD_DISPLAY="ni hao jie bao bao"`
- MultiNet 中文模型从 `CN_NONE` 切换到 `MULTINET6_QUANT`。

效果：

- 当前分支的唤醒链是 `CustomWakeWord + MultiNet6 + AFE_TYPE_SR` 路线。

## 3. 运行期验证与定位结论

### 3.1 双麦链路当前验证结论

已经确认：

- 双麦 stereo PDM 驱动路径已经真正运行。
- 双通道 PCM 已进入 `AudioService`，并可在测试链路中打印 `CH0/CH1`。
- 主 AFE 已按双麦输入模式初始化。

定位过程中得到的关键结论：

- 最初第二路（odd / CH1）为常量槽，这一问题后来被用户确认已从硬件侧修复。
- 当前两路都已出数，但仍存在明显偏置和不对称，需要继续观察“减均值后”的能量判断。

### 3.2 CH0 / CH1 日志所属链路

结论：

- `CH0/CH1` 日志写在 [main/audio/audio_service.cc](main/audio/audio_service.cc) 的 `ReadAudioData()` 中，本质属于通用输入调试日志。
- 但当前用户最常见的触发方式是在 `audio_testing` 状态下，因此现象上常表现为“进入测试链路后开始打印”。

### 3.3 唤醒词当前状态判断

当前分支里：

- 唤醒词检测确实已经切换到 `CustomWakeWord + MultiNet-only`。
- `ni hao jie bao bao` 唤醒不稳定的主要风险不再是状态机没开，而更可能与当前双麦输入质量、偏置和 MultiNet 对输入质量敏感有关。

## 4. 构建问题处理

### 4.1 srmodels 打包目录权限问题

问题现象：

- 构建过程中 `movemodel.py` 在删除 `build/srmodels/fst` 时出现：
  - `PermissionError: [WinError 5] 拒绝访问`

原因：

- `build/srmodels` 目录及其子目录带只读属性，Windows 下 `shutil.rmtree()` 删除失败。

处理方式：

- 手动删除整个 `build/srmodels` 目录后重新构建。

结果：

- 后续 `esp-idf build` 已重新通过。
- `Jabob.bin` 已成功生成。

## 5. 临时验证但未保留为最终分支状态的操作

以下内容用于联调判断，但当前不一定保留在分支最终代码中：

- 对板级 `GPIO4 / LR` 做过高低电平 A/B 验证思路与临时切换尝试。
- 该类改动用于判断单数据线双麦左右槽与物理麦克风的对应关系，不作为当前分支最终功能提交的一部分。

## 6. 当前分支状态总结

当前分支已经完成：

- 双麦底层 stereo 采集改造。
- 主 AFE 切换到推荐方案实现。
- 测试链路双通道编码支持。
- 自定义唤醒词 MultiNet-only 路线切换。
- 多层级调试日志增强。
- `srmodels` 构建阻塞清理。

当前仍需继续验证或完善：

- 双麦两路输入质量是否已经完全正常。
- `ni hao jie bao bao` 唤醒词的稳定触发率。
- 是否需要在输入进入唤醒链或主处理链前增加去直流/归一化等预处理。
