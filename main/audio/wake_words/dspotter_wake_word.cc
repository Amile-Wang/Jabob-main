#include "dspotter_wake_word.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

// 在包含任何其他头文件之前处理DSpotter宏冲突
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include "DSpotterSDKApi.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "audio/audio_processor.h"
#include "opus_encoder.h"
#include "audio_codec.h"
#include "application.h"

static const char* TAG = "DSpotterWakeWord";

DSpotterWakeWord::DSpotterWakeWord() {
    audio_buffer_.resize(kMaxBufferSize);
    event_group_ = xEventGroupCreate();
}

DSpotterWakeWord::~DSpotterWakeWord() {
    if (event_group_) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
    
    if (license_data_) {
        free(license_data_);
        license_data_ = nullptr;
    }
}

void DSpotterWakeWord::Initialize(AudioCodec* codec) {
    codec_ = codec;
    ESP_LOGI(TAG, "DSpotter initialized (placeholder)");
}

void DSpotterWakeWord::Feed(const std::vector<int16_t>& data) {
    // 简单模拟检测
    static int counter = 0;
    counter++;
    if (counter % 200 == 0) {
        ESP_LOGI(TAG, "DSpotter wake word detected! (simulation)");
        if (wake_word_detected_callback_) {
            wake_word_detected_callback_(last_detected_wake_word_);
        }
    }
}

void DSpotterWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void DSpotterWakeWord::Start() {
    detection_running_ = true;
    ESP_LOGI(TAG, "DSpotter started");
}

void DSpotterWakeWord::Stop() {
    detection_running_ = false;
    ESP_LOGI(TAG, "DSpotter stopped");
}

size_t DSpotterWakeWord::GetFeedSize() {
    return kFrameSample;
}

void DSpotterWakeWord::EncodeWakeWordData() {
    // 空实现
}

bool DSpotterWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    opus.clear();
    return false;
}

void DSpotterWakeWord::DetectionTask() {
    // 空实现
}

void DSpotterWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    // 空实现
}

void DSpotterWakeWord::ProcessAudioBuffer() {
    // 空实现
}

bool DSpotterWakeWord::InitializeDSpotter() {
    // 空实现
    return true;
}