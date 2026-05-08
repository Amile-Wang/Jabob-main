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
// Adapted from esp-tflite-micro/examples/micro_speech (single-frame path only).

#include "micro_features_generator.h"

#include <algorithm>
#include <cstring>

#include "audio_preprocessor_int8_model_data.h"
#include "micro_model_settings.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
const tflite::Model* g_model = nullptr;
tflite::MicroInterpreter* g_interpreter = nullptr;

constexpr size_t kArenaSize = 16 * 1024;
alignas(16) uint8_t g_arena[kArenaSize];

using AudioPreprocessorOpResolver = tflite::MicroMutableOpResolver<18>;

TfLiteStatus RegisterOps(AudioPreprocessorOpResolver& op_resolver) {
  TF_LITE_ENSURE_STATUS(op_resolver.AddReshape());
  TF_LITE_ENSURE_STATUS(op_resolver.AddCast());
  TF_LITE_ENSURE_STATUS(op_resolver.AddStridedSlice());
  TF_LITE_ENSURE_STATUS(op_resolver.AddConcatenation());
  TF_LITE_ENSURE_STATUS(op_resolver.AddMul());
  TF_LITE_ENSURE_STATUS(op_resolver.AddAdd());
  TF_LITE_ENSURE_STATUS(op_resolver.AddDiv());
  TF_LITE_ENSURE_STATUS(op_resolver.AddMinimum());
  TF_LITE_ENSURE_STATUS(op_resolver.AddMaximum());
  TF_LITE_ENSURE_STATUS(op_resolver.AddWindow());
  TF_LITE_ENSURE_STATUS(op_resolver.AddFftAutoScale());
  TF_LITE_ENSURE_STATUS(op_resolver.AddRfft());
  TF_LITE_ENSURE_STATUS(op_resolver.AddEnergy());
  TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBank());
  TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBankSquareRoot());
  TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBankSpectralSubtraction());
  TF_LITE_ENSURE_STATUS(op_resolver.AddPCAN());
  TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBankLog());
  return kTfLiteOk;
}
}  // namespace

TfLiteStatus InitializeMicroFeatures() {
  g_model = tflite::GetModel(g_audio_preprocessor_int8_tflite);
  if (g_model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf(
        "Audio preprocessor schema mismatch: model=%d expected=%d",
        g_model->version(), TFLITE_SCHEMA_VERSION);
    return kTfLiteError;
  }

  static AudioPreprocessorOpResolver op_resolver;
  if (RegisterOps(op_resolver) != kTfLiteOk) {
    return kTfLiteError;
  }

  static tflite::MicroInterpreter static_interpreter(
      g_model, op_resolver, g_arena, kArenaSize);
  g_interpreter = &static_interpreter;

  if (g_interpreter->AllocateTensors() != kTfLiteOk) {
    MicroPrintf("Audio preprocessor AllocateTensors failed");
    return kTfLiteError;
  }
  return kTfLiteOk;
}

TfLiteStatus GenerateFeature(const int16_t* audio_data,
                             int audio_data_size,
                             int8_t* feature_output) {
  if (g_interpreter == nullptr) {
    return kTfLiteError;
  }
  TfLiteTensor* input = g_interpreter->input(0);
  TfLiteTensor* output = g_interpreter->output(0);
  std::copy_n(audio_data, audio_data_size,
              tflite::GetTensorData<int16_t>(input));
  if (g_interpreter->Invoke() != kTfLiteOk) {
    MicroPrintf("Audio preprocessor invoke failed");
    return kTfLiteError;
  }
  std::copy_n(tflite::GetTensorData<int8_t>(output), kFeatureSize,
              feature_output);
  return kTfLiteOk;
}
