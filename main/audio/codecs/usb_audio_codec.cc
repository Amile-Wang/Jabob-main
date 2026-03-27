#include "usb_audio_codec.h"
#include "settings.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <usb/uac_host.h>
#include <usb/usb_types_ch9.h>
#include <usb/usb_host.h>  // USB Host 底层 API
#include <esp_task_wdt.h>  // 看门狗相关API
#endif

#include <esp_log.h>
#include <cmath>
#include <cstring>

#define TAG "UsbAudioCodec"

// USB 事件位定义
#define USB_EVENT_RX_CONNECTED  (1 << 0)
#define USB_EVENT_TX_CONNECTED  (1 << 1)
#define USB_EVENT_DISCONNECTED  (1 << 2)
#define USB_EVENT_ERROR         (1 << 3)

UsbAudioCodec::UsbAudioCodec(int input_sample_rate, int output_sample_rate) {
    duplex_ = true;                    // UAC 设备通常支持双工
    input_reference_ = false;          // USB 麦克风通常不提供参考信号
    input_channels_ = 1;               // 默认单声道，实际值在枚举时确定
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    
#ifdef CONFIG_IDF_TARGET_ESP32S3
    usb_event_group_ = xEventGroupCreate();
#endif
    
    ESP_LOGI(TAG, "UsbAudioCodec created - Input: %dHz, Output: %dHz", 
             input_sample_rate_, output_sample_rate_);
}

UsbAudioCodec::~UsbAudioCodec() {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    if (usb_event_group_ != nullptr) {
        vEventGroupDelete(usb_event_group_);
    }
    
    if (usb_host_initialized_) {
        CloseStreams();
        uac_host_driver_uninstall();
        usb_host_uninstall();
        usb_host_initialized_ = false;
    }
    
    ESP_LOGI(TAG, "UsbAudioCodec destroyed");
#endif
}

#ifdef CONFIG_IDF_TARGET_ESP32S3

// UAC 驱动事件回调 - 在头文件中已声明为 extern "C"
void uac_driver_event_callback(uint8_t addr, uint8_t iface_num,
                                      const uac_host_driver_event_t event, void *arg) {
    // arg 参数应该是指向 UsbAudioCodec 实例的指针
    auto codec = reinterpret_cast<UsbAudioCodec*>(arg);
    
    switch (event) {
        case UAC_HOST_DRIVER_EVENT_RX_CONNECTED:
            ESP_LOGI(TAG, "UAC RX device connected - Addr: %d, Iface: %d", addr, iface_num);
            // 保存设备信息供主任务使用
            // 注意：现在需要通过公共方法或确保这些成员是可访问的
            // 由于我们移除了友元声明，这里需要确保这些操作是安全的
            if (codec) {
                codec->rx_device_addr_ = addr;
                codec->rx_iface_num_ = iface_num;
                if (codec->usb_event_group_ != nullptr) {
                    xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_RX_CONNECTED);
                }
            }
            break;
            
        case UAC_HOST_DRIVER_EVENT_TX_CONNECTED:
            ESP_LOGI(TAG, "UAC TX device connected - Addr: %d, Iface: %d", addr, iface_num);
            if (codec) {
                codec->tx_device_addr_ = addr;
                codec->tx_iface_num_ = iface_num;
                if (codec->usb_event_group_ != nullptr) {
                    xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_TX_CONNECTED);
                }
            }
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown UAC driver event: %d", event);
            break;
    }
}

esp_err_t UsbAudioCodec::InitializeUsbHost() {
    if (usb_host_initialized_) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing USB Host...");
    
    // 1. 安装 USB Host 驱动
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    
    esp_err_t ret = usb_host_install(&host_config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install USB Host: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 2. 安装 UAC 驱动
    uac_host_driver_config_t uac_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,  // 固定在 CPU Core 0 上运行，避免 tskNO_AFFINITY 导致的错误
        .callback = uac_driver_event_callback,
        .callback_arg = this,
    };
    
    ret = uac_host_install(&uac_config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install UAC driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 创建 USB 事件处理任务
    xTaskCreate([](void* arg) {
        auto codec = reinterpret_cast<UsbAudioCodec*>(arg);
        
        while (true) {
            EventBits_t events = xEventGroupWaitBits(
                codec->usb_event_group_,
                USB_EVENT_RX_CONNECTED | USB_EVENT_TX_CONNECTED | USB_EVENT_DISCONNECTED | USB_EVENT_ERROR,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY
            );
            
            if (events & USB_EVENT_RX_CONNECTED) {
                ESP_LOGI(TAG, "USB Audio RX device connected");
                codec->device_connected_ = true;
                
                // 在主任务中打开 RX 设备（使用从回调获取的地址和接口号）
                esp_err_t rx_ret = codec->OpenRxStream(codec->rx_device_addr_, codec->rx_iface_num_);
                if (rx_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to open RX stream: %s", esp_err_to_name(rx_ret));
                } else {
                    // 获取并打印设备信息
                    uac_host_dev_info_t dev_info;
                    esp_err_t info_ret = uac_host_get_device_info(codec->uac_rx_stream_, &dev_info);
                    if (info_ret == ESP_OK) {
                        ESP_LOGI(TAG, "=== USB Audio Device Info ===");
                        ESP_LOGI(TAG, "  Manufacturer: %ls", dev_info.iManufacturer[0] ? dev_info.iManufacturer : L"N/A");
                        ESP_LOGI(TAG, "  Product: %ls", dev_info.iProduct[0] ? dev_info.iProduct : L"N/A");
                        ESP_LOGI(TAG, "  Serial Number: %ls", dev_info.iSerialNumber[0] ? dev_info.iSerialNumber : L"N/A");
                        ESP_LOGI(TAG, "  VID: 0x%04X, PID: 0x%04X", dev_info.VID, dev_info.PID);
                        ESP_LOGI(TAG, "  Interface Num: %d", dev_info.iface_num);
                        ESP_LOGI(TAG, "  Stream Type: %s", dev_info.type == UAC_STREAM_RX ? "RX(Microphone)" : "TX(Speaker)");
                        ESP_LOGI(TAG, "===============================");
                    }
                }
            }
            
            if (events & USB_EVENT_TX_CONNECTED) {
                ESP_LOGI(TAG, "USB Audio TX device connected");
                codec->device_connected_ = true;
                
                // 在主任务中打开 TX 设备（使用从回调获取的地址和接口号）
                esp_err_t tx_ret = codec->OpenTxStream(codec->tx_device_addr_, codec->tx_iface_num_);
                if (tx_ret != ESP_OK) {
                    ESP_LOGW(TAG, "TX stream not available: %s", esp_err_to_name(tx_ret));
                }
            }
            
            if (events & USB_EVENT_DISCONNECTED) {
                ESP_LOGW(TAG, "USB Audio device disconnected");
                codec->device_connected_ = false;
                codec->rx_stream_started_ = false;
                codec->tx_stream_started_ = false;
                codec->input_enabled_ = false;
                codec->output_enabled_ = false;
                
                // 清理流句柄
                codec->CloseStreams();
            }
            
            if (events & USB_EVENT_ERROR) {
                ESP_LOGE(TAG, "USB Audio error occurred");
            }
            
            // 主动喂狗，防止看门狗复位
            esp_task_wdt_reset();
        }
    }, "usb_event", 4096, this, 5, &usb_event_task_handle_);
    
    usb_host_initialized_ = true;
    ESP_LOGI(TAG, "USB Host and UAC driver initialized");
    
    return ESP_OK;
}


esp_err_t UsbAudioCodec::OpenRxStream(uint8_t addr, uint8_t iface_num) {
    if (!device_connected_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Opening RX stream (MIC) - Addr: %d, Iface: %d", addr, iface_num);
    
    // 配置 RX 流参数
    uac_host_device_config_t rx_config = {
        .addr = addr,  // 使用从回调获取的地址
        .iface_num = iface_num,  // 使用从回调获取的接口号
        .buffer_size = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t),
        .buffer_threshold = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t) / 2,
        .callback = nullptr,
        .callback_arg = this,
    };
    
    // 打开 RX 流
    esp_err_t ret = uac_host_device_open(&rx_config, &uac_rx_stream_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open RX stream: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 查询设备实际支持的能力
    uac_host_dev_alt_param_t rx_alt_params;
    ret = uac_host_get_device_alt_param(uac_rx_stream_, 1, &rx_alt_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get RX device alt param: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_rx_stream_);
        uac_rx_stream_ = nullptr;
        return ret;
    }
    
    // 使用设备实际支持的参数配置流
    uac_host_stream_config_t stream_config = {
        .channels = rx_alt_params.channels,
        .bit_resolution = rx_alt_params.bit_resolution,
        .sample_freq = rx_alt_params.sample_freq[0],  // 使用设备支持的第一个采样率
        .flags = 0,
    };
    
    ret = uac_host_device_start(uac_rx_stream_, &stream_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start RX stream: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_rx_stream_);
        uac_rx_stream_ = nullptr;
        return ret;
    }
    
    // 更新实际通道数和采样率
    input_channels_ = stream_config.channels;
    input_sample_rate_ = stream_config.sample_freq;
    
    ESP_LOGI(TAG, "RX stream opened successfully - Channels: %d, Sample Rate: %dHz",
             input_channels_, input_sample_rate_);
    
    return ESP_OK;
}

esp_err_t UsbAudioCodec::OpenTxStream(uint8_t addr, uint8_t iface_num) {
    if (!device_connected_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Opening TX stream (SPK) - Addr: %d, Iface: %d", addr, iface_num);
    
    // 配置 TX 流参数
    uac_host_device_config_t tx_config = {
        .addr = addr,  // 使用从回调获取的地址
        .iface_num = iface_num,  // 使用从回调获取的接口号
        .buffer_size = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t),
        .buffer_threshold = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t) / 2,
        .callback = nullptr,
        .callback_arg = this,
    };
    
    // 打开 TX 流
    esp_err_t ret = uac_host_device_open(&tx_config, &uac_tx_stream_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open TX stream: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 查询设备实际支持的能力
    uac_host_dev_alt_param_t tx_alt_params;
    ret = uac_host_get_device_alt_param(uac_tx_stream_, 1, &tx_alt_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get TX device alt param: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_tx_stream_);
        uac_tx_stream_ = nullptr;
        return ret;
    }
    
    // 使用设备实际支持的参数配置流
    uac_host_stream_config_t stream_config = {
        .channels = tx_alt_params.channels,
        .bit_resolution = tx_alt_params.bit_resolution,
        .sample_freq = tx_alt_params.sample_freq[0],  // 使用设备支持的第一个采样率
        .flags = 0,
    };
    
    ret = uac_host_device_start(uac_tx_stream_, &stream_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TX stream: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_tx_stream_);
        uac_tx_stream_ = nullptr;
        return ret;
    }
    
    // 更新实际输出采样率
    output_sample_rate_ = stream_config.sample_freq;
    
    ESP_LOGI(TAG, "TX stream opened successfully - Channels: %d, Sample Rate: %dHz",
             stream_config.channels, output_sample_rate_);
    
    return ESP_OK;
}

void UsbAudioCodec::CloseStreams() {
    if (uac_rx_stream_ != nullptr) {
        uac_host_device_stop(uac_rx_stream_);
        uac_host_device_close(uac_rx_stream_);
        uac_rx_stream_ = nullptr;
        rx_stream_started_ = false;
        ESP_LOGI(TAG, "RX stream closed");
    }
    
    if (uac_tx_stream_ != nullptr) {
        uac_host_device_stop(uac_tx_stream_);
        uac_host_device_close(uac_tx_stream_);
        uac_tx_stream_ = nullptr;
        tx_stream_started_ = false;
        ESP_LOGI(TAG, "TX stream closed");
    }
}

bool UsbAudioCodec::WaitForDevice(int timeout_ms) {
    ESP_LOGI(TAG, "Waiting for USB audio device (timeout: %dms)...", timeout_ms);
    ESP_LOGI(TAG, "Please ensure USB microphone is properly connected");
    
    // 给USB Host一些时间来初始化和开始枚举设备
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_task_wdt_reset();
    
    // 拆分长延时为短延时循环，防止看门狗复位
    TickType_t total_ticks = pdMS_TO_TICKS(timeout_ms);
    TickType_t elapsed_ticks = 0;
    const TickType_t check_interval = pdMS_TO_TICKS(100);
    
    while (elapsed_ticks < total_ticks) {
        EventBits_t bits = xEventGroupWaitBits(
            usb_event_group_,
            USB_EVENT_RX_CONNECTED | USB_EVENT_TX_CONNECTED,
            pdTRUE,  // 清除标志位
            pdFALSE,
            check_interval
        );
        
        if (bits & (USB_EVENT_RX_CONNECTED | USB_EVENT_TX_CONNECTED)) {
            ESP_LOGI(TAG, "USB audio device found and ready");
            return true;
        }
        
        elapsed_ticks += check_interval;
        esp_task_wdt_reset();  // 主动喂狗
        
        // 如果设备还没连接，给USB Host更多时间处理事件
        if (elapsed_ticks % pdMS_TO_TICKS(1000) == 0) {
            ESP_LOGD(TAG, "Still waiting for USB device... (%dms elapsed)", 
                     (int)(elapsed_ticks * portTICK_PERIOD_MS));
        }
    }
    
    ESP_LOGW(TAG, "No USB audio device found within %dms", timeout_ms);
    ESP_LOGW(TAG, "Entering standby mode - system will continue without USB microphone");
    ESP_LOGW(TAG, "To use USB microphone:");
    ESP_LOGW(TAG, "  1. Check USB connection");
    ESP_LOGW(TAG, "  2. Verify USB cable supports data transfer");
    ESP_LOGW(TAG, "  3. Ensure adequate power supply");
    ESP_LOGW(TAG, "  4. Try reconnecting the device");
    return false;
}

void UsbAudioCodec::Start() {
    ESP_LOGI(TAG, "Starting USB Audio codec (microphone only)...");
    
    // 初始化 USB Host 和 UAC 驱动
    esp_err_t ret = InitializeUsbHost();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB Host");
        return;
    }
    
    // 等待设备连接（在事件任务中自动处理）
    if (!WaitForDevice(5000)) {
        ESP_LOGW(TAG, "No USB audio device connected, entering standby mode");
        return;
    }
    
    // 短暂延迟等待设备完全枚举，期间喂狗
    for (int i = 0; i < 5; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    
    EnableInput(true);
    EnableOutput(false);  // 禁用输出
    
    ESP_LOGI(TAG, "USB Audio codec started successfully (microphone only)");
}

void UsbAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    
    if (enable && uac_rx_stream_ != nullptr && device_connected_) {
        rx_stream_started_ = true;
        input_enabled_ = true;
        ESP_LOGI(TAG, "RX (MIC) enabled");
    } else if (!enable && uac_rx_stream_ != nullptr) {
        uac_host_device_stop(uac_rx_stream_);
        rx_stream_started_ = false;
        input_enabled_ = false;
        ESP_LOGI(TAG, "RX (MIC) disabled");
    }
}

void UsbAudioCodec::EnableOutput(bool enable) {
    // 不支持 USB 扬声器输出，始终禁用
    output_enabled_ = false;
    if (enable) {
        ESP_LOGW(TAG, "USB speaker output is not supported, output disabled");
    }
}

void UsbAudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGW(TAG, "USB speaker output is not supported, volume setting ignored");
}

int UsbAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || uac_rx_stream_ == nullptr || !device_connected_) {
        return 0;
    }
    
    uint32_t bytes_read = 0;
    
    // 从 USB 音频流读取数据
    esp_err_t ret = uac_host_device_read(
        uac_rx_stream_,
        reinterpret_cast<uint8_t*>(dest),
        samples * sizeof(int16_t),
        &bytes_read,
        portMAX_DELAY
    );
    
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        }
        return 0;
    }
    
    // 返回实际读取的样本数
    return bytes_read / sizeof(int16_t);
}

int UsbAudioCodec::Write(const int16_t* data, int samples) {
    // 不支持 USB 扬声器输出
    return 0;
}

#endif  // CONFIG_IDF_TARGET_ESP32S3

// 非 ESP32-S3 平台的空实现 - 注意：这里没有 #else，因为 Read/Write 已经在头文件中有内联实现
#ifndef CONFIG_IDF_TARGET_ESP32S3

void UsbAudioCodec::Start() {
    ESP_LOGE(TAG, "UAC is only supported on ESP32-S3");
}

void UsbAudioCodec::EnableInput(bool enable) {
    ESP_LOGW(TAG, "UAC is not available on this platform");
}

void UsbAudioCodec::EnableOutput(bool enable) {
    ESP_LOGW(TAG, "UAC is not available on this platform");
}

void UsbAudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGW(TAG, "Volume setting has no effect (UAC not available)");
}

#endif  // !CONFIG_IDF_TARGET_ESP32S3