#include "hybrid_usb_i2s_codec.h"
#include "board.h"

#include <esp_log.h>
#include <cstring>
#include <usb/usb_host.h>

#define TAG "HybridUsbI2sCodec"

// 启用更详细的 USB Host 日志
// #define USB_HOST_LOG_LEVEL ESP_LOG_DEBUG
// #define UAC_HOST_LOG_LEVEL ESP_LOG_DEBUG

// USB Host 事件处理任务
static void usb_host_task(void *arg) {
    while (true) {
        uint32_t event_flags;
        esp_err_t ret = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_lib_handle_events failed: %s", esp_err_to_name(ret));
            break;
        }
        
        // 检查是否需要退出任务
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more USB clients, exiting USB host task");
            break;
        }
    }
    
    vTaskDelete(NULL);
}

HybridUsbI2sCodec::HybridUsbI2sCodec(int output_sample_rate, 
                                   gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout)
    : spk_bclk_(spk_bclk), spk_ws_(spk_ws), spk_dout_(spk_dout), output_sample_rate_(output_sample_rate) {
    
    // 设置基类属性 - 输入采样率初始化为 0，表示尚未确定
    duplex_ = true;
    input_reference_ = false;
    input_enabled_ = false;
    output_enabled_ = false;
    input_sample_rate_ = 0;  // 动态确定
    output_sample_rate_ = output_sample_rate;
    input_channels_ = 1;
    output_channels_ = 1;
    
    // 初始化成员变量
    usb_event_group_ = nullptr;
    uac_rx_device_ = nullptr;
    usb_device_addr_ = 0;
    usb_iface_num_ = 0;
    usb_microphone_ready_ = false;
    i2s_tx_handle_ = nullptr;
    usb_initialized_ = false;
    i2s_initialized_ = false;
    
    ESP_LOGI(TAG, "HybridUsbI2sCodec created - Input: Dynamic (USB), Output: %dHz (I2S)", 
             output_sample_rate_);
}

HybridUsbI2sCodec::~HybridUsbI2sCodec() {
    // 关闭 USB 麦克风
    CloseUsbMicrophone();
    
    // 关闭 I2S 扬声器  
    CloseI2sSpeaker();
    
    // 清理 USB Host 资源
    if (usb_initialized_) {
        // 卸载 UAC 驱动
        uac_host_uninstall();
        
        // 等待 USB Host 任务退出
        if (usb_host_task_handle_) {
            vTaskDelete(usb_host_task_handle_);
            usb_host_task_handle_ = nullptr;
        }
        
        // 卸载 USB Host
        usb_host_uninstall();
        usb_initialized_ = false;
    }
    
    // 删除事件组
    if (usb_event_group_) {
        vEventGroupDelete(usb_event_group_);
        usb_event_group_ = nullptr;
    }
}

void HybridUsbI2sCodec::Start() {
    ESP_LOGI(TAG, "Starting Hybrid USB-I2S Audio codec...");
    
    // 创建事件组
    usb_event_group_ = xEventGroupCreate();
    if (usb_event_group_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }
    
    // 初始化 USB Host
    if (InitializeUsbHost() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB Host");
        return;
    }
    usb_initialized_ = true;
    
    // 初始化 I2S 扬声器
    if (InitializeI2sSpeaker() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S speaker");
        return;
    }
    i2s_initialized_ = true;
    
    // 打开 USB 麦克风
    if (OpenUsbMicrophone() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open USB microphone, continuing with I2S output only");
        usb_microphone_ready_ = false;
    } else {
        usb_microphone_ready_ = true;
    }
    
    // 启用输入输出
    input_enabled_ = true;
    output_enabled_ = true;
    
    ESP_LOGI(TAG, "Hybrid USB-I2S Audio codec started successfully");
}

void HybridUsbI2sCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    input_enabled_ = enable;
    ESP_LOGI(TAG, "Set USB microphone input enable to %s", enable ? "true" : "false");
}

void HybridUsbI2sCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    output_enabled_ = enable;
    
    if (i2s_tx_handle_ != nullptr) {
        if (enable) {
            i2s_channel_enable(i2s_tx_handle_);
        } else {
            i2s_channel_disable(i2s_tx_handle_);
        }
    }
    
    ESP_LOGI(TAG, "Set I2S speaker output enable to %s", enable ? "true" : "false");
}

void HybridUsbI2sCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGI(TAG, "Set hybrid output volume to %d", output_volume_);
}

int HybridUsbI2sCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || !usb_microphone_ready_ || uac_rx_device_ == nullptr) {
        return 0;
    }
    
    uint32_t bytes_read = 0;
    esp_err_t ret = uac_host_device_read(uac_rx_device_, (uint8_t*)dest, samples * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(10));
    if (ret == ESP_OK && bytes_read > 0) {
        return bytes_read / sizeof(int16_t);
    }
    return 0;
}

int HybridUsbI2sCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_ || i2s_tx_handle_ == nullptr) {
        return 0;
    }
    
    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(i2s_tx_handle_, data, samples * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(10));
    if (ret == ESP_OK && bytes_written > 0) {
        return bytes_written / sizeof(int16_t);
    }
    return 0;
}

// USB 相关实现
void HybridUsbI2sCodec::UacDriverEventCallback(uint8_t addr, uint8_t iface_num, 
                                             const uac_host_driver_event_t event, void* arg) {
    auto codec = reinterpret_cast<HybridUsbI2sCodec*>(arg);
    
    switch (event) {
        case UAC_HOST_DRIVER_EVENT_RX_CONNECTED:
            ESP_LOGI(TAG, "UAC RX device connected - Addr: %d, Iface: %d", addr, iface_num);
            codec->usb_device_addr_ = addr;
            codec->usb_iface_num_ = iface_num;
            if (codec->usb_event_group_ != nullptr) {
                xEventGroupSetBits(codec->usb_event_group_, USB_EVENT_CONNECTED);
            }
            break;
            
        case UAC_HOST_DRIVER_EVENT_TX_CONNECTED:
            ESP_LOGI(TAG, "UAC TX device connected (ignored for microphone-only mode)");
            break;
    }
}

void HybridUsbI2sCodec::UacDeviceEventCallback(uac_host_device_handle_t uac_device_handle,
                                             const uac_host_device_event_t event, void* arg) {
    auto codec = reinterpret_cast<HybridUsbI2sCodec*>(arg);
    
    switch (event) {
        case UAC_HOST_DEVICE_EVENT_RX_DONE:
            // 数据已准备好，Read 方法会处理
            break;
            
        case UAC_HOST_DEVICE_EVENT_TX_DONE:
            // TX 事件，但我们只使用 RX（麦克风），所以忽略
            break;
            
        case UAC_HOST_DRIVER_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "USB audio device disconnected");
            codec->usb_microphone_ready_ = false;
            break;
            
        case UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR:
            ESP_LOGE(TAG, "USB audio transfer error");
            break;
    }
}

esp_err_t HybridUsbI2sCodec::InitializeUsbHost() {
    ESP_LOGI(TAG, "Initializing USB Host with enhanced debug logging...");
    
    // 设置 USB Host 日志级别为 DEBUG
    // esp_log_level_set("uac-host", ESP_LOG_DEBUG);
    // esp_log_level_set("usb", ESP_LOG_DEBUG);
    // esp_log_level_set("usb_host", ESP_LOG_DEBUG);
    
    // 安装 USB Host
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t ret = usb_host_install(&host_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "USB Host installed successfully");
    
    // 创建 USB Host 事件处理任务
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        usb_host_task,
        "usb_host_task",
        4096,
        nullptr,
        5,
        &usb_host_task_handle_,
        0
    );
    if (task_ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create USB host task");
        usb_host_uninstall();
        return ESP_FAIL;
    }
    
    // 安装 UAC Host 驱动（UAC 驱动会自动注册客户端）
    const uac_host_driver_config_t uac_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,  // 使用核心 0
        .callback = UacDriverEventCallback,
        .callback_arg = this,
    };
    ret = uac_host_install(&uac_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UAC host driver: %s", esp_err_to_name(ret));
        // 清理资源
        if (usb_host_task_handle_) {
            vTaskDelete(usb_host_task_handle_);
            usb_host_task_handle_ = nullptr;
        }
        usb_host_uninstall();
        return ret;
    }
    
    ESP_LOGI(TAG, "USB Host and UAC driver initialized");
    usb_initialized_ = true;
    return ESP_OK;
}

// 辅助函数：根据设备能力选择最佳采样率
uint32_t HybridUsbI2sCodec::SelectBestSampleRate(const uac_host_dev_alt_param_t& alt_params) {
    if (alt_params.sample_freq_type == 0) {
        // 连续采样率范围 - 选择我们期望的 16kHz，但如果超出范围则选择边界值
        if (alt_params.sample_freq_lower <= 16000 && 16000 <= alt_params.sample_freq_upper) {
            return 16000;
        } else if (alt_params.sample_freq_lower > 16000) {
            return alt_params.sample_freq_lower;
        } else {
            return alt_params.sample_freq_upper;
        }
    } else {
        // 离散采样率列表 - 优先选择最接近 16kHz 的采样率
        uint32_t best_rate = alt_params.sample_freq[0];  // 默认选择第一个（通常是最高质量）
        int32_t best_diff = abs((int32_t)alt_params.sample_freq[0] - 16000);
        
        for (int i = 0; i < alt_params.sample_freq_type && i < UAC_FREQ_NUM_MAX; i++) {
            int32_t diff = abs((int32_t)alt_params.sample_freq[i] - 16000);
            if (diff < best_diff) {
                best_diff = diff;
                best_rate = alt_params.sample_freq[i];
            }
        }
        
        // 如果最佳匹配仍然差距很大（>4kHz），则使用第一个采样率以保证音质
        if (best_diff > 4000) {
            best_rate = alt_params.sample_freq[0];
        }
        
        return best_rate;
    }
}

esp_err_t HybridUsbI2sCodec::OpenUsbMicrophone() {
    if (!usb_initialized_ || uac_rx_device_ != nullptr) {
        return ESP_FAIL;
    }
    
    // 等待 USB 设备连接（最多 5 秒）
    if (!WaitForUsbDevice(5000)) {
        ESP_LOGW(TAG, "No USB audio device detected within timeout");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "Opening USB microphone with dynamic sample rate detection...");
    
    // 设置 UAC 设备配置
    const uac_host_device_config_t dev_config = {
        .addr = usb_device_addr_,           // USB 设备地址
        .iface_num = usb_iface_num_,        // USB 接口号
        .buffer_size = 8192*10,                // 增加缓冲区大小以避免溢出（48kHz 需要更大缓冲区）
        .buffer_threshold = 4096*10,           // 相应增加缓冲区阈值
        .callback = UacDeviceEventCallback, // 事件回调
        .callback_arg = this,               // 回调参数
    };
    
    esp_err_t ret = uac_host_device_open(&dev_config, &uac_rx_device_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open UAC RX device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 获取设备支持的音频格式参数
    uac_host_dev_alt_param_t alt_params;
    ret = uac_host_get_device_alt_param(uac_rx_device_, 1, &alt_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device alternate parameters: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_rx_device_);
        uac_rx_device_ = nullptr;
        return ret;
    }
    
    // 打印设备支持的所有采样率
    ESP_LOGI(TAG, "USB device supports the following sample rates:");
    if (alt_params.sample_freq_type > 0) {
        // 离散采样率列表
        for (int i = 0; i < alt_params.sample_freq_type && i < UAC_FREQ_NUM_MAX; i++) {
            ESP_LOGI(TAG, "  - %" PRIu32 " Hz", alt_params.sample_freq[i]);
        }
    } else {
        // 连续采样率范围
        ESP_LOGI(TAG, "  - Continuous range: %" PRIu32 " - %" PRIu32 " Hz", 
                 alt_params.sample_freq_lower, alt_params.sample_freq_upper);
    }
    
    ESP_LOGI(TAG, "Device capabilities - Channels: %d, Bit Resolution: %d, Format Type: %d",
             alt_params.channels, alt_params.bit_resolution, alt_params.format);
    
    // 智能选择最佳采样率
    uint32_t selected_sample_rate = SelectBestSampleRate(alt_params);
    ESP_LOGI(TAG, "Selected optimal sample rate: %" PRIu32 " Hz", selected_sample_rate);
    
    // 配置流参数
    const uac_host_stream_config_t stream_config = {
        .channels = alt_params.channels,           // 使用设备实际通道数
        .bit_resolution = alt_params.bit_resolution, // 使用设备实际位深度
        .sample_freq = selected_sample_rate,       // 使用动态选择的采样率
        .flags = 0,
    };
    
    // 启动 UAC 设备
    ret = uac_host_device_start(uac_rx_device_, &stream_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UAC RX stream: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_rx_device_);
        uac_rx_device_ = nullptr;
        return ret;
    }
    
    // 更新内部采样率以匹配实际设备
    input_sample_rate_ = selected_sample_rate;
    
    usb_microphone_ready_ = true;
    ESP_LOGI(TAG, "USB microphone opened and started successfully at %" PRIu32 "Hz!", selected_sample_rate);
    return ESP_OK;
}

esp_err_t HybridUsbI2sCodec::CloseUsbMicrophone() {
    if (uac_rx_device_ != nullptr) {
        uac_host_device_stop(uac_rx_device_);
        uac_host_device_close(uac_rx_device_);
        uac_rx_device_ = nullptr;
        usb_microphone_ready_ = false;
        ESP_LOGI(TAG, "USB microphone closed");
    }
    
    return ESP_OK;
}

// I2S 相关实现
esp_err_t HybridUsbI2sCodec::InitializeI2sSpeaker() {
    // 创建 I2S 发送通道
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)1, I2S_ROLE_MASTER);
    tx_chan_cfg.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    tx_chan_cfg.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    tx_chan_cfg.auto_clear_after_cb = true;
    tx_chan_cfg.auto_clear_before_cb = false;
    tx_chan_cfg.intr_priority = 0;
    esp_err_t ret = i2s_new_channel(&tx_chan_cfg, &i2s_tx_handle_, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S TX channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 配置 I2S 标准模式
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk_,
            .ws = spk_ws_,
            .dout = spk_dout_,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ret = i2s_channel_init_std_mode(i2s_tx_handle_, &tx_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S TX standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(i2s_tx_handle_);
        i2s_tx_handle_ = nullptr;
        return ret;
    }
    
    ESP_LOGI(TAG, "I2S speaker initialized successfully - Sample Rate: %dHz", output_sample_rate_);
    return ESP_OK;
}

esp_err_t HybridUsbI2sCodec::CloseI2sSpeaker() {
    if (i2s_tx_handle_ != nullptr) {
        i2s_channel_disable(i2s_tx_handle_);
        i2s_del_channel(i2s_tx_handle_);
        i2s_tx_handle_ = nullptr;
        i2s_initialized_ = false;
        ESP_LOGI(TAG, "I2S speaker closed");
    }
    
    return ESP_OK;
}

bool HybridUsbI2sCodec::WaitForUsbDevice(int timeout_ms) {
    if (!usb_initialized_) {
        ESP_LOGE(TAG, "USB not initialized");
        return false;
    }
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    int last_log_second = -1;
    
    ESP_LOGI(TAG, "Waiting for USB audio device connection (%d seconds timeout)...", timeout_ms / 1000);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        int current_second = (xTaskGetTickCount() - start_time) / configTICK_RATE_HZ;
        if (current_second > last_log_second) {
            last_log_second = current_second;
            int remaining_seconds = (timeout_ticks - (xTaskGetTickCount() - start_time)) / configTICK_RATE_HZ;
            
            // 检查是否有任何 USB 设备被枚举
            uint8_t dev_addr_list[8];
            int num_devices = 0;
            esp_err_t ret = usb_host_device_addr_list_fill(8, dev_addr_list, &num_devices);
            if (ret == ESP_OK && num_devices > 0) {
                ESP_LOGW(TAG, "Found %d USB device(s) enumerated:", num_devices);
                for (int i = 0; i < num_devices; i++) {
                    ESP_LOGW(TAG, "  Device %d: Address %d", i, dev_addr_list[i]);
                    // 尝试获取设备描述符
                    usb_device_handle_t dev_handle;
                    ret = usb_host_device_open(usb_client_handle_, dev_addr_list[i], &dev_handle);
                    if (ret == ESP_OK) {
                        const usb_device_desc_t *desc;
                        ret = usb_host_get_device_descriptor(dev_handle, &desc);
                        if (ret == ESP_OK) {
                            ESP_LOGW(TAG, "    VID: 0x%04X, PID: 0x%04X, Class: 0x%02X", 
                                    desc->idVendor, desc->idProduct, desc->bDeviceClass);
                        }
                        usb_host_device_close(usb_client_handle_, dev_handle);

                    }
                }
                return true;
            }

            ESP_LOGW(TAG, "No USB audio device found yet. Elapsed time: %d seconds, remaining: %d seconds", 
                    current_second, remaining_seconds);
        }
        
        if (usb_microphone_ready_) {
            ESP_LOGI(TAG, "USB microphone detected and ready!");
            return true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 每100ms检查一次
    }
    
    ESP_LOGW(TAG, "No USB audio device found within %dms", timeout_ms);
    return false;
}
