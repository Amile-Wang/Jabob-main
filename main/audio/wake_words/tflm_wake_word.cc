#include "tflm_wake_word.h"
#include "micro_speech_quantized_model_data.h"
#include "micro_model_settings.h"

#include <esp_log.h>
#include <model_path.h>
#include <arpa/inet.h>
#include "opus_encoder.h"

#define DETECTION_RUNNING_EVENT 1
#define TAG "TFLMWakeWord"
#define OPUS_FRAME_DURATION_MS 20

// TFLM includes
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
    constexpr size_t kTensorArenaSize = 16000;  // 根据实际需求调整
    // alignas(16) static uint8_t tensor_arena[kTensorArenaSize];
    
    const tflite::Model* model = nullptr;
    // tflite::MicroInterpreter* interpreter = nullptr;
    TfLiteTensor* input = nullptr;
    TfLiteTensor* output = nullptr;
    
    // Op Resolver
    tflite::MicroMutableOpResolver<5> op_resolver;
}

TFLMWakeWord::TFLMWakeWord()
    : afe_data_(nullptr),
      tensor_arena_(nullptr),
      interpreter_(nullptr),
      wake_word_pcm_(),
      wake_word_opus_() {
    event_group_ = xEventGroupCreate();
    tensor_arena_ = new uint8_t[kTensorArenaSize];
}

TFLMWakeWord::~TFLMWakeWord() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    vEventGroupDelete(event_group_);

    delete[] tensor_arena_;
}

void TFLMWakeWord::Initialize(AudioCodec* codec) {
    codec_ = codec;
    int ref_num = codec_->input_reference() ? 1 : 0;

    models = esp_srmodel_init("model");
    if (models == nullptr || models->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize models");
        return;
    }
    
    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }

    // 配置AFE用于TFLM唤醒词检测
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_config->aec_init = codec_->input_reference();
    afe_config->aec_mode = AEC_MODE_SR_HIGH_PERF;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    // 关闭内置的唤醒词检测，使用我们自己的TFLM模型
    afe_config->wakenet_init = false;
    
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);

    // 初始化TFLM模型
    model = tflite::GetModel(micro_speech_quantized_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model version mismatch");
        return;
    }

    // 初始化操作解析器
    op_resolver.AddReshape();
    op_resolver.AddFullyConnected();
    op_resolver.AddDepthwiseConv2D();
    op_resolver.AddSoftmax();
    op_resolver.AddConv2D();

    // 创建解释器
    static tflite::MicroInterpreter static_interpreter(
        model, op_resolver, tensor_arena_, kTensorArenaSize);
    interpreter_ = &static_interpreter;

    // 分配张量
    TfLiteStatus allocate_status = interpreter_->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors");
        return;
    }

    // 获取输入和输出张量
    input = interpreter_->input(0);
    output = interpreter_->output(0);

    xTaskCreate([](void* arg) {
        auto this_ = (TFLMWakeWord*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 8192, this, 3, nullptr);
}

void TFLMWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void TFLMWakeWord::Start() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void TFLMWakeWord::Stop() {
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
}

void TFLMWakeWord::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr) {
        return;
    }
    afe_iface_->feed(afe_data_, data.data());
}

size_t TFLMWakeWord::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
}

void TFLMWakeWord::AudioDetectionTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);

    ESP_LOGI(TAG, "TFLM Audio detection task started, feed size: %d fetch size: %d", 
             feed_size, fetch_size);

    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            ESP_LOGW(TAG, "Fetch failed, continue");
            continue;
        }

        // 存储音频数据用于语音识别
        StoreWakeWordData(res->data, res->data_size / sizeof(int16_t));

        // 使用TFLM模型检测唤醒词
        // ESP AFE已经处理了音频预处理，现在我们只需要将处理后的音频数据传递给TFLM模型
        // 注意：我们需要确保AFE输出的数据格式与TFLM模型输入格式匹配
        
        // 检查输入张量大小是否匹配
        if (res->data_size == input->bytes) {
            // 直接复制AFE处理后的数据到模型输入
            memcpy(input->data.int8, res->data, input->bytes);
        } else {
            ESP_LOGW(TAG, "Data size mismatch: AFE output %d bytes, model input %d bytes", 
                     res->data_size, input->bytes);
            // 如果大小不匹配，进行适当的处理（如填充或截断）
            size_t copy_size = (res->data_size < input->bytes) ? res->data_size : input->bytes;
            memcpy(input->data.int8, res->data, copy_size);
            // 如果AFE输出较小，用零填充剩余部分
            if (copy_size < input->bytes) {
                memset(input->data.int8 + copy_size, 0, input->bytes - copy_size);
            }
        }
        
        // 运行推理
        TfLiteStatus invoke_status = interpreter_->Invoke();
        if (invoke_status != kTfLiteOk) {
            ESP_LOGE(TAG, "TFLM inference failed");
            continue;
        }

        // 解析结果
        float output_scale = output->params.scale;
        int output_zero_point = output->params.zero_point;
        
        // 查找概率最高的类别
        int max_index = 0;
        float max_prob = (output->data.int8[0] - output_zero_point) * output_scale;
        for (int i = 1; i < kCategoryCount; i++) {
            float prob = (output->data.int8[i] - output_zero_point) * output_scale;
            if (prob > max_prob) {
                max_prob = prob;
                max_index = i;
            }
        }

        // 如果检测到唤醒词 ("yes" 或 "no")
        if (max_index >= 2 && max_prob > 0.7) { // 阈值可根据需要调整
            ESP_LOGI(TAG, "Wake word detected via TFLM: %s, prob=%f", 
                     kCategoryLabels[max_index], max_prob);
            
            // 停止检测
            Stop();
            last_detected_wake_word_ = kCategoryLabels[max_index];
            
            // 调用回调
            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(last_detected_wake_word_);
            }
            
            // 重新开始检测
            Start();
            ESP_LOGI(TAG, "Ready for next detection");
        }
    }
    
    ESP_LOGI(TAG, "Audio detection task ended");
}

void TFLMWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    // store audio data to wake_word_pcm_
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    // keep about 2 seconds of data, detect duration is 30ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void TFLMWakeWord::EncodeWakeWordData() {
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(4096 * 8, MALLOC_CAP_SPIRAM);
    }
    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (TFLMWakeWord*)arg;
        {
            auto start_time = esp_timer_get_time();
            auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
            encoder->SetComplexity(0); // 0 is the fastest
            for (const auto& frame : this_->wake_word_pcm_) {
                std::vector<uint8_t> opus_data;
                encoder->Encode(std::vector<int16_t>(frame), opus_data);
                
                if (opus_data.size() > 0) {
                    this_->wake_word_opus_.push_back(std::move(opus_data));
                }
            }
            ESP_LOGI(TAG, "Encode %d frames, time used: %lld ms", this_->wake_word_pcm_.size(),
                (esp_timer_get_time() - start_time) / 1000);
        }
        this_->wake_word_cv_.notify_one();
        vTaskDelete(NULL);
    }, "wake_word_encode", 4096 * 8, this, 2, wake_word_encode_task_stack_, &wake_word_encode_task_buffer_);
}

bool TFLMWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this] { return !wake_word_opus_.empty(); });
    if (!wake_word_opus_.empty()) {
        opus = std::move(wake_word_opus_.front());
        wake_word_opus_.pop_front();
        return true;
    }
    return false;
}