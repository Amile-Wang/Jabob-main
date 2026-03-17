#include "usb_audio_codec.h"
#include "settings.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <usb/uac_host.h>
#include <usb/usb_types_ch9.h>
#include <usb/usb_host.h>  // USB Host 底层 API
#endif

#include <esp_log.h>
#include <cmath>
#include <cstring>

#define TAG "UsbAudioCodec"

// USB 事件位定义
#define USB_EVENT_CONNECTED     (1 << 0)
#define USB_EVENT_DISCONNECTED  (1 << 1)
#define USB_EVENT_ERROR         (1 << 2)

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
    // 停止流传输
    CloseStreams();
    
    // 关闭设备
    if (uac_device_ != nullptr) {
        ESP_LOGI(TAG, "Closing UAC device");
        uac_host_device_close(uac_device_);
        uac_device_ = nullptr;
    }
    
    // 删除事件任务
    if (usb_event_task_handle_ != nullptr) {
        vTaskDelete(usb_event_task_handle_);
    }
    
    // 删除事件组
    if (usb_event_group_ != nullptr) {
        vEventGroupDelete(usb_event_group_);
    }
    
    ESP_LOGI(TAG, "UsbAudioCodec destroyed");
#endif
}

#ifdef CONFIG_IDF_TARGET_ESP32S3

// UAC 驱动事件回调
void uac_driver_event_callback(uint8_t addr, uint8_t iface_num,
                                      const uac_host_driver_event_t event, void *arg) {
    auto codec = reinterpret_cast<UsbAudioCodec*>(arg);
    
    switch (event) {
        case UAC_HOST_DRIVER_EVENT_RX_CONNECTED:
            ESP_LOGI(TAG, "UAC RX device connected - Addr: %d, Iface: %d", addr, iface_num);
            if (codec->usb_event_group_ != nullptr) {
                xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_CONNECTED);
            }
            break;
            
        case UAC_HOST_DRIVER_EVENT_TX_CONNECTED:
            ESP_LOGI(TAG, "UAC TX device connected - Addr: %d, Iface: %d", addr, iface_num);
            if (codec->usb_event_group_ != nullptr) {
                xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_CONNECTED);
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
        .core_id = tskNO_AFFINITY,
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
                USB_EVENT_CONNECTED | USB_EVENT_DISCONNECTED | USB_EVENT_ERROR,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY
            );
            
            if (events & USB_EVENT_CONNECTED) {
                ESP_LOGI(TAG, "USB Audio device connected");
                codec->device_connected_ = true;
                
                // 尝试打开音频流
                codec->OpenRxStream();
                codec->OpenTxStream();
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
        }
    }, "usb_event", 4096, this, 5, &usb_event_task_handle_);
    
    usb_host_initialized_ = true;
    ESP_LOGI(TAG, "USB Host and UAC driver initialized");
    
    return ESP_OK;
}


esp_err_t UsbAudioCodec::OpenRxStream() {
    if (!device_connected_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Opening RX stream (MIC)...");
    
    // 配置 RX 流参数
    uac_host_device_config_t rx_config = {
        .buffer_size = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t),
        .buffer_threshold = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t) / 2,
        .callback = nullptr,
        .callback_arg = this,
    };
    
    // 打开 RX 流（使用第一个找到的 RX 设备）
    esp_err_t ret = uac_host_device_open(&rx_config, &uac_rx_stream_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open RX stream: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 获取设备信息
    ret = uac_host_get_device_info(uac_rx_stream_, &device_info_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Device Info:");
    ESP_LOGI(TAG, "  VID: 0x%04X, PID: 0x%04X", device_info_.VID, device_info_.PID);
    ESP_LOGI(TAG, "  Stream Type: %s", device_info_.type == UAC_STREAM_RX ? "RX(MIC)" : "TX(SPK)");
    
    // 启动 RX 流
    uac_host_stream_config_t stream_config = {
        .channels = 1,  // 默认单声道
        .bit_resolution = 16,
        .sample_freq = (uint32_t)input_sample_rate_,
        .flags = 0,
    };
    
    ret = uac_host_device_start(uac_rx_stream_, &stream_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start RX stream: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_rx_stream_);
        uac_rx_stream_ = nullptr;
        return ret;
    }
    
    // 更新实际通道数
    input_channels_ = stream_config.channels;
    
    ESP_LOGI(TAG, "RX stream opened successfully - Channels: %d, Sample Rate: %dHz",
             input_channels_, input_sample_rate_);
    
    return ESP_OK;
}

esp_err_t UsbAudioCodec::OpenTxStream() {
    if (!device_connected_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Opening TX stream (SPK)...");
    
    // 配置 TX 流参数
    uac_host_device_config_t tx_config = {
        .buffer_size = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t),
        .buffer_threshold = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t) / 2,
        .callback = nullptr,
        .callback_arg = this,
    };
    
    // 打开 TX 流（使用第一个找到的 TX 设备）
    esp_err_t ret = uac_host_device_open(&tx_config, &uac_tx_stream_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open TX stream: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 启动 TX 流
    uac_host_stream_config_t stream_config = {
        .channels = 1,  // 默认单声道
        .bit_resolution = 16,
        .sample_freq = (uint32_t)output_sample_rate_,
        .flags = 0,
    };
    
    ret = uac_host_device_start(uac_tx_stream_, &stream_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TX stream: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_tx_stream_);
        uac_tx_stream_ = nullptr;
        return ret;
    }
    
    ESP_LOGI(TAG, "TX stream opened successfully - Sample Rate: %dHz",
             output_sample_rate_);
    
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
    ESP_LOGI(TAG, "Waiting for USB audio device...");
    
    EventBits_t bits = xEventGroupWaitBits(
        usb_event_group_,
        USB_EVENT_CONNECTED,
        pdTRUE,  // 清除标志位
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );
    
    if (bits & USB_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "USB audio device found");
        return true;
    }
    
    ESP_LOGW(TAG, "No USB audio device found within %dms", timeout_ms);
    return false;
}

void UsbAudioCodec::Start() {
    ESP_LOGI(TAG, "Starting USB Audio codec...");
    
    // 读取音量设置
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    
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
    
    ESP_LOGI(TAG, "UAC device opened");
    
    // 短暂延迟等待设备完全枚举
    vTaskDelay(pdMS_TO_TICKS(500));
    
    EnableInput(true);
    EnableOutput(true);
    
    ESP_LOGI(TAG, "USB Audio codec started successfully");
}

void UsbAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    
    if (enable && uac_rx_stream_ != nullptr && device_connected_) {
        // RX 流已经在 OpenRxStream 中启动
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
    if (enable == output_enabled_) {
        return;
    }
    
    if (enable && uac_tx_stream_ != nullptr && device_connected_) {
        // TX 流已经在 OpenTxStream 中启动
        tx_stream_started_ = true;
        output_enabled_ = true;
        ESP_LOGI(TAG, "TX (SPK) enabled");
    } else if (!enable && uac_tx_stream_ != nullptr) {
        uac_host_device_stop(uac_tx_stream_);
        tx_stream_started_ = false;
        output_enabled_ = false;
        ESP_LOGI(TAG, "TX (SPK) disabled");
    }
}

void UsbAudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    
    // 如果设备支持硬件音量控制，可以尝试设置
    if (uac_tx_stream_ != nullptr && device_connected_) {
        esp_err_t ret = uac_host_device_set_volume(uac_tx_stream_, (uint8_t)volume);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Output volume set to %d (hardware control)", output_volume_);
        } else {
            ESP_LOGW(TAG, "Hardware volume control not supported, using software control");
        }
    } else {
        ESP_LOGI(TAG, "Output volume set to %d (software control)", output_volume_);
    }
    
    // 保存到 NVS
    Settings settings("audio", true);
    settings.SetInt("output_volume", output_volume_);
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
    if (!output_enabled_ || uac_tx_stream_ == nullptr || !device_connected_) {
        return 0;
    }
    
    // 应用音量控制（软件）
    std::vector<int16_t> buffer(samples);
    float volume_factor = powf(static_cast<float>(output_volume_) / 100.0f, 2.0f);
    
    for (int i = 0; i < samples; i++) {
        float temp = static_cast<float>(data[i]) * volume_factor;
        if (temp > INT16_MAX) {
            buffer[i] = INT16_MAX;
        } else if (temp < INT16_MIN) {
            buffer[i] = INT16_MIN;
        } else {
            buffer[i] = static_cast<int16_t>(temp);
        }
    }
    
    // 写入到 USB 音频流
    esp_err_t ret = uac_host_device_write(
        uac_tx_stream_,
        reinterpret_cast<uint8_t*>(buffer.data()),
        samples * sizeof(int16_t),
        portMAX_DELAY
    );
    
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(ret));
        }
        return 0;
    }
    
    return samples;
}

#else  // CONFIG_IDF_TARGET_ESP32S3

// 非 ESP32-S3 平台的空实现

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

#endif  // CONFIG_IDF_TARGET_ESP32S3
