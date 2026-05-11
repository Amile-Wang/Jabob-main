/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// SPDX-License-Identifier: Apache-2.0
// Adapted from esp-tflite-micro/examples/micro_speech: only the single-frame
// GenerateFeature path is kept; the original multi-frame loop is dropped
// because micro-wake-word's streaming model wants per-stride control.

#ifndef MAIN_AUDIO_WAKE_WORDS_MICRO_FEATURES_MICRO_FEATURES_GENERATOR_H_
#define MAIN_AUDIO_WAKE_WORDS_MICRO_FEATURES_MICRO_FEATURES_GENERATOR_H_

#include "tensorflow/lite/c/common.h"
#include "micro_model_settings.h"

// Sets up the audio-preprocessor TFLite Micro interpreter. Must be called once
// before GenerateFeature.
TfLiteStatus InitializeMicroFeatures();

// Runs one preprocessor inference: 480 int16 samples (30 ms @ 16 kHz) ->
// 40-byte int8 spectrogram slice.
TfLiteStatus GenerateFeature(const int16_t* audio_data,
                             int audio_data_size,
                             int8_t* feature_output);

#endif  // MAIN_AUDIO_WAKE_WORDS_MICRO_FEATURES_MICRO_FEATURES_GENERATOR_H_
