#include "hybrid_usb_i2s_codec.h"
#include "board.h"

#include <esp_log.h>
#include <cstring>
#include <usb/usb_host.h>

#define TAG "HybridUsbI2sCodec"

// 启用更详细的 USB Host 日志
#define USB_HOST_LOG_LEVEL ESP_LOG_DEBUG
#define UAC_HOST_LOG_LEVEL ESP_LOG_DEBUG

// 添加 USB 客户端事件处理
static void usb_client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg) {
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGD(TAG, "USB_CLIENT_EVENT_NEW_DEV: Device connected - Address: %d", event_msg->new_dev.address);
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGD(TAG, "USB_CLIENT_EVENT_DEV_GONE: Device disconnected - Handle: %p", event_msg->dev_gone.dev_hdl);
            break;
        default:
            ESP_LOGD(TAG, "USB_CLIENT_EVENT: Unknown event %d", event_msg->event);
            break;
    }
}

HybridUsbI2sCodec::HybridUsbI2sCodec(int input_sample_rate, int output_sample_rate, 
                                   gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout)
    : spk_bclk_(spk_bclk), spk_ws_(spk_ws), spk_dout_(spk_dout), output_sample_rate_(output_sample_rate) {
    
    // 设置基类属性
    duplex_ = true;
    input_reference_ = false;
    input_enabled_ = false;
    output_enabled_ = false;
    input_sample_rate_ = input_sample_rate;
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
    
    ESP_LOGI(TAG, "HybridUsbI2sCodec created - Input: %dHz (USB), Output: %dHz (I2S)", 
             input_sample_rate_, output_sample_rate_);
}

HybridUsbI2sCodec::~HybridUsbI2sCodec() {
    // 关闭 USB 麦克风
    CloseUsbMicrophone();
    
    // 关闭 I2S 扬声器
    CloseI2sSpeaker();
    
    // 删除事件组
    if (usb_event_group_ != nullptr) {
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
    esp_log_level_set("uac-host", ESP_LOG_DEBUG);
    esp_log_level_set("usb", ESP_LOG_DEBUG);
    esp_log_level_set("usb_host", ESP_LOG_DEBUG);
    
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
    
    // 注册 USB 客户端以接收设备事件
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_client_event_callback,
            .callback_arg = nullptr,
        },
    };
    ret = usb_host_client_register(&client_config, &usb_client_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register USB client: %s", esp_err_to_name(ret));
        usb_host_uninstall();
        return ret;
    }
    
    // 安装 UAC Host 驱动
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
        usb_host_client_deregister(usb_client_handle_);
        usb_host_uninstall();
        return ret;
    }
    
    ESP_LOGI(TAG, "USB Host and UAC driver initialized");
    return ESP_OK;
}

esp_err_t HybridUsbI2sCodec::OpenUsbMicrophone() {
    if (!usb_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Waiting for USB audio device connection...");
    
    // 等待设备连接
    EventBits_t bits = xEventGroupWaitBits(
        usb_event_group_,
        USB_EVENT_CONNECTED,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(10000)
    );
    
    if (!(bits & USB_EVENT_CONNECTED)) {
        ESP_LOGW(TAG, "No USB audio device found within timeout");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "USB audio device detected!");
    
    // 配置并打开 RX 流（麦克风）
    uac_host_device_config_t config = {};
    config.addr = usb_device_addr_;
    config.iface_num = usb_iface_num_;
    config.buffer_size = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t);
    config.buffer_threshold = AUDIO_CODEC_DMA_FRAME_NUM * sizeof(int16_t) / 2;
    config.callback = UacDeviceEventCallback;
    config.callback_arg = this;
    
    esp_err_t ret = uac_host_device_open(&config, &uac_rx_device_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open UAC device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 启动 RX 流
    uac_host_stream_config_t stream_config = {
        .channels = 1,
        .bit_resolution = 16,
        .sample_freq = (uint32_t)input_sample_rate_,
        .flags = 0,
    };
    
    ret = uac_host_device_start(uac_rx_device_, &stream_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UAC RX stream: %s", esp_err_to_name(ret));
        uac_host_device_close(uac_rx_device_);
        uac_rx_device_ = nullptr;
        return ret;
    }
    
    ESP_LOGI(TAG, "USB microphone opened successfully - Sample Rate: %dHz", input_sample_rate_);
    return ESP_OK;
}

void HybridUsbI2sCodec::CloseUsbMicrophone() {
    if (uac_rx_device_ != nullptr) {
        uac_host_device_stop(uac_rx_device_);
        uac_host_device_close(uac_rx_device_);
        uac_rx_device_ = nullptr;
        usb_microphone_ready_ = false;
        ESP_LOGI(TAG, "USB microphone closed");
    }
    
    if (usb_initialized_) {
        uac_host_uninstall();
        usb_host_uninstall();
        usb_initialized_ = false;
        ESP_LOGI(TAG, "USB Host uninstalled");
    }
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

void HybridUsbI2sCodec::CloseI2sSpeaker() {
    if (i2s_tx_handle_ != nullptr) {
        i2s_channel_disable(i2s_tx_handle_);
        i2s_del_channel(i2s_tx_handle_);
        i2s_tx_handle_ = nullptr;
        i2s_initialized_ = false;
        ESP_LOGI(TAG, "I2S speaker closed");
    }
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
            esp_err_t ret = usb_host_device_addr_list_fill(dev_addr_list, sizeof(dev_addr_list), &num_devices);
            if (ret == ESP_OK && num_devices > 0) {
                ESP_LOGW(TAG, "Found %d USB device(s) enumerated:", num_devices);
                for (int i = 0; i < num_devices; i++) {
                    ESP_LOGW(TAG, "  Device %d: Address %d", i, dev_addr_list[i]);
                    // 尝试获取设备描述符
                    usb_device_handle_t dev_handle;
                    ret = usb_host_device_open(usb_client_handle_, dev_addr_list[i], &dev_handle);
                    if (ret == ESP_OK) {
                        usb_device_desc_t desc;
                        ret = usb_host_get_device_descriptor(dev_handle, &desc);
                        if (ret == ESP_OK) {
                            ESP_LOGW(TAG, "    VID: 0x%04X, PID: 0x%04X, Class: 0x%02X", 
                                    desc.idVendor, desc.idProduct, desc.bDeviceClass);
                        }
                        usb_host_device_close(usb_client_handle_, dev_handle);
                    }
                }
            } else {
                ESP_LOGW(TAG, "No USB audio device found yet. Elapsed time: %d seconds, remaining: %d seconds", 
                        current_second, remaining_seconds);
            }
        }
        
        if (usb_microphone_ready_) {
            ESP_LOGI(TAG, "USB microphone detected and ready!");
            return true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 每100ms检查一次
        esp_task_wdt_reset(); // 喂狗防止看门狗复位
    }
    
    ESP_LOGW(TAG, "No USB audio device found within %dms", timeout_ms);
    return false;
}
