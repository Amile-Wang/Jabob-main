# Jabob AI Chatbot Project Context

## Project Overview

**项目名称**: 捷宝宝 (Jabob) AI 聊天机器人
**平台**: ESP32S3嵌入式设备
**当前版本分支**: 2.0.5
**主控芯片**: ESP32S3双核处理器
**推荐开发板**: bread-compact-wifi-lcd-tianhao (定制版)

## Core Functionality Modules

### Audio System (main/audio)
- Support for multiple audio codecs (including USB audio)
- Opus codec (high compression, low latency)
- Real-time audio processing (AEC echo cancellation, VAD voice activity detection)
- Wake word detection (AFE WakeNet, ESP WakeNet)
- Dual audio stream processing (microphone input + speaker output)
- Three-task concurrent architecture (AudioInputTask, AudioOutputTask, OpusCodecTask)

### Display System (main/display)
- LVGL graphics library support
- Multiple LCD controllers (ST7789, GC9A01, ILI9341, etc.)
- GIF animation and emoji display
- Touch screen support
- Network status, battery, mute and other status indicators

### Communication Protocols (main/protocols)
- MQTT protocol (with QoS levels)
- WebSocket protocol (real-time bidirectional communication)
- Unified protocol abstraction interface
- Automatic reconnection and heartbeat mechanism

### Hardware Abstraction Layer (main/boards)
- Multi-platform support (ESP32, ESP32S3, ESP32C3, etc.)
- Unified hardware interface abstraction
- Supported peripherals: Audio Codec, Display, LED, RFID/NFC, etc.

### System Services
- OTA over-the-air updates
- Device state machine management
- Configuration persistent storage
- MCP internal message protocol

## Technology Stack

- **Development Environment**: ESP-IDF v5.1+
- **Programming Language**: C++
- **Real-time OS**: FreeRTOS
- **Key Components**:
  - esp-sr (speech recognition)
  - esp-opus (audio codec)
  - esp_lvgl_port (LVGL porting)
  - esp-wifi-connect (WiFi management)

## Supported Hardware Platforms

- bread-compact-esp32 (basic ESP32)
- bread-compact-esp32-lcd (with LCD display)
- bread-compact-ml307 (with 4G module support)
- bread-compact-wifi (WiFi version)
- bread-compact-wifi-lcd (WiFi + LCD)
- bread-compact-wifi-lcd-tianhao (Jabob custom version)
- bread-compact-wifi-s3cam (S3 version with camera)

## Recent Development Focus

Current branch (2.0.5) main work:
- USB audio codec support and improvements
- Sample rate processing optimization
- Buffer management improvements
- Microphone dedicated mode implementation
- Duplex communication support

## Project Characteristics

1. **Low Power and High Performance**: Based on ESP32S3 dual-core, optimized power management
2. **Multi-protocol Support**: MQTT and WebSocket unified interface
3. **Voice Interaction**: Integrated audio codec and wake word detection
4. **Visual Interface**: LVGL modern graphics interface with rich UI elements
5. **Modular Design**: Clear code structure, easy for secondary development and porting

## Build and Development

- Build scripts available in project root (various .ps1 and .bat files)
- ESP-IDF build system
- Multi-configuration support via sdkconfig
- OTA update capability

## Recent Commits

- feat(audio): 改进音频服务的采样率处理和缓冲区管理
- feat(usb_audio): 实现USB音频编解码器的麦克风专用模式
- feat(audio): 改进USB音频编解码器实现支持双工通信
- feat(usb-audio): 完善USB音频编解码器功能和设备信息显示
- feat(audio): 添加USB音频编解码器支持并更新配置选项
