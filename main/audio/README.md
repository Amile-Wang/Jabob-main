# 音频服务架构

音频服务是捷宝宝 AI 聊天机器人项目的核心组件之一，负责管理所有与音频相关的功能，包括从麦克风捕获音频、处理音频、编码/解码以及通过扬声器播放音频。它被设计为模块化和高效，其主要操作在专用的 FreeRTOS 任务中运行，以确保实时性能。

## 🆕 USB音频支持 (2.0.5 Beta)

**重要**: USB音频支持是2.0.5 Beta版本的新特性，目前仅在tianhaoUAC定制硬件平台上完全测试。

- **USB Host UAC**: 支持USB Audio Class 1.0/2.0设备
- **双工通信**: 同时支持USB麦克风输入和USB扬声器输出  
- **自动采样率检测**: 支持48kHz、44.1kHz等常见USB音频采样率
- **直接回调模式**: 优化实时音频采集性能，减少延迟

## 核心组件

- **[AudioService](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L80-L156)**：中央协调器。它初始化和管理所有其他音频组件、任务和数据队列。
- **[AudioCodec](file://z:\jabobo\Jabob-main\main\audio\audio_codec.h#L17-L56)**：物理音频编解码器芯片的硬件抽象层(HAL)。它处理音频输入和输出的原始 I2S 通信。
- **[HybridUsbI2sCodec](file://z:\jabobo\Jabob-main\main\audio\codecs\hybrid_usb_i2s_codec.h#L15-L45)**：🆕 **新增USB/I2S混合编解码器**，支持USB音频设备和传统I2S设备
- **[AudioProcessor](file://z:\jabobo\Jabob-main\main\audio\audio_processor.h#L9-L22)**：对麦克风输入流执行实时音频处理。这通常包括声学回声消除(AEC)、噪声抑制和语音活动检测(VAD)。[AfeAudioProcessor](file://z:\jabobo\Jabob-main\main\audio\processors\afe_audio_processor.h#L15-L42) 是默认实现，利用 ESP-ADF 音频前端。
- **[WakeWord](file://z:\jabobo\Jabob-main\main\audio\wake_word.h#L9-L22)**：从音频流中检测关键词(例如"你好，小智"，"Hi, ESP")。它独立于主音频处理器运行，直到检测到唤醒词。
- **`OpusEncoderWrapper` / `OpusDecoderWrapper`**：管理将 PCM 音频编码为 Opus 格式以及将 Opus 数据包解码回 PCM。Opus 因其高压缩率和低延迟而被选用，非常适合语音流传输。
- **`OpusResampler`**：实用工具，用于在不同的采样率之间转换音频流(例如，将编解码器的原生采样率重新采样到处理所需的 16kHz)。

## 线程模型

该服务在三个主要任务上运行，以并发处理音频管道的不同阶段：

1. **[AudioInputTask](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L150-L150)**：专门负责从 [AudioCodec](file://z:\jabobo\Jabob-main\main\audio\audio_codec.h#L17-L56) 读取原始 PCM 数据。然后根据当前状态将这些数据馈送到 [WakeWord](file://z:\jabobo\Jabob-main\main\audio\wake_word.h#L9-L22) 引擎或 [AudioProcessor](file://z:\jabobo\Jabob-main\main\audio\audio_processor.h#L9-L22)。
2. **[AudioOutputTask](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L151-L151)**：负责播放音频。它从 [audio_playback_queue_](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L134-L134) 检索解码的 PCM 数据并将其发送到 [AudioCodec](file://z:\jabobo\Jabob-main\main\audio\audio_codec.h#L17-L56) 以通过扬声器播放。
3. **[OpusCodecTask](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L152-L152)**：工作线程任务，负责处理编码和解码。它从 [audio_encode_queue_](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L133-L133) 获取原始音频，将其编码为 Opus 数据包，并放入 [audio_send_queue_](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L131-L131)。同时，它从 [audio_decode_queue_](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L130-L130) 获取 Opus 数据包，将其解码为 PCM，并将结果放入 [audio_playback_queue_](file://z:\jabobo\Jabob-main\main\audio\audio_service.h#L134-L134)。

## 数据流向

有两种主要的数据流向：音频输入(上行)和音频输出(下行)。

### 1. 音频输入(上行)流向

此流向从麦克风捕获音频，处理音频，编码音频，并准备将其发送到服务器。

``mermaid
graph TD
    subgraph 设备
        Mic[("麦克风")] -->|I2S/USB| Codec(音频编解码器)
        
        subgraph 音频输入任务
            Codec -->|原始PCM| Read(读取音频数据)
            Read -->|16kHz PCM| Processor(音频处理器)
        end

        subgraph Opus编解码任务
            Processor -->|清理后的PCM| EncodeQueue(编码队列)
            EncodeQueue --> Encoder(Opus编码器)
            Encoder -->|Opus数据包| SendQueue(发送队列)
        end

        SendQueue --> |"PopPacketFromSendQueue()"| App(应用层)
    end
    
    App -->|网络| Server((云端服务器))

    2. 音频输出(下行)流向
此流向接收来自服务器的音频，解码音频，并通过扬声器播放。

mermaid
graph TD
    subgraph 云端服务器
        Server((云端服务器)) -->|网络| App(应用层)
    end

    App --> |"PushPacketToDecodeQueue()"| SendQueue(解码队列)
    
    subgraph Opus编解码任务
        SendQueue --> Decoder(Opus解码器)
        Decoder -->|PCM数据| PlaybackQueue(播放队列)
    end

    subgraph 音频输出任务
        PlaybackQueue --> Player(播放器)
        Player -->|PCM数据| Codec(音频编解码器)
        Codec -->|I2S| Speaker(("扬声器"))
    end
音频处理流程
音频处理遵循以下状态转换:

mermaid
stateDiagram-v2
    [*] --> Standby: 初始化
    Standby --> WakeWordDetection: 设备就绪
    WakeWordDetection --> AudioProcessing: 检测到唤醒词
    AudioProcessing --> WakeWordDetection: 音频处理结束
    WakeWordDetection --> Standby: 进入待机
    AudioProcessing --> Standby: 进入待机
配置选项
音频服务支持多种配置选项，可在项目配置中启用或禁用:

CONFIG_USE_AUDIO_PROCESSOR：启用音频处理功能（如AEC、VAD等）
CONFIG_USE_AFE_WAKE_WORD：使用 AFE 唤醒词引擎
CONFIG_USE_ESP_WAKE_WORD：使用 ESP 唤醒词引擎
CONFIG_USE_CUSTOM_WAKE_WORD：使用自定义唤醒词引擎
子目录结构
codecs/：包含不同音频编解码器的实现
processors/：包含音频处理器实现（如 AEC、VAD 等）
wake_words/：包含唤醒词检测引擎实现
性能优化
使用 Opus 编解码器以获得高压缩率和低延迟
采用多任务架构以实现并发处理
音频缓冲区大小经过优化以平衡延迟和内存使用
通过事件组同步不同任务间的音频状态