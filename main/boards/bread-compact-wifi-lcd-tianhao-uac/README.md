# Bread Compact WiFi LCD Tianhao UAC Board Support Package

## 概述

这是一个基于 `bread-compact-wifi-lcd-tianhao` 的板级支持包（BSP），主要特点是将音频输入改为使用 **USB Audio Class (UAC)** 麦克风，音频输出保持 **I2S** 扬声器。

## 硬件配置

### 音频配置
- **输入**: USB Audio Class (UAC) 麦克风
  - 采样率：16kHz（自动协商）
  - 位宽：16 位（自动协商）
  - 通道数：自动检测（单声道/立体声）
  - 即插即用，支持热插拔
  
- **输出**: I2S 扬声器
  - GPIO7: DOUT (数据输出)
  - GPIO15: BCLK (位时钟)
  - GPIO16: LRCK (左右时钟)
  - GPIO8: 扬声器电源控制
  - 采样率：24kHz

### LCD 显示屏
- SPI 接口 LCD 显示屏
- 支持多种 LCD 控制器（ST7789, ILI9341, GC9A01 等）
- 通过 menuconfig 选择具体型号

### 其他外设
- **电池检测**: ADC1 Channel 0
- **按钮**:
  - BOOT: GPIO0
  - TOUCH: GPIO14 (阈值：1500)
  - VOLUME+: GPIO38
  - VOLUME-: GPIO39
- **LED**: GPIO48
- **NFC**: 
  - SDA: GPIO10
  - SCK: GPIO11
  - MOSI: GPIO12
  - MISO: GPIO13
  - IRQ: GPIO3
  - RST: GPIO9

## 使用方法

### 1. 在 menuconfig 中选择板型

```bash
idf.py menuconfig
```

导航到：
```
Jabob Configuration → Board type → bread-compact-wifi-lcd-tianhao-uac
```

### 2. 启用 USB Host 驱动

确保在 menuconfig 中启用以下选项：
```
Component config → USB Support → Enable USB Host Driver [✓]
Component config → USB Support → USB Host UAC Driver [✓]
```

### 3. 编译和烧录

```bash
idf.py build flash monitor
```

## 特性

### UAC 音频输入优势
1. **即插即用**: 自动检测和配置 USB 麦克风设备
2. **广泛兼容**: 支持 UAC1.0 和 UAC2.0 标准设备
3. **热插拔**: 设备拔出后重新插入可自动恢复
4. **零配置**: 自动枚举采样率、位宽、通道数等参数

### I2S 音频输出
1. **稳定可靠**: 直接 GPIO 控制，无需额外协议栈
2. **低延迟**: 适合实时音频播放
3. **简单配置**: 固定引脚，易于调试

### 混合架构注意事项
- 输入和输出采样率可以不同（16kHz in / 24kHz out）
- AudioService 会自动处理采样率转换
- 需要额外的 RAM 用于 USB 协议栈（约 20KB）

## 已知兼容的 USB 麦克风

- ✅ C-Media Electronics Inc. USB Audio Device
- ✅ Logitech H390 USB 耳机
- ✅ Generic USB Microphone (UAC1.0)
- ✅ Generic USB Speaker (UAC1.0)

## 故障排查

### USB 设备无法识别
1. 检查 USB 连接是否牢固
2. 确认开发板能提供足够的 USB 电流（100mA+）
3. 尝试不同的 USB 麦克风设备
4. 检查是否启用了 USB Host 驱动

### 音频断断续续
1. 增加缓冲区大小（修改 usb_audio_codec.h）
2. 降低 CPU 负载（减少其他任务）
3. 检查 USB 线缆质量

### 设备频繁断开重连
1. 使用屏蔽良好的 USB 线
2. 添加去耦电容
3. 远离高频信号源

## 日志示例

正常启动日志：
```
I UsbAudioCodec: UsbAudioCodec created - Input: 16000Hz, Output: 24000Hz
I UsbAudioCodec: Starting USB Audio codec...
I UsbAudioCodec: Initializing USB Host...
I UsbAudioCodec: USB Host initialized
I UsbAudioCodec: Waiting for USB audio device...
I UsbAudioCodec: USB Audio device connected
I UsbAudioCodec: Device Info:
I UsbAudioCodec:   Manufacturer: C-Media Electronics Inc.
I UsbAudioCodec:   Product: USB Audio Device
I UsbAudioCodec:   RX channels: 1
I UsbAudioCodec:   TX channels: 2
I UsbAudioCodec: RX stream opened successfully - Channels: 1, Sample Rate: 16000Hz
I UsbAudioCodec: TX stream opened successfully - Channels: 2, Sample Rate: 24000Hz
```

## 与原版 Tianhao 的区别

| 特性 | 原版 Tianhao | UAC 版本 |
|------|-------------|---------|
| 音频输入 | I2S/PDM 麦克风 | USB 麦克风 |
| 音频输出 | I2S 扬声器 | I2S 扬声器 |
| USB 使用 | 无特殊用途 | USB Host (音频输入) |
| RAM 占用 | 较低 | +20KB (USB 协议栈) |
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
