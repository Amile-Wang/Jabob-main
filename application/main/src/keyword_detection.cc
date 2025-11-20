#include "keyword_detection.h"

#include "audio_service.h"
#include "model_runner.h"
#include "ring_buffer.h"

#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "keyword_detection";

namespace {
// The number of features we compute for each slice of audio.
constexpr int kFeatureSize = 40;
// The number of slices we process in each loop iteration.
constexpr int kFeatureSliceCount = 49;

// Controls how often the ring buffer is polled for new data when the model is
// not running.
constexpr int kNoModelPollIntervalMs = 5;
// Controls how often the ring buffer is polled for new data while the model is
// running (and consuming input).
constexpr int kConsumeInputPollIntervalMs = 1;

// The amount of audio (in ms) to keep in the ring buffer after we've found a
// keyword.
constexpr int kKeepAfterKeywordMs = 2000;

int16_t g_audio_buffer[kFeatureSliceCount * kFeatureSize];
} // namespace

KeywordDetection::KeywordDetection(AudioService& audio_service,
                                   ModelRunner& model_runner)
    : audio_service_(audio_service), model_runner_(model_runner) {}

void KeywordDetection::Start() {
    ESP_LOGI(TAG, "Starting keyword detection");
    
    // Add detailed logging for audio configuration
    ESP_LOGI(TAG, "Audio Configuration:");
    ESP_LOGI(TAG, "  Input Channels: %d", audio_service_.GetInputChannelCount());
    ESP_LOGI(TAG, "  Output Channels: %d", audio_service_.GetOutputChannelCount());
    ESP_LOGI(TAG, "  Sample Rate: %d Hz", audio_service_.GetSampleRate());
    ESP_LOGI(TAG, "  Reference Input Enabled: %s", audio_service_.IsReferenceInputEnabled() ? "true" : "false");
    
    if (audio_service_.IsReferenceInputEnabled()) {
        ESP_LOGI(TAG, "  Reference Input Channels: %d", audio_service_.GetReferenceInputChannelCount());
    }
    
    // Start the audio service.
    audio_service_.Start();
    
    // Run the main loop.
    while (true) {
        int16_t latest_audio_data[kFeatureSliceCount * kFeatureSize];
        const size_t num_samples_read =
            audio_service_.Read(latest_audio_data, sizeof(latest_audio_data) /
                                                       sizeof(latest_audio_data[0]));
        if (num_samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(kNoModelPollIntervalMs));
            continue;
        }

        // Shift the existing audio data left to make room for the new data.
        const int samples_to_keep = sizeof(g_audio_buffer) / sizeof(g_audio_buffer[0]) - num_samples_read;
        memmove(g_audio_buffer, g_audio_buffer + num_samples_read, samples_to_keep * sizeof(g_audio_buffer[0]));

        // Copy the new data to the end of the buffer.
        memcpy(g_audio_buffer + samples_to_keep, latest_audio_data, num_samples_read * sizeof(g_audio_buffer[0]));

        // Run the model on the audio data.
        const float* output_data = nullptr;
        size_t output_length = 0;
        model_runner_.Run(g_audio_buffer, sizeof(g_audio_buffer) / sizeof(g_audio_buffer[0]),
                          &output_data, &output_length);

        if (output_length != 0) {
            // Check if the model detected a keyword.
            bool found_keyword = false;
            for (size_t i = 0; i < output_length; ++i) {
                if (output_data[i] > 0.8f) {
                    found_keyword = true;
                    break;
                }
            }

            if (found_keyword) {
                ESP_LOGI(TAG, "Detected keyword!");
                
                // Log additional information about the detection
                ESP_LOGI(TAG, "Detection details:");
                ESP_LOGI(TAG, "  Audio Buffer Size: %d samples", sizeof(g_audio_buffer) / sizeof(g_audio_buffer[0]));
                ESP_LOGI(TAG, "  Model Output Length: %d", output_length);
                ESP_LOGI(TAG, "  Feature Size: %d", kFeatureSize);
                ESP_LOGI(TAG, "  Feature Slice Count: %d", kFeatureSliceCount);
                
                // Keep some audio after the keyword.
                vTaskDelay(pdMS_TO_TICKS(kKeepAfterKeywordMs));
            }
            
            // Poll more frequently while consuming input.
            vTaskDelay(pdMS_TO_TICKS(kConsumeInputPollIntervalMs));
        } else {
            vTaskDelay(pdMS_TO_TICKS(kNoModelPollIntervalMs));
        }
    }
}