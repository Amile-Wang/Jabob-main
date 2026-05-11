/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.

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
// Adapted from esp-tflite-micro/examples/micro_speech for streaming
// micro-wake-word: stride 10ms (was 20ms) to match training params.

#ifndef MAIN_AUDIO_WAKE_WORDS_MICRO_FEATURES_MICRO_MODEL_SETTINGS_H_
#define MAIN_AUDIO_WAKE_WORDS_MICRO_FEATURES_MICRO_MODEL_SETTINGS_H_

constexpr int kAudioSampleFrequency = 16000;
constexpr int kFeatureSize = 40;
constexpr int kFeatureDurationMs = 30;
constexpr int kFeatureStrideMs = 10;

constexpr int kAudioSampleDurationCount =
    kFeatureDurationMs * kAudioSampleFrequency / 1000;  // 480
constexpr int kAudioSampleStrideCount =
    kFeatureStrideMs * kAudioSampleFrequency / 1000;    // 160

#endif  // MAIN_AUDIO_WAKE_WORDS_MICRO_FEATURES_MICRO_MODEL_SETTINGS_H_
