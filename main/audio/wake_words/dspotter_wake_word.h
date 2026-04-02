#ifndef DSPOTTER_WAKE_WORD_H
#define DSPOTTER_WAKE_WORD_H

#include "sdkconfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <list>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>

#include "audio_codec.h"
#include "wake_word.h"

// 使用void指针而不是具体的DSpotter类型
class DSpotterWakeWord : public WakeWord {
public:
    DSpotterWakeWord();
    ~DSpotterWakeWord();

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
    static constexpr int kFrameSample = 480; // 30ms @ 16kHz
    static constexpr int kMaxBufferSize = 16000; // 1秒缓冲区
    
    void* dspotter_handle_ = nullptr; // 使用void指针
    uint8_t* license_data_ = nullptr;
    size_t license_size_ = 0;
    
    EventGroupHandle_t event_group_;
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_ = "Hey Jabra";
    
    TaskHandle_t detection_task_ = nullptr;
    StaticTask_t detection_task_buffer_;
    StackType_t* detection_task_stack_ = nullptr;
    
    std::vector<int16_t> audio_buffer_;
    size_t buffer_write_pos_ = 0;
    bool detection_running_ = false;
    
    TaskHandle_t encode_task_ = nullptr;
    StaticTask_t encode_task_buffer_;
    StackType_t* encode_task_stack_ = nullptr;
    std::list<std::vector<int16_t>> wake_word_pcm_;
    std::list<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;

    void DetectionTask();
    void StoreWakeWordData(const int16_t* data, size_t samples);
    void ProcessAudioBuffer();
    bool InitializeDSpotter();
};

#endif