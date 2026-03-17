#include "usb_audio_codec.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <usb_host_uac.h>
#include <usb/usb_types_ch9.h>
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
        usb_host_uac_close(uac_device_);
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
                if (codec->rx_handle_ == nullptr) {
                    codec->OpenRxStream();
                }
                if (codec->tx_handle_ == nullptr) {
                    codec->OpenTxStream();
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
        }
    }, "usb_event", 4096, this, 5, &usb_event_task_handle_);
    
    usb_host_initialized_ = true;
    ESP_LOGI(TAG, "USB Host initialized");
    
    return ESP_OK;
}

bool UsbAudioCodec::usb_event_callback(const usb_host_uac_event_t* event, void* user_data) {
    auto codec = reinterpret_cast<UsbAudioCodec*>(user_data);
    
    switch (event->type) {
        case UAC_EVENT_DEVICE_CONNECTED:
            xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_CONNECTED);
            break;
            
        case UAC_EVENT_DEVICE_DISCONNECTED:
            xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_DISCONNECTED);
            break;
            
        case UAC_EVENT_BUFFER_UNDERRUN:
            ESP_LOGW(TAG, "Buffer underrun detected");
            break;
            
        case UAC_EVENT_BUFFER_OVERRUN:
            ESP_LOGW(TAG, "Buffer overrun detected");
            break;
            
        case UAC_EVENT_TRANSFER_ERROR:
            ESP_LOGE(TAG, "Transfer error");
            xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_ERROR);
            break;
            
        default:
            break;
    }
    
    return true;
}

esp_err_t UsbAudioCodec::OpenRxStream() {
    if (!device_connected_ || uac_device_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Opening RX stream (MIC)...");
    
    // 获取设备信息
    esp_err_t ret = usb_host_uac_get_device_info(uac_device_, &device_info_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Device Info:");
    ESP_LOGI(TAG, "  Manufacturer: %s", device_info_.manufacturer ? device_info_.manufacturer : "N/A");
    ESP_LOGI(TAG, "  Product: %s", device_info_.product ? device_info_.product : "N/A");
    ESP_LOGI(TAG, "  RX channels: %d", device_info_.rx_channel_num);
    ESP_LOGI(TAG, "  TX channels: %d", device_info_.tx_channel_num);
    
    // 检查是否支持 RX（麦克风输入）
    if (device_info_.rx_channel_num == 0) {
        ESP_LOGW(TAG, "Device does not support RX (microphone input)");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // 配置 RX 流参数
    uac_stream_config_t rx_config = {
        .direction = UAC_STREAM_RX,              // 从设备接收数据（麦克风）
        .sample_rate = (uint32_t)input_sample_rate_,
        .channel_num = device_info_.rx_channel_num,
        .bit_resolution = 16,                    // UAC1.0 固定 16 位
        .buffer_size = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t),
        .buffer_num = AUDIO_CODEC_DMA_DESC_NUM,
        .delay_ms = 0,                           // 自动计算
    };
    
    // 打开 RX 流
    ret = usb_host_uac_stream_open(uac_device_, &rx_config, &uac_rx_stream_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open RX stream: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 更新实际通道数
    input_channels_ = device_info_.rx_channel_num;
    
    ESP_LOGI(TAG, "RX stream opened successfully - Channels: %d, Sample Rate: %dHz",
             input_channels_, input_sample_rate_);
    
    return ESP_OK;
}

esp_err_t UsbAudioCodec::OpenTxStream() {
    if (!device_connected_ || uac_device_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Opening TX stream (SPK)...");
    
    // 检查是否支持 TX（扬声器输出）
    if (device_info_.tx_channel_num == 0) {
        ESP_LOGW(TAG, "Device does not support TX (speaker output)");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // 配置 TX 流参数
    uac_stream_config_t tx_config = {
        .direction = UAC_STREAM_TX,              // 向设备发送数据（扬声器）
        .sample_rate = (uint32_t)output_sample_rate_,
        .channel_num = device_info_.tx_channel_num,
        .bit_resolution = 16,
        .buffer_size = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t),
        .buffer_num = AUDIO_CODEC_DMA_DESC_NUM,
        .delay_ms = 0,
    };
    
    // 打开 TX 流
    esp_err_t ret = usb_host_uac_stream_open(uac_device_, &tx_config, &uac_tx_stream_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open TX stream: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "TX stream opened successfully - Channels: %d, Sample Rate: %dHz",
             device_info_.tx_channel_num, output_sample_rate_);
    
    return ESP_OK;
}

void UsbAudioCodec::CloseStreams() {
    if (uac_rx_stream_ != nullptr) {
        usb_host_uac_stream_stop(uac_rx_stream_);
        usb_host_uac_stream_close(uac_rx_stream_);
        uac_rx_stream_ = nullptr;
        rx_stream_started_ = false;
        ESP_LOGI(TAG, "RX stream closed");
    }
    
    if (uac_tx_stream_ != nullptr) {
        usb_host_uac_stream_stop(uac_tx_stream_);
        usb_host_uac_stream_close(uac_tx_stream_);
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
    
    // 初始化 USB Host
    esp_err_t ret = InitializeUsbHost();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB Host");
        return;
    }
    
    // 等待设备连接
    if (!WaitForDevice(5000)) {
        ESP_LOGW(TAG, "No USB audio device connected, entering standby mode");
        return;
    }
    
    // 打开 UAC 设备
    uac_device_config_t device_config = {
        .callback = usb_event_callback,
        .user_data = this,
    };
    
    ret = usb_host_uac_open(&device_config, &uac_device_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open UAC device: %s", esp_err_to_name(ret));
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
        esp_err_t ret = usb_host_uac_stream_start(uac_rx_stream_);
        if (ret == ESP_OK) {
            rx_stream_started_ = true;
            input_enabled_ = true;
            ESP_LOGI(TAG, "RX (MIC) stream started");
        } else {
            ESP_LOGE(TAG, "Failed to start RX stream: %s", esp_err_to_name(ret));
        }
    } else if (!enable && uac_rx_stream_ != nullptr) {
        usb_host_uac_stream_stop(uac_rx_stream_);
        rx_stream_started_ = false;
        input_enabled_ = false;
        ESP_LOGI(TAG, "RX (MIC) stream stopped");
    }
}

void UsbAudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    
    if (enable && uac_tx_stream_ != nullptr && device_connected_) {
        esp_err_t ret = usb_host_uac_stream_start(uac_tx_stream_);
        if (ret == ESP_OK) {
            tx_stream_started_ = true;
            output_enabled_ = true;
            ESP_LOGI(TAG, "TX (SPK) stream started");
        } else {
            ESP_LOGE(TAG, "Failed to start TX stream: %s", esp_err_to_name(ret));
        }
    } else if (!enable && uac_tx_stream_ != nullptr) {
        usb_host_uac_stream_stop(uac_tx_stream_);
        tx_stream_started_ = false;
        output_enabled_ = false;
        ESP_LOGI(TAG, "TX (SPK) stream stopped");
    }
}

void UsbAudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    
    // 如果设备支持硬件音量控制，可以尝试设置
    // 注意：并非所有 UAC 设备都支持硬件音量控制
    
    ESP_LOGI(TAG, "Output volume set to %d (software control)", output_volume_);
    
    // 保存到 NVS
    Settings settings("audio", true);
    settings.SetInt("output_volume", output_volume_);
}

int UsbAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || uac_rx_stream_ == nullptr || !device_connected_) {
        return 0;
    }
    
    size_t bytes_read = 0;
    
    // 从 USB 音频流读取数据
    esp_err_t ret = usb_host_uac_stream_read(
        uac_rx_stream_,
        dest,
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
    
    size_t bytes_written = 0;
    
    // 写入到 USB 音频流
    esp_err_t ret = usb_host_uac_stream_write(
        uac_tx_stream_,
        buffer.data(),
        samples * sizeof(int16_t),
        &bytes_written,
        portMAX_DELAY
    );
    
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(ret));
        }
        return 0;
    }
    
    return bytes_written / sizeof(int16_t);
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
