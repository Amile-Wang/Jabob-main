#include "micro_wake_word.h"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "tensorflow/lite/core/c/common.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "micro_features/micro_features_generator.h"
#include "micro_features/micro_model_settings.h"

#include "audio_service.h"  // OPUS_FRAME_DURATION_MS
#include "opus_encoder.h"

static const char* TAG = "MicroWakeWord";

namespace {
// Hi Jabra micro-wake-word model — symbols come from EMBED_FILES in main/CMakeLists.txt.
// Tip: 如果换模型文件名,这两行的 _binary_<basename>_start/_end 要跟着改。
extern "C" const uint8_t kMwwModelStart[] asm("_binary_stream_state_internal_quant_tflite_start");
extern "C" const uint8_t kMwwModelEnd[] asm("_binary_stream_state_internal_quant_tflite_end");

// Tensor arena size: 沿用 hey_jarvis manifest 的 22860 作为起点,AllocateTensors 失败时
// 自动翻倍重试一次。Hi Jabra 模型结构同源,实测可放下;若以后换更大模型再调。
constexpr size_t kBaseTensorArenaSize = 22860;
constexpr size_t kVariableArenaSize = 1024;
constexpr size_t kMaxResourceVariables = 20;

// Cool-off in slices after a detection; matches ESPHome MIN_SLICES_BEFORE_DETECTION.
constexpr int16_t kMinSlicesBeforeDetection = 100;

using StreamingOpResolver = tflite::MicroMutableOpResolver<20>;

bool RegisterStreamingOps(StreamingOpResolver& r) {
    if (r.AddCallOnce() != kTfLiteOk) return false;
    if (r.AddVarHandle() != kTfLiteOk) return false;
    if (r.AddReshape() != kTfLiteOk) return false;
    if (r.AddReadVariable() != kTfLiteOk) return false;
    if (r.AddStridedSlice() != kTfLiteOk) return false;
    if (r.AddConcatenation() != kTfLiteOk) return false;
    if (r.AddAssignVariable() != kTfLiteOk) return false;
    if (r.AddConv2D() != kTfLiteOk) return false;
    if (r.AddMul() != kTfLiteOk) return false;
    if (r.AddAdd() != kTfLiteOk) return false;
    if (r.AddMean() != kTfLiteOk) return false;
    if (r.AddFullyConnected() != kTfLiteOk) return false;
    if (r.AddLogistic() != kTfLiteOk) return false;
    if (r.AddQuantize() != kTfLiteOk) return false;
    if (r.AddDepthwiseConv2D() != kTfLiteOk) return false;
    if (r.AddAveragePool2D() != kTfLiteOk) return false;
    if (r.AddMaxPool2D() != kTfLiteOk) return false;
    if (r.AddPad() != kTfLiteOk) return false;
    if (r.AddPack() != kTfLiteOk) return false;
    if (r.AddSplitV() != kTfLiteOk) return false;
    return true;
}

uint8_t* AllocPSRAM(size_t bytes) {
    return static_cast<uint8_t*>(
        heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
}  // namespace

MicroWakeWord::MicroWakeWord() {
#ifdef CONFIG_MICRO_WAKE_WORD_DISPLAY
    last_detected_wake_word_ = CONFIG_MICRO_WAKE_WORD_DISPLAY;
#else
    last_detected_wake_word_ = "Hey Jarvis";
#endif

#ifdef CONFIG_MICRO_WAKE_WORD_THRESHOLD_X100
    probability_cutoff_u8_ = static_cast<uint8_t>(
        std::min(255, std::max(0, CONFIG_MICRO_WAKE_WORD_THRESHOLD_X100 * 255 / 100)));
#endif

#ifdef CONFIG_MICRO_WAKE_WORD_WINDOW_SIZE
    sliding_window_size_ = CONFIG_MICRO_WAKE_WORD_WINDOW_SIZE;
#endif
    recent_probs_.assign(sliding_window_size_, 0);
    ignore_windows_ = -kMinSlicesBeforeDetection;
}

MicroWakeWord::~MicroWakeWord() {
    Stop();
    if (interpreter_) {
        delete interpreter_;
        interpreter_ = nullptr;
    }
    // tensor_arena_ / var_arena_ allocated via PSRAM heap_caps_aligned_alloc; not freed
    // because life of MicroWakeWord is the process life — same pattern as DSpotter.
}

void MicroWakeWord::Initialize(AudioCodec* codec) {
    codec_ = codec;

    if (InitializeMicroFeatures() != kTfLiteOk) {
        ESP_LOGE(TAG, "Audio preprocessor init failed");
        return;
    }

    if (!LoadWakeWordModel()) {
        ESP_LOGE(TAG, "Wake word model load failed");
        return;
    }

    ESP_LOGI(TAG,
             "MicroWakeWord initialized: model=%u bytes, arena=%u bytes (PSRAM), "
             "wake_word=\"%s\", cutoff=%u/255, window=%u",
             (unsigned)(kMwwModelEnd - kMwwModelStart),
             (unsigned)tensor_arena_size_,
             last_detected_wake_word_.c_str(),
             (unsigned)probability_cutoff_u8_,
             (unsigned)sliding_window_size_);
}

bool MicroWakeWord::LoadWakeWordModel() {
    const tflite::Model* model = tflite::GetModel(kMwwModelStart);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Wake word model schema mismatch: %u vs %u",
                 (unsigned)model->version(), (unsigned)TFLITE_SCHEMA_VERSION);
        return false;
    }

    static StreamingOpResolver op_resolver;
    if (!RegisterStreamingOps(op_resolver)) {
        ESP_LOGE(TAG, "Streaming op resolver registration failed");
        return false;
    }

    var_arena_ = AllocPSRAM(kVariableArenaSize);
    if (!var_arena_) {
        ESP_LOGE(TAG, "PSRAM variable arena alloc failed (%u)", (unsigned)kVariableArenaSize);
        return false;
    }
    allocator_ = tflite::MicroAllocator::Create(var_arena_, kVariableArenaSize);
    resource_vars_ = tflite::MicroResourceVariables::Create(allocator_, kMaxResourceVariables);

    // Try base size; if AllocateTensors fails, double once.
    size_t attempt_sizes[2] = {
        (kBaseTensorArenaSize + 15) & ~15u,
        ((kBaseTensorArenaSize * 2) + 15) & ~15u,
    };
    for (size_t s : attempt_sizes) {
        tensor_arena_ = AllocPSRAM(s);
        if (!tensor_arena_) {
            ESP_LOGW(TAG, "PSRAM tensor arena alloc failed (%u), retry larger", (unsigned)s);
            continue;
        }
        interpreter_ = new tflite::MicroInterpreter(
            model, op_resolver, tensor_arena_, s, resource_vars_);
        if (interpreter_->AllocateTensors() == kTfLiteOk) {
            tensor_arena_size_ = s;
            ESP_LOGI(TAG, "Wake word interpreter allocated, arena=%u used=%u",
                     (unsigned)s, (unsigned)interpreter_->arena_used_bytes());
            break;
        }
        ESP_LOGW(TAG, "AllocateTensors failed at arena=%u, retry larger", (unsigned)s);
        delete interpreter_;
        interpreter_ = nullptr;
        heap_caps_free(tensor_arena_);
        tensor_arena_ = nullptr;
        // Recreate allocator/resource_vars for next attempt — they hold state.
        allocator_ = tflite::MicroAllocator::Create(var_arena_, kVariableArenaSize);
        resource_vars_ = tflite::MicroResourceVariables::Create(allocator_, kMaxResourceVariables);
    }
    if (!interpreter_) {
        return false;
    }

    // Cache stride from input shape: model input is [1, stride, kFeatureSize, 1] or similar.
    TfLiteTensor* in = interpreter_->input(0);
    if (in->dims->size >= 2) {
        input_stride_ = std::max<uint8_t>(1, in->dims->data[1]);
    } else {
        input_stride_ = 1;
    }
    ESP_LOGI(TAG, "Wake word model input stride=%u dims=%dD", (unsigned)input_stride_, in->dims->size);
    return true;
}

void MicroWakeWord::Feed(const std::vector<int16_t>& data) {
    if (!detection_running_ || !interpreter_) {
        return;
    }
    if (data.size() != static_cast<size_t>(kSamplesPerFeed)) {
        ESP_LOGW(TAG, "Unexpected Feed size=%u, expected %d (skipping)",
                 (unsigned)data.size(), kSamplesPerFeed);
        return;
    }

    StoreWakeWordData(data.data(), data.size());

    // Build temporary 800-sample buffer = history(320) + new(480).
    int16_t window_buf[kHistorySamples + kSamplesPerFeed];
    std::memcpy(window_buf, history_pcm_, kHistorySamples * sizeof(int16_t));
    std::memcpy(window_buf + kHistorySamples, data.data(),
                kSamplesPerFeed * sizeof(int16_t));

    // Slide three 30-ms (480-sample) windows at stride 160 samples (10 ms).
    // Window starts: 0, 160, 320 -> covers indices [0..480), [160..640), [320..800).
    for (int w = 0; w < kFramesPerFeed; ++w) {
        if (!RunFrame(window_buf + w * kStrideSamples)) {
            return;  // Inference error; bail this Feed, retry on the next.
        }
    }

    // Update history: keep last 320 samples of new data for next Feed.
    std::memcpy(history_pcm_, data.data() + (kSamplesPerFeed - kHistorySamples),
                kHistorySamples * sizeof(int16_t));
}

bool MicroWakeWord::RunFrame(const int16_t* window_pcm) {
    int8_t feature[kFeatureSize] = {0};
    if (GenerateFeature(window_pcm, kSamplesPerFeed, feature) != kTfLiteOk) {
        ESP_LOGW(TAG, "GenerateFeature failed");
        return false;
    }

    TfLiteTensor* input = interpreter_->input(0);
    int8_t* in_data = tflite::GetTensorData<int8_t>(input);
    std::memcpy(in_data + kFeatureSize * current_stride_step_, feature, kFeatureSize);
    ++current_stride_step_;

    if (current_stride_step_ >= input_stride_) {
        current_stride_step_ = 0;
        if (interpreter_->Invoke() != kTfLiteOk) {
            ESP_LOGW(TAG, "Wake word interpreter invoke failed");
            return false;
        }
        TfLiteTensor* output = interpreter_->output(0);
        // Hi Jabra stream_state_internal_quant.tflite 输出是 int8 量化,需要按
        // scale + zero_point 反量化成 [0,1] 概率,再缩放到 0-255 跟 sliding window 对齐。
        // hey_jarvis 老模型输出是 uint8,直接读就行 — 这里兼容两种。
        uint8_t prob;
        if (output->type == kTfLiteInt8) {
            int8_t raw = output->data.int8[0];
            float prob_float = (raw - output->params.zero_point) * output->params.scale;
            int scaled = static_cast<int>(prob_float * 255.0f);
            prob = static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
        } else {
            prob = output->data.uint8[0];
        }

        ++last_n_index_;
        if (last_n_index_ >= sliding_window_size_) last_n_index_ = 0;
        recent_probs_[last_n_index_] = prob;

        if (prob < probability_cutoff_u8_) {
            ignore_windows_ = std::min<int16_t>(ignore_windows_ + 1, 0);
            return true;
        }

        // prob >= cutoff this slice. Decide on detection only after cool-off & window full.
        if (ignore_windows_ < 0) {
            ignore_windows_ = std::min<int16_t>(ignore_windows_ + 1, 0);
            return true;
        }

        uint32_t sum = 0;
        for (uint8_t p : recent_probs_) sum += p;
        bool detected = sum >
            static_cast<uint32_t>(probability_cutoff_u8_) * sliding_window_size_;
        if (!detected) {
            return true;
        }

        ESP_LOGI(TAG,
                 "Wake word DETECTED: prob=%u/255 avg=%u/255 cutoff=%u window=%u",
                 (unsigned)prob,
                 (unsigned)(sum / sliding_window_size_),
                 (unsigned)probability_cutoff_u8_,
                 (unsigned)sliding_window_size_);
        // Reset state to avoid duplicate triggers.
        std::fill(recent_probs_.begin(), recent_probs_.end(), 0);
        ignore_windows_ = -kMinSlicesBeforeDetection;
        Stop();
        if (wake_word_detected_callback_) {
            wake_word_detected_callback_(last_detected_wake_word_);
        }
    }
    return true;
}

void MicroWakeWord::OnWakeWordDetected(
    std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = std::move(callback);
}

void MicroWakeWord::Start() {
    if (!interpreter_) {
        ESP_LOGW(TAG, "MicroWakeWord not initialized, skip start");
        return;
    }
    detection_running_ = true;
    std::fill(recent_probs_.begin(), recent_probs_.end(), 0);
    ignore_windows_ = -kMinSlicesBeforeDetection;
    current_stride_step_ = 0;
    std::memset(history_pcm_, 0, sizeof(history_pcm_));
    // Hi Jabra 模型是 streaming 模型,内部带 ResourceVariable 状态(CallOnce/VarHandle 那套)。
    // 上一次 Start..Stop 期间的 hidden state 会污染本次首批推理 → 漏检/误检。
    // 每次 Start 必须 ResetAll(),把所有 resource variables 清零。
    if (resource_vars_) {
        resource_vars_->ResetAll();
    }
    ESP_LOGI(TAG, "MicroWakeWord started");
}

void MicroWakeWord::Stop() {
    detection_running_ = false;
    ESP_LOGI(TAG, "MicroWakeWord stopped");
}

size_t MicroWakeWord::GetFeedSize() {
    return kSamplesPerFeed;
}

void MicroWakeWord::EncodeWakeWordData() {
    {
        std::lock_guard<std::mutex> lock(wake_word_mutex_);
        wake_word_opus_.clear();
    }

    auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    encoder->SetComplexity(0);

    for (auto& pcm : wake_word_pcm_) {
        encoder->Encode(std::move(pcm), [this](std::vector<uint8_t>&& opus) {
            std::lock_guard<std::mutex> queue_lock(wake_word_mutex_);
            wake_word_opus_.emplace_back(std::move(opus));
            wake_word_cv_.notify_all();
        });
    }
    wake_word_pcm_.clear();

    // 空包标记编码结束（与 dspotter_wake_word.cc 一致）
    wake_word_opus_.emplace_back(std::vector<uint8_t>());
    wake_word_cv_.notify_all();
}

bool MicroWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() { return !wake_word_opus_.empty(); });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}

void MicroWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    while (wake_word_pcm_.size() > 2000 / 30) {  // ~2 s of 30-ms frames
        wake_word_pcm_.pop_front();
    }
}
