# USB 主机 UAC 驱动

[![Component Registry](https://components.espressif.com/components/espressif/usb_host_uac/badge.svg)](https://components.espressif.com/components/espressif/usb_host_uac)
![maintenance-status](https://img.shields.io/badge/maintenance-actively--developed-brightgreen.svg)
![changelog](https://img.shields.io/badge/Keep_a_Changelog-blue?logo=keepachangelog&logoColor=E05735)

本目录包含基于 [USB Host Library](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/api-reference/peripherals/usb_host.html) 实现的 USB UAC 驱动。

UAC 驱动用于访问 UAC 1.0 设备。

## 使用方法

下面列出 UAC 类驱动的典型 API 调用流程：

1. 通过 `usb_host_install()` 安装 USB 主机库
2. 通过 `uac_host_install()` 安装 UAC 驱动
3. 当新的（逻辑）UAC 设备连接时，驱动的事件回调会收到设备地址和事件：
   - `UAC_HOST_DRIVER_EVENT_TX_CONNECTED`
   - `UAC_HOST_DRIVER_EVENT_RX_CONNECTED`
4. 使用设备地址和接口号打开/关闭 UAC 设备：
   - `uac_host_device_open()`
   - `uac_host_device_close()`
5. 获取设备支持的音频格式：
   - `uac_host_get_device_info()`
   - `uac_host_get_device_alt_param()`
6. 使用特定音频格式启用/禁用数据流：
   - `uac_host_device_start()`
   - `uac_host_device_stop()`
7. 挂起/恢复数据流：
   - `uac_host_device_suspend()`
   - `uac_host_device_resume()`
8. 控制静音/取消静音：
   - `uac_host_device_set_mute()`
9. 控制音量：
   - `uac_host_device_set_volume()` 或 `uac_host_device_set_volume_db()`
10. 打开设备后，设备回调会收到以下事件：
    - `UAC_HOST_DEVICE_EVENT_RX_DONE`
    - `UAC_HOST_DEVICE_EVENT_TX_DONE`
    - `UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR`
    - `UAC_HOST_DRIVER_EVENT_DISCONNECTED`
11. 收到 `UAC_HOST_DRIVER_EVENT_DISCONNECTED` 时，应调用 `uac_host_device_close()` 关闭设备
12. 可通过 `uac_host_uninstall()` 卸载 UAC 驱动

> 注意：若物理设备同时包含麦克风和扬声器，驱动会将其视为两个独立的逻辑设备。

## 已知问题

- 无

## 示例

- 参考示例：usb_audio_player（见 esp-iot-solution 仓库）

## 支持的设备

- 支持任何兼容 UAC 1.0 的设备。
