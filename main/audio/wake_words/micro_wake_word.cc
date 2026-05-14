#include "micro_wake_word.h"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"  // esp_timer_get_time() — 给概率 log 打时间戳用

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

// ============================ 调试开关 =====================================
// 唤醒不工作时打开下面两个 macro,可以看每帧概率 / spectrogram 摘要 / 模型 tensor 细节,
// 用来定位"前端是否在出数 / 概率到底高不高 / 模型是否按 int8 输出 / 是否落在阈值上方"。
//
// 警告:打开后串口会非常吵 — 每秒约 100 行(概率) + 每 0.5s 一行(feature),
//      定位完务必把两个 macro 改回 0 再发布。
//
// 关闭后这部分代码会被预处理器整段干掉,零运行时开销。
#define MWW_VERBOSE_PROB_LOG    1   // 每次 Invoke 后打 prob/cutoff/时间戳/state
#define MWW_VERBOSE_FEATURE_LOG 1   // 每 50 次 RunFrame 打 spectrogram 摘要 (前端健康检查)
// ===========================================================================

namespace {
// Hi Jabra micro-wake-word model — symbols come from EMBED_FILES in main/CMakeLists.txt.
// Tip: 如果换模型文件名,这两行的 _binary_<basename>_start/_end 要跟着改。
extern "C" const uint8_t kMwwModelStart[] asm("_binary_hi_jabra_tflite_start");
extern "C" const uint8_t kMwwModelEnd[] asm("_binary_hi_jabra_tflite_end");

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

    // 把 input/output tensor 的量化参数和形状全打出来 — 加载阶段只打 1 次,信息密度高。
    // 排障时优先看 OUT tensor 的 type:
    //   type=9 (kTfLiteInt8)  → 走 (raw - zp) * scale 反量化,Hi Jabra 走这条
    //   type=3 (kTfLiteUInt8) → 直接读 data.uint8[0],hey_jarvis 走这条
    // 如果两条都不是,RunFrame 里的概率会全是 0 或乱码 — 模型量化方案不对要重训。
    TfLiteTensor* out = interpreter_->output(0);
    auto dim_or = [](TfLiteIntArray* d, int i) {
        return (d && i < d->size) ? (int)d->data[i] : -1;
    };
    ESP_LOGI(TAG, "Wake word IN  tensor: type=%d scale=%.6f zp=%d dims=[%d,%d,%d,%d]",
             (int)in->type, in->params.scale, (int)in->params.zero_point,
             dim_or(in->dims, 0), dim_or(in->dims, 1),
             dim_or(in->dims, 2), dim_or(in->dims, 3));
    ESP_LOGI(TAG, "Wake word OUT tensor: type=%d scale=%.6f zp=%d dims=[%d,%d,%d,%d]",
             (int)out->type, out->params.scale, (int)out->params.zero_point,
             dim_or(out->dims, 0), dim_or(out->dims, 1),
             dim_or(out->dims, 2), dim_or(out->dims, 3));
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

#if MWW_VERBOSE_FEATURE_LOG
    // ---- Spectrogram 健康检查 ----
    // 排障问题:"前端到底有没有在出数?" 如果 abs_sum 长期 = 0 → 麦克风没声 / 前端挂了;
    //          如果 abs_sum 不随说话变化 → 前端可能算错了 stride / scale。
    // 正常情况:安静时 abs_sum ≈ 几十到几百;说话时 abs_sum 跳到 1000+。
    {
        static uint16_t dbg_feat_count = 0;
        if (++dbg_feat_count >= 50) {  // 每 50 帧打一次 ≈ 每 500 ms
            dbg_feat_count = 0;
            int feat_abs_sum = 0;
            int feat_min = 127, feat_max = -128;
            for (int i = 0; i < kFeatureSize; i++) {
                feat_abs_sum += abs(feature[i]);
                if (feature[i] < feat_min) feat_min = feature[i];
                if (feature[i] > feat_max) feat_max = feature[i];
            }
            ESP_LOGI(TAG, "Feature health: abs_sum=%d min=%d max=%d head=[%d,%d,%d,%d,%d]",
                     feat_abs_sum, feat_min, feat_max,
                     feature[0], feature[1], feature[2], feature[3], feature[4]);
        }
    }
#endif

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
        // Hi Jabra hi_jabra.tflite 输出是 int8 量化,需要按
        // scale + zero_point 反量化成 [0,1] 概率,再缩放到 0-255 跟 sliding window 对齐。
        // hey_jarvis 老模型输出是 uint8,直接读就行 — 这里兼容两种。
        // prob_float 存原始 [0,1] 概率,verbose log 用;prob 是 0-255 量化值用于滑窗。
        uint8_t prob;
        float prob_float;
        if (output->type == kTfLiteInt8) {
            int8_t raw = output->data.int8[0];
            prob_float = (raw - output->params.zero_point) * output->params.scale;
            int scaled = static_cast<int>(prob_float * 255.0f);
            prob = static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
        } else {
            uint8_t raw = output->data.uint8[0];
            // 即使 uint8 也按 scale + zp 反量化算 prob_float — 跨模型一致。
            prob_float = (raw - output->params.zero_point) * output->params.scale;
            prob = raw;
        }

#if MWW_VERBOSE_PROB_LOG
        // ---- 每帧概率 log ----
        // 排障:把这一行往串口里翻 — 安静时 prob 应该接近 0;
        //       说唤醒词时,**说完之后那 100-200 ms** prob 应该冲到 cutoff 以上。
        //   - 全程 prob ≈ 0:模型没接收到有效特征(前端坏 / 量化分支错)
        //   - prob 慢慢漂高但永远不过阈值:阈值偏高,先把 THRESHOLD_X100 调到 30 试
        //   - prob 跳到 200+ 但不触发:滑窗 / cool-off 问题,看 ignore=
        // 时间戳是 esp_timer 自启动起的 ms — 拿来跟"我开始说唤醒词"对时,算有效感受野延迟。
        // 注意:不能用 %llu — sdkconfig 开了 CONFIG_NEWLIB_NANO_FORMAT=y,
        //       nano printf 不支持 long long,会把 %llu 当字面 "lu" 打,
        //       并且后续所有参数错位 → 整行垃圾。这里强制 32-bit ms (49 天才溢出).
        uint32_t ts_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        ESP_LOGI(TAG, "[%lu ms] prob=%u(%.3f) cutoff=%u ignore=%d",
                 (unsigned long)ts_ms,
                 (unsigned)prob, prob_float,
                 (unsigned)probability_cutoff_u8_,
                 (int)ignore_windows_);
#endif

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
    // 如果 resource_vars_ 是 NULL(LoadWakeWordModel 失败那段路径)前面已经 return,
    // 但加防御性 check + warn log,排障时一眼能看出来。
    if (resource_vars_) {
        resource_vars_->ResetAll();
        ESP_LOGI(TAG, "MicroWakeWord started (streaming state ResetAll done, cutoff=%u/255 window=%u)",
                 (unsigned)probability_cutoff_u8_, (unsigned)sliding_window_size_);
    } else {
        ESP_LOGW(TAG, "MicroWakeWord started but resource_vars_ is NULL — streaming state NOT reset, first detection unreliable");
    }
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
