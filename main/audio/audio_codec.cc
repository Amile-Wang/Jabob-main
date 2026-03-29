#include "audio_codec.h"
#include "board.h"
#include "settings.h"

#include <esp_log.h>
#include <cstring>
#include <driver/i2s_common.h>

#define TAG "AudioCodec"

AudioCodec::AudioCodec() {
}

AudioCodec::~AudioCodec() {
}

void AudioCodec::OutputData(std::vector<int16_t>& data) {
    Write(data.data(), data.size());
}

bool AudioCodec::InputData(std::vector<int16_t>& data) {
    ESP_LOGD(TAG, "InputData: %d", data.size());
    if (data.empty()) {
        ESP_LOGW(TAG, "No input data");
        return false;
    }

    int samples = Read(data.data(), data.size());
    if (samples > 0) {
        // 如果读取的样本数少于vector的大小，调整vector大小以匹配实际读取的数据
        if (samples < data.size()) {
            data.resize(samples);
            ESP_LOGI(TAG, "InputData: Read %d samples, adjusted vector size from %u to %d",
                     samples, data.size() + (samples < data.size() ? (data.size() - samples) : 0), samples);
            // 增加延时防止频繁获取空数据
            ESP_LOGI(TAG, "InputData: Adding delay of 50ms after reading data to prevent rapid empty reads");
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return true;
    }
    data.clear();
    return false;
}

void AudioCodec::Start() {
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    if (output_volume_ <= 0) {
        ESP_LOGW(TAG, "Output volume value (%d) is too small, setting to default (10)", output_volume_);
        output_volume_ = 10;
    }

    if (tx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    }

    if (rx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    }

    EnableInput(true);
    EnableOutput(true);
    ESP_LOGI(TAG, "Audio codec started");
}

void AudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGI(TAG, "Set output volume to %d", output_volume_);
    
    Settings settings("audio", true);
    settings.SetInt("output_volume", output_volume_);
}

void AudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    input_enabled_ = enable;
    ESP_LOGI(TAG, "Set input enable to %s", enable ? "true" : "false");
}

void AudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    output_enabled_ = enable;
    ESP_LOGI(TAG, "Set output enable to %s", enable ? "true" : "false");
}
