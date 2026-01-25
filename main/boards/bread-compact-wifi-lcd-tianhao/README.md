# 捷宝宝面包板紧凑型WiFi LCD开发板 BSP

## 概述

`bread-compact-wifi-lcd-tianhao` 是捷宝宝 AI 聊天机器人项目的一个特殊开发板配置，专门为捷宝宝定制版本设计。该开发板基于ESP32-S3，具有WiFi连接能力、LCD显示屏和音频功能，并特别增加了电源管理和触摸控制功能。

## 特殊功能

### 1. 电源管理增强
此开发板实现了一个特殊的电源管理功能，用于控制扬声器电源：
- 通过 [SPK_GPIO_POWERSAVE](file://z:\jabobo\Jabob-main\main\boards\bread-compact-wifi-lcd-tianhao\config.h#L50-L51) 引脚控制扬声器电源
- 防止引脚悬空导致扬声器电源被意外切断
- 在系统初始化时将电源控制引脚设置为高电平，确保扬声器正常供电

### 2. 触摸控制
- 集成了触摸监控功能，支持触摸按键交互
- 可通过 [TOUCH_BUTTON_GPIO](file://z:\jabobo\Jabob-main\main\boards\bread-compact-wifi-lcd-tianhao\config.h#L70-L71) 引脚实现触摸感应

### 3. 舵机控制
- 集成PWM舵机控制功能
- 通过 [pwm_servo](file://z:\jabobo\Jabob-main\main\pwm\pwm_servo.h#L17-L34) 实现舵机控制

### 4. 电池监测
- 支持电池电量检测
- 通过ADC引脚监测电池电压
- 支持充电状态检测

## 硬件特性

### 音频配置
- **麦克风配置**：支持多种I2S音频模式
  - `AUDIO_I2S_METHOD_SIMPLEX_I2S_PDM`（默认）
  - `AUDIO_I2S_METHOD_SIMPLEX`
  - `AUDIO_I2S_METHOD_SIMPLEX_PDM`
- **采样率**：
  - 输入采样率：16000Hz
  - 输出采样率：24000Hz
- **GPIO分配**：
  - PDM麦克风SCK：GPIO 5
  - PDM麦克风DIN：GPIO 6
  - PDM麦克风LR：GPIO 4
  - 扬声器输出：GPIO 7
  - 扬声器BCLK：GPIO 15
  - 扬声器LRCK：GPIO 16
  - 扬声器电源控制：GPIO 8

### 显示配置
- **显示屏类型**：SPI接口LCD显示屏
  - ST7789系列（支持多种分辨率）
- **GPIO分配**：
  - 背光控制：GPIO 42
  - MOSI：GPIO 47
  - 时钟：GPIO 21
  - DC：GPIO 40
  - 复位：GPIO 45
  - CS：GPIO 41
- **分辨率支持**：
  - 240×320
  - 170×320
  - 172×320
  - 240×240

### 其他外设
- **LED**：GPIO 48
- **启动按钮**：GPIO 0
- **触摸按钮**：GPIO 14（可选）
- **音量调节**：GPIO 38（上）和 GPIO 39（下）
- **NFC模块**：支持NFC功能
  - SDA：GPIO 10
  - SCK：GPIO 11
  - MOSI：GPIO 12
  - MISO：GPIO 13
  - IRQ：GPIO 12
  - RST：GPIO 14

## 与其它BSP的主要区别

### 1. 电源管理机制
与标准的 `bread-compact-wifi-lcd` 开发板相比，此版本特别增加了扬声器电源管理机制，这是其最显著的特点：
- 标准版本：直接连接音频硬件，没有额外的电源控制
- 捷宝宝版本：通过GPIO控制扬声器电源开关，防止悬空状态下电源异常

### 2. 音频配置差异
- 标准版本可能使用不同的I2S模式配置
- 捷宝宝版本默认使用 `SIMPLEX_I2S_PDM` 模式，专门适配特定的音频硬件

### 3. 触摸功能
- 标准版本：触摸功能可能被禁用或配置为NC（不连接）
- 捷宝宝版本：明确启用了触摸功能，提供了触摸监控实现

### 4. 舵机控制
- 捷宝宝版本特别集成了舵机控制功能，这在其他版本中可能不存在

### 5. 电池监测
- 捷宝宝版本提供了完整的电池监测功能，包括ADC采样和充电状态检测

## 配置选项

### 音频模式
- `AUDIO_I2S_METHOD_SIMPLEX`：简单I2S模式
- `AUDIO_I2S_METHOD_SIMPLEX_PDM`：PDM麦克风模式
- `AUDIO_I2S_METHOD_SIMPLEX_I2S_PDM`：I2S+PDM混合模式（默认）

### 显示配置
- `CONFIG_LCD_ST7789_240X320`：240×320分辨率
- `CONFIG_LCD_ST7789_240X320_NO_IPS`：非IPS屏幕选项
- `CONFIG_LCD_ST7789_170X320`：170×320分辨率
- `CONFIG_LCD_ST7789_172X320`：172×320分辨率

