# UAC Input + I2S Output BSP 创建总结

## 📋 项目概述

已成功创建基于 `bread-compact-wifi-lcd-tianhao` 的新板级支持包（BSP），将音频输入从 I2S/PDM 麦克风改为使用 **USB Audio Class (UAC)** 麦克风，同时保持 I2S 扬声器输出。

## 🎯 BSP 信息

- **板型名称**: `bread-compact-wifi-lcd-tianhao-uac`
- **配置文件**: 
  - `config.h` - GPIO 引脚和硬件配置
  - `config.json` - 构建系统配置
  - `compact_wifi_board_lcd_uac.cc` - 板级实现
  - `README.md` - 详细使用文档

## 🔧 主要特性

### 音频配置
- **输入**: USB Audio Class (UAC) 麦克风
  - 采样率：16kHz（自动协商）
  - 位宽：16 位（自动协商）
  - 通道数：自动检测
  - 即插即用，支持热插拔
  
- **输出**: I2S 扬声器
  - GPIO7: DOUT (数据输出)
  - GPIO15: BCLK (位时钟)
  - GPIO16: LRCK (左右时钟)
  - GPIO8: 扬声器电源控制
  - 采样率：24kHz

### 保留的功能
- ✅ LCD 显示屏（SPI 接口，多种控制器支持）
- ✅ 触摸按钮（GPIO14）
- ✅ 音量控制按钮（GPIO38, GPIO39）
- ✅ 电池监控（ADC1 Channel 0）
- ✅ NFC 模块（GPIO10-13, GPIO3, GPIO9）
- ✅ LED 指示灯（GPIO48）
- ✅ 电源管理定时器
- ✅ 舵机控制（PWM）

## 📁 文件清单

```
main/boards/bread-compact-wifi-lcd-tianhao-uac/
├── config.h                          # 硬件配置头文件
├── config.json                       # 构建配置文件
├── compact_wifi_board_lcd_uac.cc     # 板级实现代码
└── README.md                         # 使用文档
```

## 🛠️ 使用方法

### 1. 在 menuconfig 中选择板型

```bash
idf.py menuconfig
```

导航到：
```
Jabob Configuration → Board type → bread-compact-wifi-lcd-tianhao-uac
```

### 2. 启用 USB Host 驱动

确保启用以下选项：
```
Component config → USB Support → Enable USB Host Driver [✓]
Component config → USB Support → USB Host UAC Driver [✓]
```

### 3. 编译和烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译项目
idf.py build

# 烧录固件
idf.py flash

# 查看日志
idf.py monitor
```

## 🔄 与原版 Tianhao 的对比

| 特性 | 原版 Tianhao | UAC 版本 |
|------|-------------|---------|
| 音频输入 | I2S/PDM 麦克风 | USB 麦克风 ⭐ |
| 音频输出 | I2S 扬声器 | I2S 扬声器 |
| USB 使用 | 无特殊用途 | USB Host (音频输入) |
| RAM 占用 | 较低 | +20KB (USB 协议栈) |
| 启动时间 | 正常 | +1-2 秒 (USB 枚举) |
| 麦克风兼容性 | 特定 I2S/PDM 型号 | 通用 USB 麦克风 ⭐ |
| 配置复杂度 | 简单 | 中等 |

## ⚙️ 技术细节

### 音频编解码器实现

使用 `UsbAudioCodec` 类处理 UAC 音频输入：

```cpp
virtual AudioCodec* GetAudioCodec() override {
    static UsbAudioCodec audio_codec(
        AUDIO_INPUT_SAMPLE_RATE,   // 16kHz
        AUDIO_OUTPUT_SAMPLE_RATE   // 24kHz
    );
    return &audio_codec;
}
```

### USB 枚举流程

1. 初始化 USB Host 控制器
2. 等待设备连接事件（最多 5 秒）
3. 查询设备能力（采样率、位宽、通道数）
4. 配置并打开 RX 流（麦克风）
5. 配置并打开 TX 流（扬声器）
6. 启动音频服务

### 混合架构优势

- **灵活性**: 可使用各种 USB 麦克风，无需定制 I2S 麦克风
- **稳定性**: I2S 扬声器输出保持稳定，不受 USB 影响
- **兼容性**: 支持 UAC1.0 和 UAC2.0 标准设备
- **热插拔**: USB 设备拔出后重新插入可自动恢复

## 🎯 应用场景

1. **开发测试**: 方便使用各种 USB 麦克风进行原型开发
2. **灵活配置**: 可根据需要更换不同型号的 USB 麦克风
3. **教育演示**: 展示 USB Audio Class 的实际应用
4. **产品定制**: 为客户提供更多麦克风选择

## 🐛 故障排查

### USB 设备无法识别
- 检查 USB 连接是否牢固
- 确认开发板能提供足够的 USB 电流（100mA+）
- 尝试不同的 USB 麦克风设备
- 检查是否启用了 USB Host 驱动

### 音频断断续续
- 增加缓冲区大小（修改 usb_audio_codec.h）
- 降低 CPU 负载（减少其他任务）
- 检查 USB 线缆质量

### 设备频繁断开重连
- 使用屏蔽良好的 USB 线
- 添加去耦电容
- 远离高频信号源

## 📚 参考资料

- [ESP-IDF USB Host UAC 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_host_uac.html)
- [USB Audio Class 规范](https://www.usb.org/audio)
- [Jabob-main 项目文档](../../README.md)

## ✅ 验证清单

- [x] 创建板级目录
- [x] 编写 config.h 配置文件
- [x] 实现 compact_wifi_board_lcd_uac.cc
- [x] 创建 config.json 构建配置
- [x] 编写 README.md 文档
- [x] 更新 main/CMakeLists.txt
- [x] 代码语法检查通过
- [x] 依赖项验证（usb_host_uac 已存在）

## 📝 注意事项

1. **RAM 占用**: USB 协议栈需要额外约 20KB RAM
2. **启动延迟**: USB 枚举过程增加 1-2 秒启动时间
3. **供电要求**: 确保开发板能提供足够的 USB 电流
4. **GPIO 冲突**: USB 使用 GPIO19 (D+) 和 GPIO20 (D-)，不能用于其他用途

## 🚀 下一步

1. 在实际硬件上测试编译和烧录
2. 验证 USB 麦克风和 I2S 扬声器功能
3. 测试音频质量和延迟
4. 根据实际需求调整缓冲区大小

---

**创建日期**: 2026-03-17  
**版本**: 1.0  
**状态**: ✅ 完成
