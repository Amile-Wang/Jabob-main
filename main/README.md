# 捷宝宝 AI 聊天机器人 - Main 模块

## 概述

Main 模块是捷宝宝 AI 聊天机器人项目的核心应用层，包含了主要的应用逻辑、硬件抽象、通信协议和系统服务等关键功能模块。

## 目录结构

### 核心应用组件

#### [application.cc/h](file://z:\jabobo\Jabob-main\main\application.cc#L278-L356)
- **功能**: 应用程序主控制器，负责协调各个子系统的工作
- **职责**: 管理设备状态、处理用户交互、协调音频和显示系统、管理通信协议等
- **特点**: 实现了状态机模式，支持多种工作模式（待机、聆听、说话等）

#### [main.cc](file://z:\jabobo\Jabob-main\main\main.cc#L1-L50)
- **功能**: 程序入口点，初始化整个应用系统
- **职责**: 初始化硬件、启动应用主循环、设置系统配置

### 硬件抽象层

#### [boards/](file://z:\jabobo\Jabob-main\main\boards\README.md#L1-L104)
- **功能**: 硬件抽象层，针对不同开发板的特定实现
- **职责**: 抽象硬件差异，提供统一的硬件接口
- **包含**: 多种开发板配置（WiFi版、LCD版、紧凑版等）

### 通信协议

#### [protocols/](file://z:\jabobo\Jabob-main\main\protocols\README.md#L1-L84)
- **功能**: 通信协议实现，支持多种网络协议
- **职责**: 实现与服务器的通信，支持 MQTT 和 WebSocket 协议
- **特点**: 统一的协议接口，支持协议热切换

### 音频系统

#### [audio/](file://z:\jabobo\Jabob-main\main\audio\README.md#L1-L121)
- **功能**: 音频处理系统，负责音频输入输出
- **职责**: 音频编解码、音频处理、唤醒词检测、语音编码传输
- **特点**: 支持多种音频格式和处理算法

### 显示系统

#### [display/](file://z:\jabobo\Jabob-main\main\display\README.md#L1-L77)
- **功能**: 显示系统，负责图形界面和状态显示
- **职责**: 状态栏显示、通知消息、情绪表达、预览图片等
- **特点**: 支持多种显示技术（LCD、LVGL）

### 系统服务

#### [ota.cc/h](file://z:\jabobo\Jabob-main\main\ota.h#L10-L31)
- **功能**: OTA（Over-The-Air）空中升级服务
- **职责**: 固件下载、验证和更新，支持安全升级
- **特点**: 支持差分升级、回滚机制

#### [settings.cc/h](file://z:\jabobo\Jabob-main\main\settings.h#L1-L23)
- **功能**: 系统配置管理
- **职责**: 保存和读取用户配置、设备设置
- **特点**: 非易失性存储，支持配置持久化

#### [system_info.cc/h](file://z:\jabobo\Jabob-main\main\system_info.h#L8-L15)
- **功能**: 系统信息获取和管理
- **职责**: 获取设备状态、网络信息、电池电量等
- **特点**: 统一的信息查询接口

### 硬件控制模块

#### [led/](file://z:\jabobo\Jabob-main\main\led\led.h#L9-L22)
- **功能**: LED 控制系统
- **职责**: 管理设备上的 LED 指示灯，提供状态指示
- **特点**: 支持多种 LED 类型（普通 LED、环形 LED 等）

#### [pwm/](file://z:\jabobo\Jabob-main\main\pwm\pwm_servo.h#L17-L34)
- **功能**: PWM 控制模块
- **职责**: 控制 PWM 设备，如舵机等
- **特点**: 精确的 PWM 信号生成

#### [rfid/](file://z:\jabobo\Jabob-main\main\rfid\rfid_manager.h#L9-L20)
- **功能**: RFID/NFC 管理
- **职责**: 读取 RFID 卡片信息，处理 NFC 通信
- **特点**: 支持多种 RFID/NFC 标准

### 通信服务

#### [mcp_server.cc/h](file://z:\jabobo\Jabob-main\main\mcp_server.h#L23-L64)
- **功能**: MCP（Message Communication Protocol）服务器
- **职责**: 处理设备内部消息通信，提供服务接口
- **特点**: 支持多种通信方式，提供标准化接口

### 状态管理

#### [device_state.h](file://z:\jabobo\Jabob-main\main\device_state.h#L1-L22)
- **功能**: 设备状态定义
- **职责**: 定义设备的各种状态类型
- **特点**: 状态枚举定义，便于状态机管理

#### [device_state_event.cc/h](file://z:\jabobo\Jabob-main\main\device_state_event.h#L1-L23)
- **功能**: 设备状态事件系统
- **职责**: 管理状态变更事件，触发相应动作
- **特点**: 事件驱动的状态管理

### 资源管理

#### [assets/](file://z:\jabobo\Jabob-main\main\assets\README.md#L1-L38)
- **功能**: 应用资源存储
- **职责**: 存储多语言文本、音频资源、图片等
- **特点**: 支持多语言国际化

### 项目配置文件

#### CMakeLists.txt
- **功能**: CMake 构建配置文件
- **职责**: 定义构建规则、依赖关系、编译选项

#### Kconfig.projbuild
- **功能**: 项目配置选项定义
- **职责**: 定义可配置的项目选项，供 menuconfig 使用

#### idf_component.yml
- **功能**: ESP-IDF 组件配置文件
- **职责**: 定义组件依赖关系和元数据

#### components_README.md
- **功能**: 组件说明文档
- **职责**: 详细说明项目中使用的各个组件及其功能

## 系统架构
     +------------------+
     |   Application    |
     |  (Main Logic)    |
     +------------------+
              |
     +------------------+
     |  Communication   |
     | (Protocols/MCP)  |
     +------------------+
     |     |      |     |
+----v--+  |      +---v-----+
| Audio |  |      | Display |
+-------+  |      +---------+
           |
     +-----v------+
     |  Services  |
     |(OTA,State) |
     +------------+
     |     |      |
+----v--+  |      +---v-----+
| Boards|  |      | Hardware|
|(HAL)  |  |      | (LED,PWM|
+-------+  |      | ,RFID)  |
           |      +---------+
     +-----v------+
     | System Info|
     | & Settings |
     +------------+

     
## 关键特性

1. **模块化设计**: 各功能模块高度解耦，便于维护和扩展
2. **硬件抽象**: 通过 Board 抽象层支持多种硬件平台
3. **多协议支持**: 支持 MQTT 和 WebSocket 等多种通信协议
4. **状态管理**: 完整的状态机设计，保证系统行为一致性
5. **资源管理**: 高效的资源管理，支持多语言和多媒体内容
6. **OTA 更新**: 支持远程固件升级，便于产品维护

## 使用说明

开发者在使用本项目时，应重点关注 [application.cc](file://z:\jabobo\Jabob-main\main\application.cc#L278-L356) 中的状态管理和各模块间的协作逻辑。对于硬件定制，应参考 [boards/](file://z:\jabobo\Jabob-main\main\boards\README.md#L1-L104) 目录中的实现。