#include "audio_service.h"
#include "board.h"
#include "settings.h"
#include "audio/codecs/hybrid_usb_i2s_codec.h"

#include <esp_log.h>
#include <cstring>
#include <algorithm>
#include <inttypes.h>

#include "esp_task_wdt.h"

#if CONFIG_USE_AUDIO_PROCESSOR
#include "processors/afe_audio_processor.h"
#else
#include "processors/no_audio_processor.h"
#endif

#if CONFIG_USE_AFE_WAKE_WORD
#include "wake_words/afe_wake_word.h"
#elif CONFIG_USE_ESP_WAKE_WORD
#include "wake_words/esp_wake_word.h"
#elif CONFIG_USE_CUSTOM_WAKE_WORD
#include "wake_words/custom_wake_word.h"
#elif CONFIG_USE_DSPOTTER_WAKE_WORD
#include "wake_words/dspotter_wake_word.h"
#elif CONFIG_USE_MICRO_WAKE_WORD
#include "wake_words/micro_wake_word.h"
#endif

#define TAG "AudioService"


AudioService::AudioService() {
    event_group_ = xEventGroupCreate();
}

AudioService::~AudioService() {
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}


void AudioService::Initialize(AudioCodec* codec) {
    codec_ = codec;
    codec_->Start();

    // 添加延迟确保 USB 设备完成枚举和采样率协商
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 验证采样率有效性（非零）
    int input_sample_rate = codec->input_sample_rate();
    int output_sample_rate = codec->output_sample_rate();

    ESP_LOGI(TAG, "Initial codec sample rates - Input: %dHz, Output: %dHz",
             input_sample_rate, output_sample_rate);

    // 初始化状态变量
    usb_device_ready_ = false;
    last_detected_input_sample_rate_ = input_sample_rate > 0 ? input_sample_rate : 0;
    last_configured_input_sample_rate_ = 0;
    last_configured_output_sample_rate_ = 0;
    sample_rate_change_count_ = 0;
    output_sample_rate_locked_ = false;
    locked_output_sample_rate_ = 0;

    if (output_sample_rate <= 0) {
        ESP_LOGE(TAG, "Invalid output sample rate: %d, using fallback 16kHz", output_sample_rate);
        output_sample_rate = 16000;
        ESP_LOGW(TAG, "Warning: Output audio may not work correctly with fallback sample rate");
    } else {
        ESP_LOGI(TAG, "Output sample rate validated: %dHz", output_sample_rate);
    }

    if (input_sample_rate <= 0) {
        ESP_LOGE(TAG, "Invalid input sample rate: %d, USB device may not be ready", input_sample_rate);
        ESP_LOGI(TAG, "System will use fallback %dHz and retry detection", fallback_input_sample_rate_);
        // 不设置input_sample_rate，让ReadAudioData动态处理
    } else {
        ESP_LOGI(TAG, "Input sample rate validated: %dHz", input_sample_rate);
        last_detected_input_sample_rate_ = input_sample_rate;
        last_configured_input_sample_rate_ = input_sample_rate;
    }

    // Setup the audio codec
    // 尝试使用输出采样率创建 Opus 解码器
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(output_sample_rate, 1, OPUS_FRAME_DURATION_MS);
    
    // 检查 Opus 解码器是否创建成功
    if (!opus_decoder_ || opus_decoder_->sample_rate() != output_sample_rate) {
        ESP_LOGE(TAG, "Failed to create Opus decoder with %dHz, falling back to 16kHz", output_sample_rate);
        opus_decoder_.reset();
        opus_decoder_ = std::make_unique<OpusDecoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
        
        // 配置输出重采样器：16kHz -> 输出采样率
        if (output_sample_rate != 16000) {
            output_resampler_.Configure(16000, output_sample_rate);
            ESP_LOGI(TAG, "Configured fallback output resampler: 16000Hz -> %dHz", output_sample_rate);
        }
    } else {
        ESP_LOGI(TAG, "Opus decoder created successfully with %dHz", output_sample_rate);
        // 如果解码器采样率与输出采样率不同，配置重采样器
        if (opus_decoder_->sample_rate() != output_sample_rate) {
            output_resampler_.Configure(opus_decoder_->sample_rate(), output_sample_rate);
            ESP_LOGI(TAG, "Configured output resampler: %dHz -> %dHz",
                    opus_decoder_->sample_rate(), output_sample_rate);
            last_configured_output_sample_rate_ = output_sample_rate;
        }
    }

    // 锁定输出采样率以防止回滚
    if (output_sample_rate > 0) {
        locked_output_sample_rate_ = output_sample_rate;
        output_sample_rate_locked_ = true;
        ESP_LOGI(TAG, "Output sample rate locked to %dHz (prevents rollback)", locked_output_sample_rate_);
    }
   
    // 添加延迟以避免中断看门狗超时
    vTaskDelay(pdMS_TO_TICKS(1000));

    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_->SetComplexity(0);

    
    // 再添加一个延迟确保OPUS编码器初始化完成
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 重采样器配置移到ReadAudioData中动态处理，避免fallback逻辑问题
    // 如果输入采样率有效且不等于16kHz，会在首次读取时自动配置
    if (input_sample_rate > 0) {
        ESP_LOGI(TAG, "Input resampler will be configured on first read: %dHz -> 16kHz", input_sample_rate);
    } else {
        ESP_LOGI(TAG, "Input resampler will be configured when USB device is ready");
    }

    // 注意：输出重采样器已经在上面的 Opus 解码器处理中配置过了，这里不再重复配置

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

#if CONFIG_USE_AFE_WAKE_WORD
    wake_word_ = std::make_unique<AfeWakeWord>();
#elif CONFIG_USE_ESP_WAKE_WORD
    wake_word_ = std::make_unique<EspWakeWord>();
#elif CONFIG_USE_CUSTOM_WAKE_WORD
    wake_word_ = std::make_unique<CustomWakeWord>();
#elif CONFIG_USE_DSPOTTER_WAKE_WORD
    wake_word_ = std::make_unique<DSpotterWakeWord>();
#elif CONFIG_USE_MICRO_WAKE_WORD
    wake_word_ = std::make_unique<MicroWakeWord>();
#else
    wake_word_ = nullptr;
#endif

    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        #if CONFIG_USE_DSPOTTER_WAKE_WORD || CONFIG_USE_MICRO_WAKE_WORD
        EventBits_t bits = xEventGroupGetBits(event_group_);
        if ((bits & AS_EVENT_WAKE_WORD_RUNNING) && wake_word_) {
            FeedWakeWordWithProcessedAudio(data);
            if (!wake_word_audio_passthrough_enabled_) {
                return;
            }
        }
        #endif
        PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue, std::move(data));
    });

    audio_processor_->OnVadStateChange([this](bool speaking) {
        voice_detected_ = speaking;
        if (callbacks_.on_vad_change) {
            callbacks_.on_vad_change(speaking);
        }
    });

    if (wake_word_) {
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            if (callbacks_.on_wake_word_detected) {
                callbacks_.on_wake_word_detected(wake_word);
            }
        });
    }

    esp_timer_create_args_t audio_power_timer_args = {
        .callback = [](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->CheckAndUpdateAudioPowerState();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_power_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&audio_power_timer_args, &audio_power_timer_);

    // 启动后备数据消费者任务，确保 AFE 缓冲区不会溢出
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->BackupDataConsumerTask();
        vTaskDelete(NULL);
    }, "backup_consumer", 4096, this, 1, &backup_consumer_task_handle_);
}

void AudioService::Start() {
    service_stopped_ = false;
    xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING | AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    esp_timer_start_periodic(audio_power_timer_, 1000000);

    /* Start the audio input task */
#if CONFIG_USE_AUDIO_PROCESSOR
    xTaskCreatePinnedToCore([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 4, this, 15, &audio_input_task_handle_, 1); // 提升优先级到15，高于监控任务
#else
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 4, this, 15, &audio_input_task_handle_); // 提升优先级到15，高于监控任务
#endif

    /* Start the audio output task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioOutputTask();
        vTaskDelete(NULL);
    }, "audio_output", 4096, this, 3, &audio_output_task_handle_);

    /* Start the opus codec task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->OpusCodecTask();
        vTaskDelete(NULL);
    }, "opus_codec", 8192 * 4, this, 10, &opus_codec_task_handle_); // 增加栈大小到32KB以解决栈溢出问题
}

void AudioService::Stop() {
    esp_timer_stop(audio_power_timer_);
    service_stopped_ = true;
    xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
        AS_EVENT_WAKE_WORD_RUNNING |
        AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    audio_encode_queue_.clear();
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
    audio_testing_queue_.clear();
    audio_queue_cv_.notify_all();
}

bool AudioService::ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples) {
    if (!codec_->input_enabled()) {
        codec_->EnableInput(true);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
    }

    // 获取实际输入采样率
    int actual_input_rate = codec_->input_sample_rate();

    // 检测USB设备就绪状态
    if (actual_input_rate > 0 && !usb_device_ready_) {
        usb_device_ready_ = true;
        ESP_LOGI(TAG, "USB device ready, detected sample rate: %dHz", actual_input_rate);
        last_detected_input_sample_rate_ = actual_input_rate;
    }

    // 处理采样率为0的情况（USB设备未完全初始化）
    if (actual_input_rate <= 0) {
        if (last_detected_input_sample_rate_ > 0) {
            // 之前检测到过采样率，现在为0，可能是设备断开
            ESP_LOGW(TAG, "USB device may have disconnected (sample rate became 0)");
            // 保持最后已知的采样率，避免fallback导致的音频质量下降
            actual_input_rate = last_detected_input_sample_rate_;
        } else {
            // 从未检测到有效采样率，使用fallback
            if (!usb_device_ready_) {
                static int fallback_warning_count = 0;
                if (fallback_warning_count++ < 3) {  // 只警告3次，避免日志泛滥
                    ESP_LOGW(TAG, "Input sample rate is 0, using fallback %dHz (warning %d/3)",
                             fallback_input_sample_rate_, fallback_warning_count);
                }
                actual_input_rate = fallback_input_sample_rate_;
            } else {
                // 设备曾经就绪，现在采样率为0，可能是临时问题
                ESP_LOGW(TAG, "USB device was ready but sample rate is now 0, retrying...");
                actual_input_rate = fallback_input_sample_rate_;
            }
        }
    }

    // 检测采样率变化
    if (actual_input_rate != last_detected_input_sample_rate_) {
        sample_rate_change_count_++;
        if (sample_rate_change_count_ <= MAX_SAMPLE_RATE_CHANGES) {
            ESP_LOGI(TAG, "Input sample rate changed: %dHz -> %dHz (change count: %" PRIu32 ")",
                     last_detected_input_sample_rate_, actual_input_rate, sample_rate_change_count_);
            last_detected_input_sample_rate_ = actual_input_rate;
        } else {
            // 变化次数过多，可能是设备问题，锁定到fallback
            ESP_LOGW(TAG, "Too many sample rate changes (%" PRIu32 "), locking to fallback %dHz",
                     sample_rate_change_count_, fallback_input_sample_rate_);
            actual_input_rate = fallback_input_sample_rate_;
        }
    }

    // 验证采样率合理性
    if (actual_input_rate < 8000 || actual_input_rate > 96000) {
        ESP_LOGE(TAG, "Invalid input sample rate: %dHz, falling back to %dHz",
                 actual_input_rate, fallback_input_sample_rate_);
        actual_input_rate = fallback_input_sample_rate_;
    }

    // 重采样器配置检查和更新
    bool need_reconfigure = false;
    if (input_resampler_.input_sample_rate() == 0) {
        // 重采样器未配置
        need_reconfigure = true;
        ESP_LOGI(TAG, "Resampler not configured, initializing with %dHz", actual_input_rate);
    } else if (input_resampler_.input_sample_rate() != actual_input_rate) {
        // 输入采样率变化
        need_reconfigure = true;
        ESP_LOGI(TAG, "Resampler input rate changed: %dHz -> %dHz",
                 input_resampler_.input_sample_rate(), actual_input_rate);
    } else if (input_resampler_.output_sample_rate() != sample_rate) {
        // 输出采样率变化（理论上不应该发生）
        need_reconfigure = true;
        ESP_LOGW(TAG, "Resampler output rate mismatch: expected %dHz, got %dHz",
                 sample_rate, input_resampler_.output_sample_rate());
    }

    if (need_reconfigure) {
        ESP_LOGI(TAG, "Reconfiguring input resampler: %dHz -> %dHz",
                 actual_input_rate, sample_rate);
        input_resampler_.Configure(actual_input_rate, sample_rate);
        reference_resampler_.Configure(actual_input_rate, sample_rate);
        last_configured_input_sample_rate_ = actual_input_rate;

        // 验证配置是否成功
        if (input_resampler_.input_sample_rate() != actual_input_rate ||
            input_resampler_.output_sample_rate() != sample_rate) {
            ESP_LOGE(TAG, "Resampler configuration failed! Expected %dHz->%dHz, got %dHz->%dHz",
                     actual_input_rate, sample_rate,
                     input_resampler_.input_sample_rate(), input_resampler_.output_sample_rate());
            return false;
        }
    }

    // 数据读取和重采样
    if (actual_input_rate != sample_rate) {
        // 需要重采样
        size_t required_samples = samples * actual_input_rate / sample_rate;
        if (required_samples == 0) {
            ESP_LOGE(TAG, "Invalid sample calculation: samples=%d, input_rate=%d, target_rate=%d",
                     samples, actual_input_rate, sample_rate);
            return false;
        }

        data.resize(required_samples);
        if (!codec_->InputData(data)) {
            return false;
        }

        if (data.empty()) {
            ESP_LOGW(TAG, "No audio data read from codec");
            return false;
        }

        if (codec_->input_channels() == 2) {
            // 双声道处理
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }

            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));

            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());

            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            // 单声道处理
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }

        // 验证重采样结果
        if (data.size() != samples) {
            // 注释掉这行日志以避免互斥锁崩溃问题
            // ESP_LOGI(TAG, "Resampling size mismatch: expected %d, got %u", samples, data.size());
            // 改为条件性调试日志（仅在DEBUG模式下启用）
#ifdef CONFIG_DEBUG_AUDIO_RESAMPLING
            ESP_LOGD(TAG, "Resampling size mismatch: expected %d, got %u", samples, (unsigned int)data.size());
#endif
        }
    } else {
        // 不需要重采样，直接读取
        data.resize(samples);
        if (!codec_->InputData(data)) {
            return false;
        }
        if (data.empty()) {
            ESP_LOGW(TAG, "No audio data read from codec");
            return false;
        }
    }

    // 检查是否所有数据都是0（USB麦克风未连接的迹象）
    int16_t max_val = *std::max_element(data.begin(), data.end());
    int16_t min_val = *std::min_element(data.begin(), data.end());
    static int consecutive_zero_reads = 0; // 静态变量跟踪连续零读取

    if (max_val == 0 && min_val == 0) {
        consecutive_zero_reads++;
        ESP_LOGD(TAG, "Zero audio data detected (count: %d), audio buffer may be empty",
                 consecutive_zero_reads);

        // // 如果连续5次读到零数据，说明USB麦克风确实未连接
        // if (consecutive_zero_reads >= 5) {
        //     ESP_LOGW(TAG, "Detected USB microphone disconnection, disabling audio input temporarily");
        //     consecutive_zero_reads = 0; // 重置计数器
        //     vTaskDelay(pdMS_TO_TICKS(1000)); // 等待1秒后重试
        //     return false;
        // }
    } else {
        consecutive_zero_reads = 0; // 有有效数据，重置计数器
    }

    /* Update the last input time */
    last_input_time_ = std::chrono::steady_clock::now();
    debug_statistics_.input_count++;

    // 添加调试日志，输出部分音频数据以检查麦克风是否正常工作
    if (debug_statistics_.input_count % 50 == 0) { // 每50次采样输出一次调试信息
        ESP_LOGD(TAG, "Audio input debug - first 10 samples: ");
        // for (int i = 0; i < std::min(10, (int)data.size()); i++) {
        //     ESP_LOGD(TAG, "  Sample[%d]: %d", i, data[i]);
        // }
        ESP_LOGD(TAG, "  Total samples: %d, max value: %d, min value: %d", 
                 (int)data.size(), 
                 *std::max_element(data.begin(), data.end()),
                 *std::min_element(data.begin(), data.end()));
    }

#if CONFIG_USE_AUDIO_DEBUGGER
    // 音频调试：发送原始音频数据
    if (audio_debugger_ == nullptr) {
        audio_debugger_ = std::make_unique<AudioDebugger>();
    }
    audio_debugger_->Feed(data);
#endif

    return true;
}

void AudioService::AudioInputTask() {
    std::vector<int16_t> audio_testing_buffer;
    std::vector<int16_t> wake_word_buffer;
    std::vector<int16_t> audio_processor_buffer;

    while (true) {
        EventBits_t bits = xEventGroupWaitBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
            AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING,
            pdFALSE, pdFALSE, portMAX_DELAY);

        if (service_stopped_) {
            break;
        }
        if (audio_input_need_warmup_) {
            audio_input_need_warmup_ = false;
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        if ((bits & AS_EVENT_AUDIO_TESTING_RUNNING) == 0) {
            audio_testing_buffer.clear();
        }
        if ((bits & AS_EVENT_WAKE_WORD_RUNNING) == 0) {
            wake_word_buffer.clear();
        }
        if ((bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) == 0) {
            audio_processor_buffer.clear();
        }

        int read_samples = 0;

        if (bits & AS_EVENT_AUDIO_TESTING_RUNNING) {
            read_samples = std::max(read_samples, OPUS_FRAME_DURATION_MS * 16000 / 1000);
        }

#if CONFIG_USE_WAKE_WORD
        if ((bits & AS_EVENT_WAKE_WORD_RUNNING) && wake_word_) {
#if CONFIG_USE_DSPOTTER_WAKE_WORD
            static uint32_t dspotter_bits_log_counter = 0;
        EventBits_t current_bits = xEventGroupGetBits(event_group_);
            ++dspotter_bits_log_counter;
            if (dspotter_bits_log_counter == 1 || (dspotter_bits_log_counter % 50) == 0) {
                ESP_LOGI(TAG,
            "DSpotter input loop bits: wake=%d processor=%d testing=%d current_processor=%d",
            (bits & AS_EVENT_WAKE_WORD_RUNNING) != 0,
            (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) != 0,
            (bits & AS_EVENT_AUDIO_TESTING_RUNNING) != 0,
            (current_bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) != 0);
            }
#endif
            read_samples = std::max(read_samples, static_cast<int>(wake_word_->GetFeedSize()));
        }
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
        if ((bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) && audio_processor_) {
            read_samples = std::max(read_samples, static_cast<int>(audio_processor_->GetFeedSize()));
        }
#endif

        if (read_samples <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        std::vector<int16_t> data;
        if (!ReadAudioData(data, 16000, read_samples)) {
            #if CONFIG_USE_DSPOTTER_WAKE_WORD
            if ((bits & AS_EVENT_WAKE_WORD_RUNNING) && (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) == 0) {
                static uint32_t dspotter_read_failure_log_counter = 0;
                ++dspotter_read_failure_log_counter;
                if (dspotter_read_failure_log_counter == 1 || (dspotter_read_failure_log_counter % 20) == 0) {
                    ESP_LOGW(TAG,
                        "DSpotter raw feed stalled: failed to read audio, input_enabled=%d, requested_samples=%d",
                        codec_ && codec_->input_enabled(),
                        read_samples);
                }
            }
            #endif
            continue;
        }

        if (codec_->input_channels() == 2) {
            auto mono_data = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
                mono_data[i] = data[j];
            }
            data = std::move(mono_data);
        }

        /* Used for audio testing in NetworkConfiguring mode by clicking the BOOT button */
        if (bits & AS_EVENT_AUDIO_TESTING_RUNNING) {
            if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS) {
                ESP_LOGW(TAG, "Audio testing queue is full, stopping audio testing");
                EnableAudioTesting(false);
                continue;
            }
            int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
            audio_testing_buffer.insert(audio_testing_buffer.end(), data.begin(), data.end());
            while (audio_testing_buffer.size() >= static_cast<size_t>(samples)) {
                auto task = std::make_unique<AudioTask>();
                task->type = kAudioTaskTypeEncodeToTestingQueue;
                task->pcm.assign(audio_testing_buffer.begin(), audio_testing_buffer.begin() + samples);
                audio_testing_buffer.erase(audio_testing_buffer.begin(), audio_testing_buffer.begin() + samples);
                {
                    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
                    audio_encode_queue_.push_back(std::move(task));
                }
            }
        }

#if CONFIG_USE_WAKE_WORD
        /* Used for wake word detection */
        if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
            if (!wake_word_) {
                continue;
            }
#if CONFIG_USE_DSPOTTER_WAKE_WORD
            EventBits_t current_bits = xEventGroupGetBits(event_group_);
            if (current_bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) {
                static uint32_t dspotter_bypass_log_counter = 0;
                ++dspotter_bypass_log_counter;
                if (dspotter_bypass_log_counter == 1 || (dspotter_bypass_log_counter % 20) == 0) {
                    ESP_LOGW(TAG,
                        "DSpotter raw feed bypassed because audio processor is still marked running (snapshot=%d current=%d)",
                        (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) != 0,
                        (current_bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) != 0);
                }
                continue;
            }

            static uint32_t dspotter_raw_feed_log_counter = 0;
            ++dspotter_raw_feed_log_counter;
            if (dspotter_raw_feed_log_counter == 1 || (dspotter_raw_feed_log_counter % 50) == 0) {
                ESP_LOGI(TAG,
                    "DSpotter raw feed active: input_enabled=%d, read_samples=%d, wake_word_feed_size=%d",
                    codec_ && codec_->input_enabled(),
                    read_samples,
                    static_cast<int>(wake_word_->GetFeedSize()));
            }
#endif
            int samples = wake_word_->GetFeedSize();
            wake_word_buffer.insert(wake_word_buffer.end(), data.begin(), data.end());
            while (wake_word_buffer.size() >= static_cast<size_t>(samples)) {
                std::vector<int16_t> wake_word_chunk(wake_word_buffer.begin(), wake_word_buffer.begin() + samples);
                wake_word_->Feed(wake_word_chunk);
                wake_word_buffer.erase(wake_word_buffer.begin(), wake_word_buffer.begin() + samples);
            }
        }
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
        /* Used for audio processor */
        if (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) {
            int samples = audio_processor_->GetFeedSize();
            audio_processor_buffer.insert(audio_processor_buffer.end(), data.begin(), data.end());
            while (audio_processor_buffer.size() >= static_cast<size_t>(samples)) {
                std::vector<int16_t> processor_chunk(audio_processor_buffer.begin(), audio_processor_buffer.begin() + samples);
                audio_processor_->Feed(std::move(processor_chunk));
                audio_processor_buffer.erase(audio_processor_buffer.begin(), audio_processor_buffer.begin() + samples);
            }
        }
#endif
    }
}

void AudioService::FeedWakeWordWithProcessedAudio(const std::vector<int16_t>& pcm) {
    if (!wake_word_) {
        return;
    }

    size_t samples = wake_word_->GetFeedSize();
    if (samples == 0) {
        return;
    }

    wake_word_preprocessed_buffer_.insert(
        wake_word_preprocessed_buffer_.end(),
        pcm.begin(),
        pcm.end());

    while (wake_word_preprocessed_buffer_.size() >= samples) {
        std::vector<int16_t> wake_word_chunk(
            wake_word_preprocessed_buffer_.begin(),
            wake_word_preprocessed_buffer_.begin() + samples);
        wake_word_->Feed(wake_word_chunk);
        wake_word_preprocessed_buffer_.erase(
            wake_word_preprocessed_buffer_.begin(),
            wake_word_preprocessed_buffer_.begin() + samples);
    }
}

void AudioService::AudioOutputTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty() || service_stopped_; });
        if (service_stopped_) {
            break;
        }

        // Log queue status before playback
        ESP_LOGD(TAG, "Playback: playback_queue=%u/%d", 
                 (unsigned int)audio_playback_queue_.size(), MAX_PLAYBACK_TASKS_IN_QUEUE);
        
        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();
        // 当我们刚取出的是一个 sound effect 任务，且 pipeline 已彻底没有 sound 在跑了
        // (sound queue 空 AND playback queue 不再含 sound task) —— 把暂存的 TTS 帧整体
        // splice 到主 decode queue，让 OpusCodecTask 接着消费。这是 pending → main 的唯一释放点。
        if (task && task->is_sound_effect &&
            audio_sound_decode_queue_.empty() &&
            std::none_of(audio_playback_queue_.begin(), audio_playback_queue_.end(),
                         [](const std::unique_ptr<AudioTask>& t) { return t && t->is_sound_effect; })) {
            if (!audio_decode_pending_queue_.empty()) {
                ESP_LOGI(TAG, "Sound effect playback drained, releasing %u pending TTS packets",
                         (unsigned int)audio_decode_pending_queue_.size());
                while (!audio_decode_pending_queue_.empty()) {
                    audio_decode_queue_.push_back(std::move(audio_decode_pending_queue_.front()));
                    audio_decode_pending_queue_.pop_front();
                }
            }
        }
        audio_queue_cv_.notify_all();
        lock.unlock();

        if (!codec_->output_enabled()) {
            codec_->EnableOutput(true);
            esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        }

        // 只有在输出启用时才写入数据
        if (codec_->output_enabled()) {
            codec_->OutputData(task->pcm);
        } else {
            ESP_LOGW(TAG, "Output not enabled, skipping audio playback");
        }

        /* Update the last output time */
        last_output_time_ = std::chrono::steady_clock::now();
        debug_statistics_.playback_count++;

#if CONFIG_USE_SERVER_AEC
        /* Record the timestamp for server AEC */
        if (task->timestamp > 0) {
            lock.lock();
            timestamp_queue_.push_back(task->timestamp);
        }
#endif
    }

    ESP_LOGW(TAG, "Audio output task stopped");
}

void AudioService::OpusCodecTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() {
            return service_stopped_ ||
                (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) ||
                ((!audio_sound_decode_queue_.empty() || !audio_decode_queue_.empty()) && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE);
        });
        if (service_stopped_) {
            break;
        }

        /* Decode the audio from decode queue —— sound 队列优先于 audio 队列。
           保证本地提示音永远先解码、先入 playback queue、先送 codec，
           不论网络 TTS 帧何时到达。 */
        bool has_decode_work =
            (!audio_sound_decode_queue_.empty() || !audio_decode_queue_.empty()) &&
            audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE;
        if (has_decode_work) {
            std::deque<std::unique_ptr<AudioStreamPacket>>& source_queue =
                !audio_sound_decode_queue_.empty() ? audio_sound_decode_queue_ : audio_decode_queue_;
            auto packet = std::move(source_queue.front());
            source_queue.pop_front();
            ESP_LOGD(TAG, "Decode from %s: sound=%u audio=%u playback=%u/%d",
                     packet && packet->is_sound_effect ? "sound" : "audio",
                     (unsigned int)audio_sound_decode_queue_.size(),
                     (unsigned int)audio_decode_queue_.size(),
                     (unsigned int)audio_playback_queue_.size(), MAX_PLAYBACK_TASKS_IN_QUEUE);
            audio_queue_cv_.notify_all();
            lock.unlock();

            // 检查 start_to_speak 标志位，如果为 true 则延迟 300ms
            // 但本地提示音（PlaySound 注入的 popup 等）跳过延迟，否则多帧 sound 会被
            // 中途插入 300ms 静音、听感断裂。flag 留给后续真正的 TTS 第一帧消费。
            if (start_to_speak_ && !packet->is_sound_effect) {
                ESP_LOGD(TAG, "Start to speak detected, delaying decode by 300ms");
                vTaskDelay(pdMS_TO_TICKS(300));
                start_to_speak_ = false;  // 重置标志位
                ESP_LOGD(TAG, "Decode delay completed, flag reset");
            }

            auto task = std::make_unique<AudioTask>();
            task->type = kAudioTaskTypeDecodeToPlaybackQueue;
            task->timestamp = packet->timestamp;
            task->is_sound_effect = packet->is_sound_effect;

            SetDecodeSampleRate(packet->sample_rate, packet->frame_duration);
            if (opus_decoder_->Decode(std::move(packet->payload), task->pcm)) {
                ESP_LOGD(TAG, "Decoded audio frame - Input samples: %zu, Sample rate: %dHz, Frame duration: %dms", 
                         task->pcm.size(), opus_decoder_->sample_rate(), opus_decoder_->duration_ms());
                
                // Resample if the sample rate is different
                int output_sample_rate = codec_->output_sample_rate();
                if (opus_decoder_->sample_rate() != output_sample_rate) {
                    if (output_sample_rate > 0) {
                        int target_size = output_resampler_.GetOutputSamples(task->pcm.size());
                        std::vector<int16_t> resampled(target_size);
                        output_resampler_.Process(task->pcm.data(), task->pcm.size(), resampled.data());
                        task->pcm = std::move(resampled);
                        ESP_LOGI(TAG, "Resampled audio from %dHz to %dHz, samples: %d -> %d",
                                 opus_decoder_->sample_rate(), output_sample_rate, 
                                 task->pcm.size() - (target_size - task->pcm.size()), target_size);
                    } else if (output_sample_rate == 0) {
                        ESP_LOGW(TAG, "Output sample rate is 0, using decoder sample rate: %dHz",
                                 opus_decoder_->sample_rate());
                        // 当输出采样率为0时，不进行重采样，直接使用解码器的采样率
                    }
                }

                lock.lock();
                audio_playback_queue_.push_back(std::move(task));
                // Log queue status after adding to playback queue
                ESP_LOGD(TAG, "Added to playback: decode_queue=%u, playback_queue=%u/%d", 
                         (unsigned int)audio_decode_queue_.size(), (unsigned int)audio_playback_queue_.size(), MAX_PLAYBACK_TASKS_IN_QUEUE);
                audio_queue_cv_.notify_all();
            } else {
                ESP_LOGE(TAG, "Failed to decode audio");
                lock.lock();
            }
            debug_statistics_.decode_count++;
        }
        
        /* Encode the audio to send queue */
        if (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) {
            auto task = std::move(audio_encode_queue_.front());
            audio_encode_queue_.pop_front();
            audio_queue_cv_.notify_all();
            lock.unlock();

            auto packet = std::make_unique<AudioStreamPacket>();
            packet->frame_duration = OPUS_FRAME_DURATION_MS;
            packet->sample_rate = 16000;
            packet->timestamp = task->timestamp;
            if (!opus_encoder_->Encode(std::move(task->pcm), packet->payload)) {
                ESP_LOGE(TAG, "Failed to encode audio");
                continue;
            }

            if (task->type == kAudioTaskTypeEncodeToSendQueue) {
                {
                    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
                    audio_send_queue_.push_back(std::move(packet));
                }
                if (callbacks_.on_send_queue_available) {
                    callbacks_.on_send_queue_available();
                }
            } else if (task->type == kAudioTaskTypeEncodeToTestingQueue) {
                std::lock_guard<std::mutex> lock(audio_queue_mutex_);
                audio_testing_queue_.push_back(std::move(packet));
            }
            debug_statistics_.encode_count++;
            lock.lock();
        }
    }

    ESP_LOGW(TAG, "Opus codec task stopped");
}

void AudioService::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration) {
        return;
    }

    // 防止频繁重配置导致的不稳定性
    static int last_reconfigure_sample_rate = 0;
    static uint32_t reconfigure_counter = 0;
    const uint32_t MAX_RECONFIGURES = 5;

    // 如果采样率变化过于频繁，锁定到当前配置
    if (sample_rate != last_reconfigure_sample_rate) {
        reconfigure_counter++;
        if (reconfigure_counter > MAX_RECONFIGURES) {
            ESP_LOGW(TAG, "Too many sample rate reconfigures (%" PRIu32 "), skipping change from %d to %d",
                     reconfigure_counter, last_reconfigure_sample_rate, sample_rate);
            return;  // 拒绝这次变化，防止回滚
        }
    } else {
        reconfigure_counter = 0;  // 采样率稳定，重置计数器
    }

    last_reconfigure_sample_rate = sample_rate;

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    int target_output_rate = codec->output_sample_rate();

    // 使用锁定的输出采样率防止回滚
    if (output_sample_rate_locked_ && locked_output_sample_rate_ > 0) {
        target_output_rate = locked_output_sample_rate_;
        ESP_LOGI(TAG, "Using locked output sample rate %dHz instead of codec's %dHz",
                 target_output_rate, codec->output_sample_rate());
    }

    // 验证目标输出采样率的有效性
    if (target_output_rate <= 0) {
        ESP_LOGE(TAG, "Invalid target output sample rate: %d, using locked %dHz",
                 target_output_rate, locked_output_sample_rate_);
        target_output_rate = locked_output_sample_rate_;
    }

    if (opus_decoder_->sample_rate() != target_output_rate) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), target_output_rate);
        output_resampler_.Configure(opus_decoder_->sample_rate(), target_output_rate);

        // 验证配置是否成功
        if (output_resampler_.input_sample_rate() != opus_decoder_->sample_rate() ||
            output_resampler_.output_sample_rate() != target_output_rate) {
            ESP_LOGE(TAG, "Output resampler configuration failed! Expected %dHz->%dHz, got %dHz->%dHz",
                     opus_decoder_->sample_rate(), target_output_rate,
                     output_resampler_.input_sample_rate(), output_resampler_.output_sample_rate());
            // 配置失败，锁定到已知良好状态
            target_output_rate = locked_output_sample_rate_;
            output_resampler_.Configure(opus_decoder_->sample_rate(), target_output_rate);
        }
    }
}

void AudioService::PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm) {
    auto task = std::make_unique<AudioTask>();
    task->type = type;
    task->pcm = std::move(pcm);
    
    /* Push the task to the encode queue */
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);

    /* If the task is to send queue, we need to set the timestamp */
    if (type == kAudioTaskTypeEncodeToSendQueue && !timestamp_queue_.empty()) {
        if (timestamp_queue_.size() <= MAX_TIMESTAMPS_IN_QUEUE) {
            task->timestamp = timestamp_queue_.front();
        } else {
            ESP_LOGW(TAG, "Timestamp queue (%u) is full, dropping timestamp", timestamp_queue_.size());
        }
        timestamp_queue_.pop_front();
    }

    audio_queue_cv_.wait(lock, [this]() { return audio_encode_queue_.size() < MAX_ENCODE_TASKS_IN_QUEUE; });
    audio_encode_queue_.push_back(std::move(task));
    audio_queue_cv_.notify_all();
}

bool AudioService::PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait) {
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    // 路由：本地提示音走 sound 专用队列（OpusCodecTask 优先消费，ResetDecoder 不清），
    // 网络音频（TTS 等）走主 decode 队列。两者共用同一容量限制即可——sound 队列实际很短
    // （popup 8 帧），不会真正打满。
    const bool is_sound = packet->is_sound_effect;

    // 路由决策：
    //  - 本地提示音 → audio_sound_decode_queue_（OpusCodecTask 优先消费）
    //  - 网络包（TTS）：检查 pipeline 是否还有 sound 在跑：
    //      sound queue 非空 OR playback queue 内含 sound task → 入 audio_decode_pending_queue_ 暂存
    //      pipeline 干净 → 入 audio_decode_queue_ 主路径
    //    pending 由 AudioOutputTask 取出最后一个 sound task 时整体 splice 到主 decode queue。
    std::deque<std::unique_ptr<AudioStreamPacket>>* target_queue = nullptr;
    const char* target_name = "?";
    if (is_sound) {
        target_queue = &audio_sound_decode_queue_;
        target_name = "sound";
    } else {
        bool sound_in_pipeline =
            !audio_sound_decode_queue_.empty() ||
            std::any_of(audio_playback_queue_.begin(), audio_playback_queue_.end(),
                        [](const std::unique_ptr<AudioTask>& t) { return t && t->is_sound_effect; });
        target_queue = sound_in_pipeline ? &audio_decode_pending_queue_ : &audio_decode_queue_;
        target_name = sound_in_pipeline ? "pending" : "audio";
    }

    if (target_queue->size() >= MAX_DECODE_PACKETS_IN_QUEUE) {
        if (wait) {
            audio_queue_cv_.wait(lock, [target_queue]() { return target_queue->size() < MAX_DECODE_PACKETS_IN_QUEUE; });
        } else {
            ESP_LOGI(TAG, "Decode queue full (%s, %u/%d), dropping packet",
                     target_name, (unsigned int)target_queue->size(), MAX_DECODE_PACKETS_IN_QUEUE);
            return false;
        }
    }
    target_queue->push_back(std::move(packet));
    ESP_LOGD(TAG, "Pushed to %s: audio=%u pending=%u sound=%u playback=%u/%d",
             target_name,
             (unsigned int)audio_decode_queue_.size(),
             (unsigned int)audio_decode_pending_queue_.size(),
             (unsigned int)audio_sound_decode_queue_.size(),
             (unsigned int)audio_playback_queue_.size(), MAX_PLAYBACK_TASKS_IN_QUEUE);
    audio_queue_cv_.notify_all();
    return true;
}

std::unique_ptr<AudioStreamPacket> AudioService::PopPacketFromSendQueue() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    if (audio_send_queue_.empty()) {
        return nullptr;
    }
    auto packet = std::move(audio_send_queue_.front());
    audio_send_queue_.pop_front();
    audio_queue_cv_.notify_all();
    return packet;
}

void AudioService::EncodeWakeWord() {
    if (wake_word_) {
        wake_word_->EncodeWakeWordData();
    }
}

const std::string& AudioService::GetLastWakeWord() const {
    return wake_word_->GetLastDetectedWakeWord();
}

std::unique_ptr<AudioStreamPacket> AudioService::PopWakeWordPacket() {
    auto packet = std::make_unique<AudioStreamPacket>();
    if (wake_word_->GetWakeWordOpus(packet->payload)) {
        return packet;
    }
    return nullptr;
}

void AudioService::EnableWakeWordDetection(bool enable) {
    if (!wake_word_) {
        return;
    }

    ESP_LOGD(TAG, "%s wake word detection", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!wake_word_initialized_) {
            wake_word_->Initialize(codec_);
            wake_word_initialized_ = true;
        }
        wake_word_->Start();
        ESP_LOGI(TAG, "Wake word detection enabled: passthrough=%d, processor_running=%d",
            wake_word_audio_passthrough_enabled_,
            IsAudioProcessorRunning());
        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    } else {
        wake_word_->Stop();
        wake_word_audio_passthrough_enabled_ = false;
        ESP_LOGI(TAG, "Wake word detection disabled");
        xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    }
}

void AudioService::SetWakeWordAudioPassthrough(bool enable) {
    wake_word_audio_passthrough_enabled_ = enable;
}

void AudioService::EnableVoiceProcessing(bool enable) {
    ESP_LOGD(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!audio_processor_initialized_) {
            audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS);
            audio_processor_initialized_ = true;
        }

        /* We should make sure no audio is playing */
        ResetDecoder();
        audio_input_need_warmup_ = true;
        audio_processor_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    } else {
        audio_processor_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    }
}

void AudioService::EnableAudioTesting(bool enable) {
    ESP_LOGI(TAG, "%s audio testing", enable ? "Enabling" : "Disabling");
    if (enable) {
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
    } else {
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
        /* Copy audio_testing_queue_ to audio_decode_queue_ */
        std::lock_guard<std::mutex> lock(audio_queue_mutex_);
        audio_decode_queue_ = std::move(audio_testing_queue_);
        audio_queue_cv_.notify_all();
    }
}

void AudioService::EnableDeviceAec(bool enable) {
    ESP_LOGI(TAG, "%s device AEC", enable ? "Enabling" : "Disabling");
    audio_processor_->EnableDeviceAec(enable);
}

void AudioService::SetCallbacks(AudioServiceCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void AudioService::PlaySound(const std::string_view& sound) {
    // 入帧前先把已在 audio_decode_queue_ 的 TTS 帧 splice 到 pending，让 sound effect
    // 真正"插队"。pending 在 sound 全部播完那一刻由 AudioOutputTask 自动 splice 回。
    // 这样调用方可以在任意时刻调 PlaySound，包括 device_state 已是 Speaking、
    // OnIncomingAudio 闸门已开、TTS 帧已经在 audio_decode_queue_ 的场景下，仍能
    // 即时插入提示音、popup 头部不被 TTS 帧切开。
    {
        std::lock_guard<std::mutex> lock(audio_queue_mutex_);
        if (!audio_decode_queue_.empty()) {
            ESP_LOGI(TAG, "PlaySound preempting %u TTS packets to pending",
                     (unsigned int)audio_decode_queue_.size());
            while (!audio_decode_queue_.empty()) {
                audio_decode_pending_queue_.push_back(std::move(audio_decode_queue_.front()));
                audio_decode_queue_.pop_front();
            }
        }
    }
    const char* data = sound.data();
    size_t size = sound.size();
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        auto packet = std::make_unique<AudioStreamPacket>();
        // 保持原有的16000Hz假设，因为提示音文件确实是16000Hz编码的
        packet->sample_rate = 16000;
        packet->frame_duration = 60;
        packet->payload.resize(payload_size);
        memcpy(packet->payload.data(), p3->payload, payload_size);
        packet->is_sound_effect = true;
        p += payload_size;

        PushPacketToDecodeQueue(std::move(packet), true);
    }
}

bool AudioService::IsIdle() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return audio_encode_queue_.empty() &&
           audio_decode_queue_.empty() &&
           audio_decode_pending_queue_.empty() &&
           audio_sound_decode_queue_.empty() &&
           audio_playback_queue_.empty() &&
           audio_testing_queue_.empty();
}

void AudioService::ResetDecoder() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    opus_decoder_->ResetState();
    timestamp_queue_.clear();
    // 只清网络流（TTS 等）。本地提示音独占 audio_sound_decode_queue_，由 PlaySound 注入、
    // OpusCodecTask 优先消费——ResetDecoder 不动它，确保任何状态切换路径调到这里都不
    // 会误清提示音。pending 也是 TTS 暂存，一并清。playback queue 是混合队列（sound task
    // 解码后也进这里），用 erase_if 保留 is_sound_effect 任务，其余清掉。
    audio_decode_queue_.clear();
    audio_decode_pending_queue_.clear();
    audio_playback_queue_.erase(
        std::remove_if(audio_playback_queue_.begin(), audio_playback_queue_.end(),
            [](const std::unique_ptr<AudioTask>& t) {
                return t && !t->is_sound_effect;
            }),
        audio_playback_queue_.end());
    audio_testing_queue_.clear();
    audio_queue_cv_.notify_all();
}

void AudioService::BackupDataConsumerTask() {
    // 将当前任务添加到看门狗监控列表
    esp_task_wdt_add(NULL);
    
    while (!service_stopped_) {
        // 检查当前是否有活跃的数据消费者
        EventBits_t bits = xEventGroupGetBits(event_group_);
        bool has_active_consumer = (bits & (AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING | AS_EVENT_AUDIO_TESTING_RUNNING)) != 0;
        
        if (!has_active_consumer && wake_word_ && wake_word_->GetFeedSize() > 0) {
            // 没有活跃消费者，但唤醒词检测已初始化，尝试fetch数据防止AFE溢出
            // 注意：这里我们不能直接调用wake_word_->Feed，因为没有数据源
            // 但我们可以通过检查AFE状态来间接帮助清空缓冲区
            // 实际上，AFE的fetch应该由对应的处理器来完成
            // 所以这里主要是作为额外的安全保障
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            // 有活跃消费者或没有初始化，可以较长延迟
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        
        // 定期重置看门狗
        esp_task_wdt_reset();
    }
    
    // 从看门狗监控列表中移除任务
    esp_task_wdt_delete(NULL);
    
    ESP_LOGW(TAG, "Backup data consumer task stopped");
}

void AudioService::CheckAndUpdateAudioPowerState() {
    if (!codec_ || !codec_->input_enabled()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // 检查是否需要关闭音频电源（5秒无输入）
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_input_time_).count() > 5000) {
        ESP_LOGI(TAG, "No audio input for 5 seconds, disabling audio input to save power");
        codec_->EnableInput(false);
        esp_timer_stop(audio_power_timer_);
    }
}