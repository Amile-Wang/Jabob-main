# Bread Compact WiFi LCD Tianhao UAC Board

## 📋 概述

此板级配置专为 **Bread Compact WiFi LCD Tianhao** 开发板设计，增加了 **USB Audio Class (UAC)** 麦克风支持。

**重要变更**: 当前实现仅支持 USB 麦克风输入，**不支持 USB 扬声器输出**。音频输出将通过其他方式（如 I2S）处理。

## 🎯 主要特性

- ✅ **WiFi 连接** - 支持 2.4GHz WiFi 网络
- ✅ **LCD 显示** - 240x240 像素彩色显示屏
- ✅ **USB 麦克风** - 支持 UAC1.0/UAC2.0 标准 USB 麦克风
- ❌ **USB 扬声器** - **已移除，不再支持**
- ✅ **按钮控制** - 包含 Boot、触摸、音量控制按钮
- ✅ **电源管理** - 支持电池监控和节能模式
- ✅ **舵机控制** - 支持 PWM 舵机

## 🔧 硬件配置

### GPIO 分配

| 功能 | GPIO | 说明 |
|------|------|------|
| USB D+ | 19 | USB Host 数据正 |
| USB D- | 20 | USB Host 数据负 |
| LCD CS | 5 | LCD 片选 |
| LCD DC | 6 | LCD 数据/命令选择 |
| LCD CLK | 7 | LCD 时钟 |
| LCD MOSI | 8 | LCD 数据输入 |
| LCD RST | 9 | LCD 复位 |
| BOOT 按钮 | 0 | 启动按钮 |
| 触摸按钮 | 4 | 触摸感应按钮 |
| 音量+ | 10 | 音量增加 |
| 音量- | 11 | 音量减少 |
| 扬声器电源 | 12 | 扬声器电源控制 |
| 电池充电检测 | 17 | 电池充电状态检测 |

### USB 麦克风要求

1. **标准 UAC 设备**: 必须是符合 USB Audio Class 1.0 或 2.0 标准的设备
2. **供电能力**: 开发板需能提供至少 100mA USB 电流
3. **采样率支持**: 推荐使用 16kHz 采样率的麦克风

## 💻 软件配置

### 编译选项

在 `idf.py menuconfig` 中确保启用：

```
Component config → USB Support → Enable USB Host Driver [✓]
Component config → USB Support → USB Host UAC Driver [✓]
```

### 音频编解码器

使用修改后的 `UsbAudioCodec` 类：

```cpp
// 仅启用麦克风输入，禁用 USB 扬声器输出
static UsbAudioCodec* audio_codec = nullptr;
if (audio_codec == nullptr) {
    audio_codec = new UsbAudioCodec(AUDIO_INPUT_SAMPLE_RATE, 0);
}
return audio_codec;
```

### 采样率配置

- **输入采样率**: 16000 Hz (可配置)
- **输出采样率**: 通过其他音频输出方式处理（非 USB）

## 🚀 使用方法

### 1. 硬件连接

1. 将 USB 麦克风插入开发板的 USB Host 接口
2. 确保开发板有足够电源（推荐使用外部电源适配器）
3. 连接其他外设（LCD、按钮等）

### 2. 编译烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### 3. 运行验证

正常启动日志应包含：

```
I UsbAudioCodec: UsbAudioCodec created - Input: 16000Hz, Output: disabled
I UsbAudioCodec: Starting USB Audio codec...
I UsbAudioCodec: USB Audio device connected
I UsbAudioCodec: RX stream opened successfully - Channels: 1, Sample Rate: 16000Hz
I UsbAudioCodec: USB Audio codec started successfully (microphone only)
```

## ⚠️ 注意事项

### USB Host 限制

1. **GPIO 冲突**: USB 使用 GPIO19/20，不能用于其他用途
2. **仅 ESP32-S3**: 其他 ESP32 系列芯片不支持 USB Host
3. **供电要求**: 确保 USB 设备获得足够电流

### 麦克风兼容性

#### 已知兼容的设备
- ✅ Logitech H390 USB 耳机（仅麦克风部分）
- ✅ Generic USB Microphone (C-Media)
- ✅ USB 麦克风（Generic UAC1.0）

#### 可能不兼容的设备
- ❌ 需要特殊驱动的 USB 设备
- ❌ USB 3.0 高速设备
- ❌ 复合设备（同时包含音频和其他功能）

## 🐛 故障排查

### USB 设备无法识别

**症状**: 日志显示 "No USB audio device found"

**解决方法**:
1. 检查 USB 连接是否牢固
2. 确认开发板能提供足够的 USB 电流（100mA+）
3. 尝试不同的 USB 麦克风设备
4. 检查是否启用了 USB Host 驱动

### 音频断断续续

**症状**: 录音有爆音、中断

**可能原因**:
- CPU 负载过高
- 缓冲区大小不足
- USB 线缆质量差

**解决方法**:
1. 增加缓冲区大小（修改 usb_audio_codec.h）
2. 降低 CPU 负载（减少其他任务）
3. 使用屏蔽良好的 USB 线

### 设备频繁断开重连

**症状**: 日志反复显示 connected/disconnected

**可能原因**:
- USB 线缆质量差
- 电源不稳定
- EMI 干扰

**解决方法**:
1. 使用屏蔽良好的 USB 线
2. 添加去耦电容
3. 远离高频信号源

## 📊 日志示例

正常启动日志：
```
I UsbAudioCodec: UsbAudioCodec created - Input: 16000Hz, Output: disabled
I UsbAudioCodec: Starting USB Audio codec...
I UsbAudioCodec: Initializing USB Host...
I UsbAudioCodec: USB Host initialized
I UsbAudioCodec: Waiting for USB audio device...
I UsbAudioCodec: USB Audio device connected
I UsbAudioCodec: Device Info:
I UsbAudioCodec:   Manufacturer: C-Media Electronics Inc.
I UsbAudioCodec:   Product: USB Audio Device
I UsbAudioCodec:   RX channels: 1
I UsbAudioCodec: RX stream opened successfully - Channels: 1, Sample Rate: 16000Hz
I UsbAudioCodec: USB Audio codec started successfully (microphone only)
```

## 与原版 Tianhao 的区别

| 特性 | 原版 Tianhao | UAC 版本 |
|------|-------------|---------|
| 音频输入 | I2S/PDM 麦克风 | USB 麦克风 |
| 音频输出 | I2S 扬声器 | I2S 扬声器 |
| USB 使用 | 无特殊用途 | USB Host (音频输入) |
| RAM 占用 | 较低 | +15KB (USB 协议栈) |
| 启动时间 | 正常 | +1-2 秒 (USB 枚举) |
| 麦克风兼容性 | 特定 I2S/PDM 型号 | 通用 USB 麦克风 |

## 应用场景

1. **开发测试**: 方便使用各种 USB 麦克风进行原型开发
2. **灵活配置**: 可根据需要更换不同型号的 USB 麦克风
3. **教育演示**: 展示 USB Audio Class 的实际应用
4. **产品定制**: 为客户提供更多麦克风选择

## 参考资料

- [ESP-IDF USB Host UAC 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_host_uac.html)
- [USB Audio Class 规范](https://www.usb.org/audio)
- [Jabob-main 项目文档](../../../README.md)

## 许可证

Apache-2.0 License
