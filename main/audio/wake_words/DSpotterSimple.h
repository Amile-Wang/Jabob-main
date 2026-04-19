#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// Simplified DSpotter interface for C compatibility
typedef struct {
    void* handle;
    uint8_t* license_buffer;
    size_t license_size;
} DSpotterSimpleHandle;

/**
 * Initialize DSpotter with license
 * @param sample_rate Audio sample rate (must be 16000)
 * @param channels Audio channels (must be 1)
 * @param license_data License data buffer
 * @param license_size Size of license data
 * @return Handle to DSpotter instance, or NULL on failure
 */
DSpotterSimpleHandle* dspotter_simple_init(int sample_rate, int channels, 
                                          const uint8_t* license_data, size_t license_size);

/**
 * Process audio data for wake word detection
 * @param handle DSpotter handle
 * @param audio_data Audio samples (16-bit signed PCM)
 * @param num_samples Number of samples to process
 * @return 1 if wake word detected, 0 otherwise, -1 on error
 */
int dspotter_simple_process(DSpotterSimpleHandle* handle, const int16_t* audio_data, size_t num_samples);

/**
 * Release DSpotter resources
 * @param handle DSpotter handle to release
 */
void dspotter_simple_release(DSpotterSimpleHandle* handle);

#ifdef __cplusplus
}
#endif