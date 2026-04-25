#include "hybrid_usb_i2s_codec.h"
#include "board.h"

#include <esp_log.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <usb/usb_host.h>
#include <inttypes.h>  // 添加inttypes.h头文件以支持PRI宏

#define TAG "HybridUsbI2sCodec"

// 启用更详细的 USB Host 日志
// #define USB_HOST_LOG_LEVEL ESP_LOG_DEBUG
// #define UAC_HOST_LOG_LEVEL ESP_LOG_DEBUG

// USB Host 事件处理任务
static void usb_host_task(void *arg) {
    ESP_LOGI("USB_HOST", "USB Host task started");
    
    while (true) {
        uint32_t event_flags;
        esp_err_t ret = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (ret != ESP_OK) {
            // 特别处理设备描述符读取错误
            if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE("USB_HOST", "USB device enumeration failed - likely hardware issue");
                ESP_LOGE("USB_HOST", "Check: USB cable (data capable), power supply, device compatibility");
            } else if (ret == ESP_ERR_INVALID_RESPONSE) {
                ESP_LOGE("USB_HOST", "Invalid USB response received - device may not be UAC compliant");
            } else {
                ESP_LOGE("USB_HOST", "usb_host_lib_handle_events failed: %s", esp_err_to_name(ret));
            }
            
            // 不立即退出，给用户时间看到错误信息
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }
        
        // 检查是否需要退出任务
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI("USB_HOST", "No more USB clients, exiting USB host task");
            break;
        }
        
        // 添加周期性状态检查
        static TickType_t last_status_check = 0;
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_status_check) > pdMS_TO_TICKS(5000)) {
            last_status_check = current_time;
            
            // 检查USB设备列表
            uint8_t dev_addr_list[8];
            int num_devices = 0;
            esp_err_t check_ret = usb_host_device_addr_list_fill(8, dev_addr_list, &num_devices);
            if (check_ret == ESP_OK) {
                ESP_LOGD("USB_HOST", "USB devices present: %d", num_devices);
            }
        }
    }
    
    ESP_LOGI("USB_HOST", "USB Host task exiting");
    vTaskDelete(NULL);
}

HybridUsbI2sCodec::HybridUsbI2sCodec(int output_sample_rate,
                                   gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout)
    : spk_bclk_(spk_bclk), spk_ws_(spk_ws), spk_dout_(spk_dout) {

    // 设置基类属性 - 输入采样率初始化为 0，表示尚未确定
    duplex_ = true;
    input_reference_ = false;
    input_enabled_ = false;
    output_enabled_ = false;
    input_sample_rate_ = 48000;  // 动态确定
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

    // 初始化异步数据处理相关变量
    usb_host_task_handle_ = nullptr;
    usb_data_task_handle_ = nullptr;
    buffer_capacity_ = USB_AUDIO_BUFFER_SIZE / sizeof(int16_t);
    buffer_overflowed_ = false;

    // 初始化统计信息
    usb_read_count_ = 0;
    usb_overflow_count_ = 0;
    samples_processed_ = 0;

    ESP_LOGI(TAG, "HybridUsbI2sCodec created - Input: Dynamic (USB), Output: %dHz (I2S)",
             output_sample_rate_);
}

HybridUsbI2sCodec::~HybridUsbI2sCodec() {
    ESP_LOGI(TAG, "Destroying HybridUsbI2sCodec with direct callback reading mode...");

    // 关闭 USB 麦克风
    CloseUsbMicrophone();

    // 关闭 I2S 扬声器
    CloseI2sSpeaker();

    // 清理 USB Host 资源
    if (usb_initialized_) {
        // 等待 USB 数据处理任务退出
        if (usb_data_task_handle_) {
            vTaskDelete(usb_data_task_handle_);
            usb_data_task_handle_ = nullptr;
        }

        // 卸载 UAC 驱动（UAC 驱动会自动处理客户端注销）
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

    // 打印统计信息
    ESP_LOGI(TAG, "USB statistics - Reads: %" PRIu32 ", Overflows: %" PRIu32 ", Samples: %" PRIu32,
             usb_read_count_, usb_overflow_count_, samples_processed_);

    ESP_LOGI(TAG, "HybridUsbI2sCodec destroyed");
}

void HybridUsbI2sCodec::Start() {
    ESP_LOGI(TAG, "Starting Hybrid USB-I2S Audio codec with async event-driven mode...");

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

    // 只有在USB麦克风就绪时才启用输入
    if (usb_microphone_ready_) {
        input_enabled_ = true;
        ESP_LOGI(TAG, "USB microphone input enabled with direct callback reading mode");

        // 立即执行USB设备状态检查，验证初始化是否成功
        ESP_LOGI(TAG, "Performing initial USB device status check...");
        CheckUsbDeviceStatus();

        // 创建USB监控任务（统计和状态监控）
        BaseType_t ret = xTaskCreatePinnedToCore(
            UsbDataProcessingTask,
            "usb_monitor_task",
            4096,  // 监控任务只需要4KB堆栈
            this,
            10,  // 中等优先级，用于监控统计
            &usb_data_task_handle_,
            0   // 核心0
        );

        if (ret != pdTRUE) {
            ESP_LOGW(TAG, "Failed to create USB monitor task (statistics will be limited)");
            usb_data_task_handle_ = nullptr;
        } else {
            ESP_LOGI(TAG, "USB monitor task created for statistics and monitoring");
        }

    } else {
        input_enabled_ = false;
        ESP_LOGW(TAG, "USB microphone not connected, input disabled");
    }

    // 启用输出
    if (i2s_tx_handle_ != nullptr) {
        output_enabled_ = true;
        ESP_LOGI(TAG, "I2S speaker output enabled");
        // 确保通道被启用
        i2s_channel_enable(i2s_tx_handle_);
        ESP_LOGI(TAG, "I2S channel enabled");
    } else {
        output_enabled_ = false;
        ESP_LOGW(TAG, "I2S speaker not initialized, output disabled");
    }

    ESP_LOGI(TAG, "Hybrid USB-I2S Audio codec started with async mode - Input: %s, Output: %s",
             input_enabled_ ? "enabled" : "disabled", output_enabled_ ? "enabled" : "disabled");
}

void HybridUsbI2sCodec::EnableInput(bool enable) {
    // USB麦克风输入无法真正关闭（ringbuffer持续接收数据），始终视为启用状态
    // 忽略enable参数，始终保持input_enabled_为true
    if (!input_enabled_) {
        input_enabled_ = true;
        ESP_LOGI(TAG, "USB microphone input is always enabled (cannot be disabled)");
    }
    // 如果尝试禁用，记录警告但不执行
    if (!enable) {
        ESP_LOGW(TAG, "Attempt to disable USB microphone input ignored - USB input cannot be truly disabled");
    }
}

void HybridUsbI2sCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    output_enabled_ = enable;
    
    if (enable && i2s_tx_handle_ != nullptr) {
        // 启用I2S输出
        i2s_channel_enable(i2s_tx_handle_);
        ESP_LOGI(TAG, "I2S speaker output enabled");
    } else if (!enable && i2s_tx_handle_ != nullptr) {
        // 禁用I2S输出
        i2s_channel_disable(i2s_tx_handle_);
        ESP_LOGI(TAG, "I2S speaker output disabled");
    }
}

void HybridUsbI2sCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGI(TAG, "Set hybrid output volume to %d", output_volume_);
}

int HybridUsbI2sCodec::Read(int16_t* dest, int samples) {
    // 添加读取时间戳用于计算间隔
    static TickType_t last_read_time = xTaskGetTickCount();
    TickType_t current_read_time = xTaskGetTickCount();
    uint32_t time_diff_ms = (current_read_time - last_read_time) * portTICK_PERIOD_MS;
    last_read_time = current_read_time;
    
    static uint32_t read_call_count = 0;
    static uint32_t empty_buffer_count = 0;
    static uint32_t last_warning_time = 0;

    read_call_count++;

    if (!input_enabled_ || !usb_microphone_ready_ || uac_rx_device_ == nullptr) {
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_warning_time) > pdMS_TO_TICKS(5000)) {  // 每5秒警告一次
            ESP_LOGW(TAG, "Read() called but not ready - Input: %s, USB ready: %s, Device: %p",
                    input_enabled_ ? "YES" : "NO",
                    usb_microphone_ready_ ? "YES" : "NO",
                    uac_rx_device_);
            last_warning_time = current_time;
        }
        return 0;
    }

    if (dest == nullptr || samples <= 0) {
        ESP_LOGW(TAG, "Invalid read parameters: dest=%p, samples=%d", dest, samples);
        return 0;
    }

    // 直接从循环缓冲区读取数据（不再等待信号，信号由底层任务处理）
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    if (usb_audio_buffer_.empty()) {
        // 缓冲区为空，返回0（让上层短暂休眠后重试）
        empty_buffer_count++;

        if ((empty_buffer_count % 100) == 0) {  // 每100次空缓冲区读取向警告一次
            ESP_LOGD(TAG, "Read() returning empty buffer for %" PRIu32 " time (Total calls: %" PRIu32 ", USB reads: %" PRIu32 ")",
                    empty_buffer_count, read_call_count, usb_read_count_);
        }
        return 0;
    }

    size_t available_samples = std::min((size_t)samples, usb_audio_buffer_.size());
    size_t samples_read = 0;

    // 从缓冲区前端读取数据
    for (size_t i = 0; i < available_samples; i++) {
        dest[i] = usb_audio_buffer_.front();
        usb_audio_buffer_.pop_front();
        samples_read++;
    }

    // 定期日志记录成功读取（每100次）
    if ((read_call_count % 100) == 0 && samples_read > 0) {
        ESP_LOGD(TAG, "Read() successful - Call: %" PRIu32 ", Samples: %d, Buffer size: %zu",
                 read_call_count, samples_read, usb_audio_buffer_.size());
    }

    // 检查缓冲区溢出状态
    if (buffer_overflowed_) {
        static int overflow_warning_count = 0;
        if (overflow_warning_count++ < 5) {  // 只警告5次
            ESP_LOGD(TAG, "Buffer overflow detected! Audio data may have been lost. Total overflows: %" PRIu32,
                     usb_overflow_count_);
        }
        buffer_overflowed_ = false;  // 重置溢出标志
    }

    // 更新统计信息
    samples_processed_ += samples_read;

    // 添加详细的读取监控日志（每50次读取输出一次）
    static int read_log_count = 0;
    if (++read_log_count >= 50) {
        read_log_count = 0;
        ESP_LOGI(TAG, "USB_INPUT_MONITOR: Interval=%" PRIu32 "ms, Requested=%d samples, Actual=%d samples, BufferSize=%u", 
                 time_diff_ms, samples, (int)samples_read, (unsigned int)usb_audio_buffer_.size());
    }

    return samples_read;
}

int HybridUsbI2sCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_ || i2s_tx_handle_ == nullptr) {
        if (!output_enabled_) {
            ESP_LOGD(TAG, "I2S output not enabled");
        }
        if (i2s_tx_handle_ == nullptr) {
            ESP_LOGW(TAG, "I2S TX handle is null");
        }
        return 0;
    }

    ESP_LOGD(TAG, "Writing %d samples to I2S at %dHz", samples, output_sample_rate_);

    std::vector<int32_t> buffer(samples);
    int32_t volume_factor = static_cast<int32_t>(pow(double(output_volume_) / 100.0, 2) * 65536);
    for (int i = 0; i < samples; ++i) {
        int64_t temp = static_cast<int64_t>(data[i]) * volume_factor;
        if (temp > INT32_MAX) {
            buffer[i] = INT32_MAX;
        } else if (temp < INT32_MIN) {
            buffer[i] = INT32_MIN;
        } else {
            buffer[i] = static_cast<int32_t>(temp);
        }
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(i2s_tx_handle_, buffer.data(), samples * sizeof(int32_t), &bytes_written, pdMS_TO_TICKS(100));
    if (ret == ESP_OK && bytes_written > 0) {
        int samples_written = bytes_written / sizeof(int32_t);
        ESP_LOGD(TAG, "Successfully wrote %d samples to I2S", samples_written);
        return samples_written;
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGD(TAG, "I2S write timeout, samples: %d", samples);
        return 0;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write error: %s, bytes_written: %zu", esp_err_to_name(ret), bytes_written);
        return 0;
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
            ESP_LOGD(TAG, "RX_DONE event triggered - Device ready: %s, Input enabled: %s, Bit depth: %d",
                    codec->usb_microphone_ready_ ? "YES" : "NO",
                    codec->input_enabled_ ? "YES" : "NO",
                    codec->device_bit_depth_);

            // 修复：直接在回调中读取USB数据（参考官方示例）
            // 避免通过队列延迟导致的读取失败问题
            if (codec->uac_rx_device_ != nullptr && codec->input_enabled_) {
                ESP_LOGD(TAG, "Starting USB data read in callback...");

                // 使用成员持有的 scratch，避免每次回调 malloc/free（OpenUsbMicrophone 已预分配）
                if (codec->rx_scratch_.empty()) {
                    ESP_LOGE(TAG, "rx_scratch_ not initialized, skipping callback read");
                    return;
                }
                uint8_t* temp_buffer = codec->rx_scratch_.data();
                const size_t temp_read_size = codec->rx_scratch_.size();
                uint32_t bytes_read = 0;

                // 使用0超时，立即读取（与官方示例一致）
                esp_err_t ret = uac_host_device_read(codec->uac_rx_device_, temp_buffer,
                                                  temp_read_size, &bytes_read, 0);

                ESP_LOGD(TAG, "USB read result: %s, Bytes read: %" PRIu32,
                         esp_err_to_name(ret), bytes_read);

                if (ret == ESP_OK && bytes_read > 0) {
                    // 根据设备位深计算实际样本数
                    size_t samples_read = 0;
                    if (codec->device_bit_depth_ == 16) {
                        samples_read = bytes_read / sizeof(int16_t);
                    } else if (codec->device_bit_depth_ == 24) {
                        samples_read = bytes_read / 3; // 24-bit = 3 bytes per sample
                    } else {
                        // 未知位深，按16-bit处理
                        samples_read = bytes_read / sizeof(int16_t);
                        ESP_LOGW(TAG, "Unknown bit depth %d, processing as 16-bit", codec->device_bit_depth_);
                    }
                    
                    ESP_LOGD(TAG, "Samples read: %zu (bit depth: %d), Buffer size before: %zu",
                            samples_read, codec->device_bit_depth_, codec->usb_audio_buffer_.size());

                    // 打印原始数据以验证是否为LRLR格式（仅打印前16个样本用于调试）
                    if (codec->device_bit_depth_ == 16 && samples_read >= 8) {
                        const int16_t* raw_samples = reinterpret_cast<const int16_t*>(temp_buffer);
                        ESP_LOGD(TAG, "RAW USB DATA (first 8 samples, LRLRLRLR format):");
                        for (int i = 0; i < 8; i++) {
                            ESP_LOGD(TAG, "  Sample[%d] = %d", i, raw_samples[i]);
                        }
                        // 如果是立体声，显示左右声道分离
                        if (codec->input_channels_ > 1) {
                            ESP_LOGD(TAG, "STEREO CHANNELS (assuming LRLR):");
                            ESP_LOGD(TAG, "  Left:  [%d, %d, %d, %d]", 
                                    raw_samples[0], raw_samples[2], raw_samples[4], raw_samples[6]);
                            ESP_LOGD(TAG, "  Right: [%d, %d, %d, %d]", 
                                    raw_samples[1], raw_samples[3], raw_samples[5], raw_samples[7]);
                        }
                    }

                    // 将数据添加到应用缓冲区（统一转换为16-bit）
                    std::lock_guard<std::mutex> lock(codec->buffer_mutex_);

                    // 关键修改：如果是立体声（2通道），只保留左声道（偶数索引）的数据
                    size_t actual_samples_to_add = samples_read;
                    if (codec->usb_channels_ > 1) {
                        // 立体声模式：只取左声道（每2个样本取1个）
                        actual_samples_to_add = samples_read / 2;
                    }

                    size_t free_space = codec->buffer_capacity_ - codec->usb_audio_buffer_.size();

                    // 缓冲区不够：先丢弃旧数据腾空间，再按当前 free_space 重新算 samples_to_add
                    if (actual_samples_to_add > free_space) {
                        codec->buffer_overflowed_ = true;
                        codec->usb_overflow_count_++;

                        size_t keep_size = codec->buffer_capacity_ / 2;
                        ESP_LOGD(TAG, "Buffer overflow! Dropping old data, keeping %u samples",
                                (unsigned int)keep_size);

                        while (codec->usb_audio_buffer_.size() > keep_size) {
                            codec->usb_audio_buffer_.pop_front();
                        }
                        free_space = codec->buffer_capacity_ - codec->usb_audio_buffer_.size();
                    }

                    size_t samples_to_add = std::min(actual_samples_to_add, free_space);

                    ESP_LOGD(TAG, "Buffer capacity: %zu, Free space: %zu, Samples to add: %zu (original: %zu)",
                            codec->buffer_capacity_, free_space, samples_to_add, samples_read);

                    // 转换并添加新数据（统一为16-bit），同时处理立体声到单声道的转换
                    if (samples_to_add > 0) {
                        std::vector<int16_t> converted_samples(samples_to_add);
                        
                        if (codec->device_bit_depth_ == 16) {
                            // 16-bit设备，直接复制
                            const int16_t* src_samples = reinterpret_cast<const int16_t*>(temp_buffer);
                            if (codec->usb_channels_ == 1) {
                                // 单声道：直接复制所有样本
                                for (size_t i = 0; i < samples_to_add; i++) {
                                    converted_samples[i] = src_samples[i];
                                }
                            } else {
                                // 立体声：只取左声道（偶数索引：0, 2, 4, 6...）
                                for (size_t i = 0; i < samples_to_add; i++) {
                                    converted_samples[i] = src_samples[i * 2]; // 取偶数索引
                                }
                            }
                        } else if (codec->device_bit_depth_ == 24) {
                            // 24-bit设备，转换为16-bit
                            // 24-bit音频数据通常以3字节小端序存储
                            if (codec->usb_channels_ == 1) {
                                // 单声道24-bit
                                for (size_t i = 0; i < samples_to_add; i++) {
                                    // 从3字节24-bit数据中提取值
                                    uint32_t sample_24bit = 0;
                                    sample_24bit |= ((uint32_t)temp_buffer[i * 3 + 0]) << 0;   // LSB
                                    sample_24bit |= ((uint32_t)temp_buffer[i * 3 + 1]) << 8;   // Middle
                                    sample_24bit |= ((uint32_t)temp_buffer[i * 3 + 2]) << 16;  // MSB (sign bit)
                                    
                                    // 处理符号位（24-bit有符号整数）
                                    if (sample_24bit & 0x800000) { // 如果最高位是1（负数）
                                        sample_24bit |= 0xFF000000; // 扩展符号位到32位
                                    }
                                    
                                    // 转换为16-bit（右移8位，保留高16位）
                                    int16_t sample_16bit = (int16_t)(sample_24bit >> 8);
                                    converted_samples[i] = sample_16bit;
                                }
                            } else {
                                // 立体声24-bit：只取左声道
                                for (size_t i = 0; i < samples_to_add; i++) {
                                    // 左声道在位置 i*2*3 = i*6
                                    uint32_t sample_24bit = 0;
                                    sample_24bit |= ((uint32_t)temp_buffer[i * 6 + 0]) << 0;   // LSB
                                    sample_24bit |= ((uint32_t)temp_buffer[i * 6 + 1]) << 8;   // Middle
                                    sample_24bit |= ((uint32_t)temp_buffer[i * 6 + 2]) << 16;  // MSB (sign bit)
                                    
                                    // 处理符号位（24-bit有符号整数）
                                    if (sample_24bit & 0x800000) { // 如果最高位是1（负数）
                                        sample_24bit |= 0xFF000000; // 扩展符号位到32位
                                    }
                                    
                                    // 转换为16-bit（右移8位，保留高16位）
                                    int16_t sample_16bit = (int16_t)(sample_24bit >> 8);
                                    converted_samples[i] = sample_16bit;
                                }
                            }
                        } else {
                            // 未知位深，按16-bit处理
                            const int16_t* src_samples = reinterpret_cast<const int16_t*>(temp_buffer);
                            if (codec->usb_channels_ == 1) {
                                for (size_t i = 0; i < samples_to_add; i++) {
                                    converted_samples[i] = src_samples[i];
                                }
                            } else {
                                for (size_t i = 0; i < samples_to_add; i++) {
                                    converted_samples[i] = src_samples[i * 2];
                                }
                            }
                        }
                        
                        // 添加转换后的16-bit数据到缓冲区
                        for (size_t i = 0; i < samples_to_add; i++) {
                            codec->usb_audio_buffer_.push_back(converted_samples[i]);
                        }

                        ESP_LOGD(TAG, "Added %u/%u samples after conversion and channel selection, Buffer size: %u",
                                (unsigned int)samples_to_add, (unsigned int)actual_samples_to_add, (unsigned int)codec->usb_audio_buffer_.size());
                    }

                    codec->usb_read_count_++;
                    // 更新统计信息：只统计实际添加的单声道样本数
                    codec->samples_processed_ += samples_to_add;
                } else if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "USB read failed in callback: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGW(TAG, "RX_DONE event but cannot read - Device: %p, Input enabled: %s",
                        codec->uac_rx_device_, codec->input_enabled_ ? "YES" : "NO");
            }
            break;

        case UAC_HOST_DEVICE_EVENT_TX_DONE:
            // TX 事件，但我们只使用 RX（麦克风），所以忽略
            break;

        case UAC_HOST_DRIVER_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "USB audio device disconnected");
            codec->usb_microphone_ready_ = false;
            codec->input_enabled_ = false;
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
        .intr_flags = ESP_INTR_FLAG_LOWMED,
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
        8192,
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
        .stack_size = 8192,
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

    // 设置 UAC 设备配置 - 参考官方示例优化API调用方式
    // 使用官方推荐的缓冲区配置和直接回调读取模式
    const uac_host_device_config_t dev_config = {
        .addr = usb_device_addr_,           // USB 设备地址
        .iface_num = usb_iface_num_,        // USB 接口号
        .buffer_size = 19200,              // 使用官方示例推荐的19.2KB
        .buffer_threshold = 4800,          // 使用官方示例推荐的4.8KB
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
    
    // 保存设备位深信息用于后续数据处理
    device_bit_depth_ = (uint8_t)alt_params.bit_resolution;
    if (device_bit_depth_ != 16 && device_bit_depth_ != 24) {
        ESP_LOGW(TAG, "Unsupported bit depth: %d, falling back to 16-bit", device_bit_depth_);
        device_bit_depth_ = 16;
    }

    // 关键：记录 USB 物理通道数到私有成员，用于回调里 stereo→mono 提取。
    // 但 input_channels_（暴露给上层）必须保持 1，因为本 codec 已经做了
    // stereo→mono 转换，对外吐出的就是 mono；如果改为 2，AudioService
    // 会以为数据是 LR 交错再拆一次，把 mono 当 stereo 处理 → 时序翻倍错乱。
    usb_channels_ = (uint8_t)alt_params.channels;
    ESP_LOGI(TAG, "USB physical channels: %d (codec output stays mono, input_channels_=%d)",
             usb_channels_, input_channels_);

    // 预分配回调里复用的 USB 读取缓冲，避免每次回调 malloc/free
    constexpr size_t kRxScratchBytes = 4096 * 3;  // 兼容 24-bit 最坏情况
    if (rx_scratch_.size() != kRxScratchBytes) {
        rx_scratch_.assign(kRxScratchBytes, 0);
    }
    
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
    tx_chan_cfg.dma_frame_num = AUDIO_CODEC_TX_DMA_FRAME_NUM;
    tx_chan_cfg.auto_clear_after_cb = true;
    tx_chan_cfg.auto_clear_before_cb = false;
    tx_chan_cfg.intr_priority = 0;
    esp_err_t ret = i2s_new_channel(&tx_chan_cfg, &i2s_tx_handle_, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S TX channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 配置 I2S 标准模式
    ESP_LOGI(TAG, "Configuring I2S with sample rate: %dHz", output_sample_rate_);

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

/**
 * @brief 检查USB驱动和设备状态
 *
 * 这个函数用于诊断USB音频设备的状态，帮助识别USB数据流问题
 */
void HybridUsbI2sCodec::CheckUsbDeviceStatus() {
    ESP_LOGI(TAG, "=== USB Device Status Check ===");

    // 检查USB设备状态（UAC 驱动内部已管理USB客户端）
    if (uac_rx_device_ == nullptr) {
        ESP_LOGW(TAG, "UAC RX Device: NULL (USB device not opened)");
    } else {
        ESP_LOGI(TAG, "UAC RX Device: OK (Addr: %d, Iface: %d)",
                usb_device_addr_, usb_iface_num_);

        // 检查USB设备列表
        uint8_t dev_addr_list[8];
        int num_devices = 0;
        esp_err_t ret = usb_host_device_addr_list_fill(8, dev_addr_list, &num_devices);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "USB Devices found: %d", num_devices);
            for (int i = 0; i < num_devices; i++) {
                ESP_LOGI(TAG, "  Device %d: Address %d", i, dev_addr_list[i]);
            }
        } else {
            ESP_LOGW(TAG, "Failed to get USB device list: %s", esp_err_to_name(ret));
        }
    }

    // 检查缓冲区状态
    size_t buffer_usage = 0;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_usage = usb_audio_buffer_.size();
    }

    ESP_LOGI(TAG, "Audio Buffer: %u/%u samples (%.1f%%)",
            (unsigned int)buffer_usage, (unsigned int)buffer_capacity_,
            (buffer_usage * 100.0) / buffer_capacity_);

    // 检查统计信息
    ESP_LOGI(TAG, "Statistics - USB reads: %" PRIu32 ", Overflows: %" PRIu32 ", Processed: %" PRIu32,
            usb_read_count_, usb_overflow_count_, samples_processed_);

    ESP_LOGI(TAG, "=== End USB Device Status Check ===");
}

bool HybridUsbI2sCodec::WaitForUsbDevice(int timeout_ms) {
    if (!usb_initialized_) {
        ESP_LOGE(TAG, "USB not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Waiting for USB audio device connection (%d seconds timeout)...", timeout_ms / 1000);
    ESP_LOGI(TAG, "Troubleshooting:");
    ESP_LOGI(TAG, "  1. Ensure USB microphone is properly connected");
    ESP_LOGI(TAG, "  2. Check USB cable supports data transfer (not power-only)");
    ESP_LOGI(TAG, "  3. Try a different USB port if available");
    ESP_LOGI(TAG, "  4. Ensure adequate power supply to the device");

    // 添加USB设备枚举前的预检查
    ESP_LOGI(TAG, "Performing USB pre-enumeration check...");
    
    // 检查是否有任何USB设备被检测到
    uint8_t dev_addr_list[8];
    int num_devices = 0;
    esp_err_t ret = usb_host_device_addr_list_fill(8, dev_addr_list, &num_devices);
    
    if (ret == ESP_OK && num_devices > 0) {
        ESP_LOGI(TAG, "Found %d USB device(s) before waiting:", num_devices);
        for (int i = 0; i < num_devices; i++) {
            ESP_LOGI(TAG, "  Device %d: Address %d", i, dev_addr_list[i]);
        }
    } else {
        ESP_LOGD(TAG, "No USB devices detected initially (this is normal for fresh connection)");
    }

    // 使用事件组等待 UAC 设备连接事件（参考官方示例）
    EventBits_t bits = xEventGroupWaitBits(
        usb_event_group_,
        USB_EVENT_CONNECTED,
        pdFALSE,  // 不清除位
        pdFALSE,  // 等待任意位
        pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & USB_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "USB microphone connected (Addr: %d, Iface: %d)",
                usb_device_addr_, usb_iface_num_);
        return true;
    }

    // 超时后的详细诊断
    ESP_LOGW(TAG, "No USB audio device found within %dms", timeout_ms);
    
    // 再次检查USB设备列表
    ret = usb_host_device_addr_list_fill(8, dev_addr_list, &num_devices);
    if (ret == ESP_OK && num_devices > 0) {
        ESP_LOGW(TAG, "USB devices detected but no UAC audio device found:");
        for (int i = 0; i < num_devices; i++) {
            ESP_LOGW(TAG, "  Device %d: Address %d (may not be audio class)", i, dev_addr_list[i]);
        }
        ESP_LOGW(TAG, "The connected device may not be a UAC-compliant audio device");
    } else {
        ESP_LOGE(TAG, "No USB devices detected at all - likely hardware/connection issue");
        ESP_LOGE(TAG, "Common causes:");
        ESP_LOGE(TAG, "  - USB cable only supports power (no data lines)");
        ESP_LOGE(TAG, "  - Insufficient power supply to USB device");
        ESP_LOGE(TAG, "  - Poor USB connector contact");
        ESP_LOGE(TAG, "  - USB device not UAC-compliant");
    }

    ESP_LOGW(TAG, "System will continue without USB microphone input");
    ESP_LOGW(TAG, "Audio input will be disabled until a USB microphone is connected");
    return false;
}

// ==================== 异步事件驱动数据处理 ====================

/**
 * @brief USB监控任务（统计和状态监控）
 *
 * 由于数据读取已移到回调中直接进行，这个任务主要用于：
 * 1. 监控缓冲区状态
 * 2. 输出统计信息
 * 3. 检测异常情况
 */
void HybridUsbI2sCodec::UsbDataProcessingTask(void* arg) {
    auto codec = static_cast<HybridUsbI2sCodec*>(arg);
    ESP_LOGI(TAG, "USB monitor task started (direct callback reading mode enabled)");

    uint32_t last_stats_time = xTaskGetTickCount();
    uint32_t last_warning_time = xTaskGetTickCount();

    // 活动统计
    uint32_t active_count = 0;      // 成功处理USB数据的次数
    uint32_t idle_count = 0;       // 空闲等待的次数
    uint32_t last_read_count = 0;  // 上次统计时的读取次数

    ESP_LOGI(TAG, "USB Task Initial State - Device ready: %s, Input enabled: %s, UAC device: %p",
            codec->usb_microphone_ready_ ? "YES" : "NO",
            codec->input_enabled_ ? "YES" : "NO",
            codec->uac_rx_device_);

    while (codec->usb_microphone_ready_ && codec->input_enabled_) {
        // 定期输出统计信息
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_stats_time) > pdMS_TO_TICKS(5000)) {  // 每5秒
            last_stats_time = current_time;

            size_t buffer_usage = 0;
            uint32_t current_read_count = 0;
            {
                std::lock_guard<std::mutex> lock(codec->buffer_mutex_);
                buffer_usage = codec->usb_audio_buffer_.size();
                current_read_count = codec->usb_read_count_;
            }

            // 计算活动统计
            uint32_t reads_in_period = current_read_count - last_read_count;
            if (reads_in_period > 0) {
                active_count += reads_in_period;
            } else {
                idle_count++;
            }
            last_read_count = current_read_count;

            ESP_LOGD(TAG, "USB Task Stats - Buffer: %u/%u samples (%.1f%%), Active: %" PRIu32 ", Idle: %" PRIu32 ", Reads: %" PRIu32 ", Overflows: %" PRIu32 ", Processed: %" PRIu32,
                     (unsigned int)buffer_usage, (unsigned int)codec->buffer_capacity_,
                     (buffer_usage * 100.0) / codec->buffer_capacity_,
                     active_count, idle_count,
                     codec->usb_read_count_, codec->usb_overflow_count_,
                     codec->samples_processed_);

            // 检测异常状态
            if (buffer_usage == 0 && current_read_count == 0 && idle_count > 3) {
                ESP_LOGW(TAG, "CRITICAL: USB buffer empty for %" PRIu32 " seconds! No USB data received.",
                        idle_count * 5);

                // 当缓冲区持续为空时，执行全面的USB设备状态检查
                if (idle_count == 6) {  // 第一次在30秒时
                    ESP_LOGW(TAG, "Performing comprehensive USB device status check...");
                    codec->CheckUsbDeviceStatus();
                }
            }

            // 每30秒检查一次USB设备状态（定期健康检查）
            static uint32_t status_check_count = 0;
            status_check_count++;
            if ((status_check_count % 6) == 0) {  // 每6个统计周期 = 30秒
                codec->CheckUsbDeviceStatus();
            }
        }

        // 检测缓冲区异常情况
        if ((current_time - last_warning_time) > pdMS_TO_TICKS(1000)) {  // 每秒检查一次
            last_warning_time = current_time;

            size_t buffer_usage = 0;
            bool overflowed = false;
            {
                std::lock_guard<std::mutex> lock(codec->buffer_mutex_);
                buffer_usage = codec->usb_audio_buffer_.size();
                overflowed = codec->buffer_overflowed_;
            }

            // 检测缓冲区溢出
            if (overflowed) {
                ESP_LOGD(TAG, "Buffer overflow detected! Usage: %u/%u (%.1f%%)",
                         (unsigned int)buffer_usage, (unsigned int)codec->buffer_capacity_,
                         (buffer_usage * 100.0) / codec->buffer_capacity_);

                std::lock_guard<std::mutex> lock(codec->buffer_mutex_);
                codec->buffer_overflowed_ = false;
            }

            // 检测缓冲区接近满
            if (buffer_usage > codec->buffer_capacity_ * 0.9) {
                static int near_full_warning = 0;
                if (near_full_warning++ < 3) {
                    ESP_LOGW(TAG, "Buffer near full! Usage: %zu/%zu (%.1f%%)",
                             buffer_usage, codec->buffer_capacity_,
                             (buffer_usage * 100.0) / codec->buffer_capacity_);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms检查一次
    }

    ESP_LOGI(TAG, "USB monitor task exiting");
    vTaskDelete(NULL);
}

/**
 * @brief 从USB设备读取数据（已弃用，现在使用直接回调读取模式）
 *
 * 注意：此函数已不再使用，数据读取已移到UacDeviceEventCallback中直接进行
 * 保留此函数仅用于兼容可能的未来需求
 *
 * @param buffer 接收数据的缓冲区
 * @param requested_samples 请求的样本数
 * @param samples_read 实际读取的样本数
 * @return ESP_OK 成功, ESP_ERR_TIMEOUT 超时, 其他错误码
 */
esp_err_t HybridUsbI2sCodec::ReadUsbData(int16_t* buffer, size_t requested_samples, size_t& samples_read) {
    // 已弃用：现在使用直接回调读取模式
    if (uac_rx_device_ == nullptr || buffer == nullptr || requested_samples == 0) {
        samples_read = 0;
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t bytes_read = 0;
    size_t bytes_to_read = requested_samples * sizeof(int16_t);

    // 限制单次读取大小
    const size_t max_usb_buffer = 4096;  // 与回调中的临时缓冲区大小一致
    if (bytes_to_read > max_usb_buffer) {
        bytes_to_read = max_usb_buffer;
    }

    // 使用官方示例推荐的0超时
    esp_err_t ret = uac_host_device_read(uac_rx_device_, (uint8_t*)buffer,
                                        bytes_to_read, &bytes_read, 0);

    if (ret == ESP_OK) {
        samples_read = bytes_read / sizeof(int16_t);
        if (samples_read > requested_samples) {
            samples_read = requested_samples;
        }
    } else {
        samples_read = 0;
    }

    return ret;
}

/**
 * @brief 将不同位深的音频数据转换为16-bit格式
 * 
 * @param src_data 源数据指针
 * @param src_bytes 源数据字节数
 * @param dest 目标16-bit缓冲区
 * @param samples 要转换的样本数
 */
void HybridUsbI2sCodec::ConvertAudioDataTo16Bit(const uint8_t* src_data, size_t src_bytes, int16_t* dest, size_t samples) {
    if (src_data == nullptr || dest == nullptr || samples == 0) {
        return;
    }
    
    if (device_bit_depth_ == 16) {
        // 16-bit设备，直接复制
        const int16_t* src_samples = reinterpret_cast<const int16_t*>(src_data);
        for (size_t i = 0; i < samples; i++) {
            dest[i] = src_samples[i];
        }
    } else if (device_bit_depth_ == 24) {
        // 24-bit设备，转换为16-bit
        for (size_t i = 0; i < samples; i++) {
            // 从3字节24-bit数据中提取值（小端序）
            uint32_t sample_24bit = 0;
            sample_24bit |= ((uint32_t)src_data[i * 3 + 0]) << 0;   // LSB
            sample_24bit |= ((uint32_t)src_data[i * 3 + 1]) << 8;   // Middle  
            sample_24bit |= ((uint32_t)src_data[i * 3 + 2]) << 16;  // MSB (sign bit)
            
            // 处理符号位（24-bit有符号整数）
            if (sample_24bit & 0x800000) { // 如果最高位是1（负数）
                sample_24bit |= 0xFF000000; // 扩展符号位到32位
            }
            
            // 转换为16-bit（右移8位，保留高16位）
            dest[i] = (int16_t)(sample_24bit >> 8);
        }
    } else {
        // 未知位深，按16-bit处理
        const int16_t* src_samples = reinterpret_cast<const int16_t*>(src_data);
        for (size_t i = 0; i < samples && i < src_bytes / sizeof(int16_t); i++) {
            dest[i] = src_samples[i];
        }
    }
}
