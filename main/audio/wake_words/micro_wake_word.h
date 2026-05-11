#ifndef MICRO_WAKE_WORD_H
#define MICRO_WAKE_WORD_H

#include "sdkconfig.h"
#include "wake_word_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#include "audio_codec.h"
#include "wake_word.h"

namespace tflite {
class MicroInterpreter;
class MicroAllocator;
class MicroResourceVariables;
}  // namespace tflite

class MicroWakeWord : public WakeWord {
 public:
    MicroWakeWord();
    ~MicroWakeWord() override;

    void Initialize(AudioCodec* codec) override;
    void Feed(const std::vector<int16_t>& data) override;
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) override;
    void Start() override;
    void Stop() override;
    size_t GetFeedSize() override;
    void EncodeWakeWordData() override;
    bool GetWakeWordOpus(std::vector<uint8_t>& opus) override;
    const std::string& GetLastDetectedWakeWord() const override { return last_detected_wake_word_; }

 private:
    // 30 ms @ 16 kHz = 480 samples per Feed; 10 ms stride = 160 samples;
    // -> 3 spectrogram frames per Feed; carry last 320 samples as history.
    static constexpr int kSamplesPerFeed = 480;
    static constexpr int kStrideSamples = 160;
    static constexpr int kHistorySamples = 320;
    static constexpr int kFramesPerFeed = 3;
    static constexpr int kFeatureSize = 40;

    bool LoadWakeWordModel();
    bool RunFrame(const int16_t* window_pcm);
    void StoreWakeWordData(const int16_t* data, size_t samples);

    AudioCodec* codec_ = nullptr;
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    std::string last_detected_wake_word_;
    bool detection_running_ = false;

    // PCM history for stride-10ms windowing across Feed calls.
    int16_t history_pcm_[kHistorySamples] = {0};

    // Wake-word streaming interpreter + arenas.
    uint8_t* tensor_arena_ = nullptr;
    size_t tensor_arena_size_ = 0;
    uint8_t* var_arena_ = nullptr;
    tflite::MicroAllocator* allocator_ = nullptr;
    tflite::MicroResourceVariables* resource_vars_ = nullptr;
    tflite::MicroInterpreter* interpreter_ = nullptr;

    // Sliding window of recent uint8 probabilities (0-255).
    std::vector<uint8_t> recent_probs_;
    size_t last_n_index_ = 0;
    int16_t ignore_windows_ = 0;
    uint8_t current_stride_step_ = 0;
    uint8_t input_stride_ = 1;
    uint8_t probability_cutoff_u8_ = static_cast<uint8_t>(
        std::min(255, std::max(0, WAKE_WORD_THRESHOLD_X100 * 255 / 100)));
    size_t sliding_window_size_ = WAKE_WORD_WINDOW_SIZE;

    // Replay buffer (抄 dspotter): keep ~1.5s of PCM for opus encode after detection.
    std::list<std::vector<int16_t>> wake_word_pcm_;
    std::list<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;
};

#endif  // MICRO_WAKE_WORD_H
