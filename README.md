# 捷宝宝 AI 聊天机器人 - ESP32S3 设备端项目

**捷宝宝**（Jabob）是一个基于ESP32S3平台的AI聊天机器人设备端开源项目，致力于提供一个轻量级、高性能的智能对话解决方案。

## 🚨 当前版本状态

**当前分支**: `2.0.5` (Beta版本)  
**⚠️ 重要提示**: 这是一个 **Beta版本**，主要实现新功能但 **不保证其他原有功能的稳定性**。  
**✅ 稳定版本**: 正式稳定版本将是 **2.0.6版本**，请在生产环境中使用稳定版本。

**[📖 2.0.5 Beta版本详细发布说明](docs/v2.0.5-release-notes.md)**

## 项目概述

捷宝宝（Jabob）是一个专为嵌入式设备设计的AI聊天机器人，采用ESP32S3作为主控芯片，集成了音频处理、语音识别、显示交互等功能，旨在为用户提供一个离在线结合的智能对话体验。

## 硬件平台

- **主控芯片**: ESP32S3
- **推荐开发板**: Kris 定制 JBB board 2代
- **推荐bsp**:  bread-compact-wifi-lcd-tianhao
- **外设支持**: 音频Codec、显示屏、LED指示灯、RFID等
- **🆕 新增支持**: USB音频设备（麦克风/扬声器）

## 项目结构
Jabob/ 
├── components/ # 第三方组件 
│ ├── 78__xiaozhi-fonts/ # 字体资源组件 
│ └── lvgl__lvgl/ # LVGL图形库组件 
├── docs/ # 项目文档 
│ └── v2.0.5-release-notes.md # 2.0.5 Beta版本发布说明
├── esp-nn-master/ # ESP神经网络加速组件 
├── esp-sr-master/ # ESP语音识别组件 
├── main/ # 主应用程序 
│ ├── assets/ # 多语言资源包 
│ ├── audio/ # 音频服务系统 
│ ├── boards/ # 硬件抽象层（BSP） 
│ ├── display/ # 显示系统 
│ ├── led/ # LED控制模块 
│ ├── protocols/ # 通信协议实现 
│ ├── pwm/ # PWM控制模块 
│ ├── rfid/ # RFID/NFC模块 
│ ├── application.cc/h # 应用主控制器 
│ ├── device_state.h # 设备状态定义 
│ ├── device_state_event.cc/h # 设备状态事件 
│ ├── mcp_server.cc/h # MCP服务器 
│ ├── ota.cc/h # OTA升级模块 
│ ├── settings.cc/h # 系统设置 
│ ├── system_info.cc/h # 系统信息 
│ └── main.cc # 程序入口 
├── managed_components/ # 管理的组件依赖 
├── reference/ # 参考资料 
├── scripts/ # 工具脚本 
├── README.md # 项目说明 
└── logclient.html # 日志客户端


## 核心功能模块

### 音频系统 ([main/audio](file://z:\jabobo\Jabob-main\main\audio\README.md#L1-L121))
- **音频编解码**: 支持多种音频格式和采样率
- **USB音频支持**: 🆕 **新增USB麦克风和扬声器支持**
- **音频处理**: 实时音频处理，包括AEC（回声消除）、VAD（语音活动检测）
- **唤醒词检测**: 支持多种唤醒词引擎（AFE WakeNet、ESP WakeNet）
- **Opus编解码**: 高效音频压缩和传输（标准60ms帧长）
- **音频流处理**: 双向音频流处理（输入和输出）

### 显示系统 ([main/display](file://z:\jabobo\Jabob-main\main\display\README.md#L1-L77))
- **LCD显示**: 支持多种LCD控制器（ST7789、GC9A01、ILI9341等）
- **LVGL图形界面**: 现代化图形用户界面
- **动画支持**: GIF动画和表情符号显示
- **触摸输入**: 支持多种触摸屏控制器
- **状态管理**: 网络、电池、静音等状态显示

### 通信协议 ([main/protocols](file://z:\jabobo\Jabob-main\main\protocols\README.md#L1-L84))
- **MQTT协议**: 支持QoS级别的可靠消息传输
- **WebSocket协议**: 实时双向通信
- **统一接口**: 抽象协议差异，支持协议切换
- **连接管理**: 自动重连和心跳机制

### 硬件抽象 ([main/boards](file://z:\jabobo\Jabob-main\main\boards\README.md#L1-L104))
- **多平台支持**: 支持多种ESP32开发板配置
- **硬件适配**: 统一的硬件接口抽象
- **定制化支持**: 如捷宝宝定制版的特殊电源管理

### 系统服务
- **OTA升级**: 支持空中固件更新
- **状态管理**: 设备状态机管理
- **配置管理**: 持久化配置存储
- **MCP服务器**: 内部消息通信协议

## 主要特性

### 低功耗高性能
- 基于ESP32S3双核处理器，提供足够的算力同时保持低功耗
- 电源管理优化，支持省电模式

### 多协议支持
- 支持MQTT、WebSocket等多种通信协议
- 统一的协议接口，便于扩展

### 语音交互
- 集成音频编解码和唤醒词检测功能
- 支持多种语音识别引擎
- 实时音频处理

### 可视化界面
- 配合LVGL图形库提供直观的用户界面
- 支持多种显示屏类型
- 丰富的UI元素和动画效果

### 模块化设计
- 代码结构清晰，便于二次开发和定制
- 硬件抽象层，支持多平台移植

## 支持的硬件平台

### 面包板紧凑型系列
- **bread-compact-esp32**: 基础ESP32版本
- **bread-compact-esp32-lcd**: 带LCD显示的ESP32版本
- **bread-compact-ml307**: 支持ML307 4G模块
- **bread-compact-wifi**: WiFi连接版本
- **bread-compact-wifi-lcd**: WiFi+LCD版本
- **bread-compact-wifi-lcd-tianhao**: 捷宝宝定制版本
- **bread-compact-wifi-s3cam**: 带摄像头的S3版本

## 开发指南

### 环境准备
```bash
# 安装ESP-IDF开发环境 (v5.1+)
git clone -b v5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
source export.sh
项目克隆
bash
git clone https://github.com/your-repo/Jabob.git
cd Jabob
配置项目
bash
idf.py set-target esp32s3
idf.py menuconfig
编译烧录
bash
idf.py build
idf.py flash monitor
配置选项
音频配置
CONFIG_USE_AUDIO_PROCESSOR: 启用音频处理功能
CONFIG_USE_AFE_WAKE_WORD: 使用AFE唤醒词引擎
CONFIG_USE_ESP_WAKE_WORD: 使用ESP唤醒词引擎
CONFIG_USE_CUSTOM_WAKE_WORD: 使用自定义唤醒词引擎
显示配置
CONFIG_LCD_ST7789_240X320: 240×320分辨率ST7789显示屏
CONFIG_LCD_ST7789_170X320: 170×320分辨率ST7789显示屏
CONFIG_OLED_SSD1306_128X64: 128×64分辨率OLED显示屏
协议配置
CONFIG_PROTOCOL_MQTT: 启用MQTT协议支持
CONFIG_PROTOCOL_WEBSOCKET: 启用WebSocket协议支持
使用方法
硬件连接: 连接开发板至电脑，确保USB串口连接正常
固件烧录: 使用上述编译烧录命令烧录固件
首次启动: 设备将进入配网模式，通过WiFi热点配置网络
语音交互: 说出唤醒词后开始语音对话
界面操作: 通过显示屏和按键进行交互
扩展开发
添加新语言
在 main/assets 目录下创建新的语言包：

创建新的语言目录（如 de-DE/ 表示德语）
复制其他语言包中的 language.json 并翻译其中的文本
生成对应的 .p3 音频文件
在 lang_config.h 中添加相应的宏定义
添加新开发板
在 boards 目录下创建新的开发板目录
实现 Board 抽象类的必要方法
配置GPIO引脚和硬件参数
添加相应的Kconfig选项
组件依赖
项目使用了多个外部组件，包括：

esp-opus: Opus音频编解码
esp-sr: 语音识别和处理
esp_lcd_*: 各种LCD控制器驱动
esp_lvgl_port: LVGL图形库移植
esp-wifi-connect: WiFi连接管理
button: 按钮输入处理
led_strip: LED灯带控制
等等（详见 managed_components 目录）
故障排除
音频问题
检查音频引脚配置是否正确
确认音频编解码器驱动是否正常
检查音频采样率设置
显示问题
验证显示屏驱动配置
检查SPI/I2C引脚连接
确认显示屏分辨率和时序参数
网络问题
检查WiFi配置和认证信息
验证网络连接状态
确认防火墙和路由器设置
贡献
欢迎提交Issue和Pull Request来帮助改进本项目。

开发规范
遵循ESP-IDF编程规范
使用统一的代码风格
添加充分的注释和文档
编写单元测试
许可证
本项目采用MIT许可证。

