#include "DSpotterSimple.h"
#include "DSpotterApi.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "DSpotterSimple";

DSpotterSimpleHandle* dspotter_simple_init(int sample_rate, int channels, 
                                          const uint8_t* license_data, size_t license_size) {
    // Validate parameters
    if (sample_rate != 16000 || channels != 1) {
        ESP_LOGE(TAG, "Invalid parameters: sample_rate=%d, channels=%d", sample_rate, channels);
        return NULL;
    }
    
    if (!license_data || license_size == 0) {
        ESP_LOGE(TAG, "Invalid license data");
        return NULL;
    }
    
    // Allocate handle
    DSpotterSimpleHandle* handle = (DSpotterSimpleHandle*)malloc(sizeof(DSpotterSimpleHandle));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    // Copy license data
    handle->license_buffer = (uint8_t*)malloc(license_size);
    if (!handle->license_buffer) {
        ESP_LOGE(TAG, "Failed to allocate license buffer");
        free(handle);
        return NULL;
    }
    
    memcpy(handle->license_buffer, license_data, license_size);
    handle->license_size = license_size;
    
    // Initialize DSpotter
    DSpotterPara para;
    memset(&para, 0, sizeof(para));
    para.SampleRate = sample_rate;
    para.ChannelNum = channels;
    para.License = handle->license_buffer;
    para.LicenseSize = license_size;
    
    handle->handle = DSpotter_Init(&para);
    if (!handle->handle) {
        ESP_LOGE(TAG, "DSpotter_Init failed");
        free(handle->license_buffer);
        free(handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "DSpotter simple init successful");
    return handle;
}

int dspotter_simple_process(DSpotterSimpleHandle* handle, const int16_t* audio_data, size_t num_samples) {
    if (!handle || !handle->handle || !audio_data || num_samples == 0) {
        return -1;
    }
    
    int result = DSpotter_Process(handle->handle, (short*)audio_data, num_samples);
    return (result > 0) ? 1 : 0;
}

void dspotter_simple_release(DSpotterSimpleHandle* handle) {
    if (!handle) {
        return;
    }
    
    if (handle->handle) {
        DSpotter_Release(handle->handle);
        handle->handle = NULL;
    }
    
    if (handle->license_buffer) {
        free(handle->license_buffer);
        handle->license_buffer = NULL;
    }
    
    free(handle);
    ESP_LOGI(TAG, "DSpotter simple released");
}