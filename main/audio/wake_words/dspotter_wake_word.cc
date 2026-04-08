#include "dspotter_wake_word.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// 在包含 DSpotter 头文件之前处理 min/max 宏冲突
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include "CybModelInfor.h"
#include "DSpotterSDKApi.h"
#include "DSpotterSDKApi_Const.h"

#include "esp_log.h"
#include "esp_partition.h"

#include "application.h"
#include "opus_encoder.h"

static const char* TAG = "DSpotterWakeWord";

namespace {
constexpr size_t kLicenseReadSize = 256;

extern const uint8_t kDSpotterModelStart[] asm("_binary_HeyJabra_Lv3_Enc1_pack_WithTxt_bin_start");
extern const uint8_t kDSpotterModelEnd[] asm("_binary_HeyJabra_Lv3_Enc1_pack_WithTxt_bin_end");
}

extern "C" void* PortMalloc(size_t size) {
    return malloc(size);
}

extern "C" void* PortCalloc(size_t num, size_t size) {
    return calloc(num, size);
}

extern "C" void PortFree(void* ptr) {
    free(ptr);
}

DSpotterWakeWord::DSpotterWakeWord() {
    audio_buffer_.resize(kMaxBufferSize);
    event_group_ = xEventGroupCreate();
}

DSpotterWakeWord::~DSpotterWakeWord() {
    Stop();

    if (dspotter_handle_) {
        DSpotter_Release((HANDLE)dspotter_handle_);
        dspotter_handle_ = nullptr;
    }

    if (cyb_model_handle_) {
        CybModelRelease((HANDLE)cyb_model_handle_);
        cyb_model_handle_ = nullptr;
    }

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
    if (!InitializeDSpotter()) {
        ESP_LOGE(TAG, "DSpotter initialize failed");
        return;
    }

    ESP_LOGI(TAG, "DSpotter initialized successfully");
}

void DSpotterWakeWord::Feed(const std::vector<int16_t>& data) {
    if (!detection_running_ || !dspotter_handle_ || data.empty()) {
        return;
    }

    StoreWakeWordData(data.data(), data.size());

    int ret = DSpotter_AddSample(
        (HANDLE)dspotter_handle_,
        const_cast<SHORT*>(reinterpret_cast<const SHORT*>(data.data())),
        static_cast<INT>(data.size()));

    if (ret == DSPOTTER_ERR_NeedMoreSample) {
        return;
    }

    if (ret == DSPOTTER_SUCCESS) {
        int command_index = DSpotter_GetResult((HANDLE)dspotter_handle_);
        int confidence = 0;
        int sg_diff = 0;
        int cmd_energy = DSpotter_GetCmdEnergy((HANDLE)dspotter_handle_);
        DSpotter_GetResultScore((HANDLE)dspotter_handle_, &confidence, &sg_diff, nullptr);

        char command[64] = {0};
        int map_id = -1;
        if (cyb_model_handle_ && command_index >= 0) {
            CybModelGetCommandInfo(
                (HANDLE)cyb_model_handle_,
                active_group_index_,
                command_index,
                command,
                sizeof(command),
                &map_id);
        }

        last_detected_wake_word_ = CONFIG_DSPOTTER_WAKE_WORD_DISPLAY;
        ESP_LOGI(TAG,
                 "DSpotter detected wake word: cmd=%s index=%d score=%d sg_diff=%d energy=%d map_id=%d",
                 command,
                 command_index,
                 confidence,
                 sg_diff,
                 cmd_energy,
                 map_id);

        DSpotter_Continue((HANDLE)dspotter_handle_);
        Stop();

        if (wake_word_detected_callback_) {
            wake_word_detected_callback_(last_detected_wake_word_);
        }
        return;
    }

    if (ret == DSPOTTER_ERR_Expired) {
        ESP_LOGE(TAG, "DSpotter trial expired, stop wake word detection");
        Stop();
        return;
    }

    ESP_LOGW(TAG, "DSpotter_AddSample returned %d", ret);
}

void DSpotterWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void DSpotterWakeWord::Start() {
    if (!dspotter_handle_) {
        ESP_LOGW(TAG, "DSpotter not initialized, skip start");
        return;
    }

    detection_running_ = true;
    DSpotter_Reset((HANDLE)dspotter_handle_);
    ESP_LOGI(TAG, "DSpotter started");
}

void DSpotterWakeWord::Stop() {
    detection_running_ = false;
    if (dspotter_handle_) {
        DSpotter_Reset((HANDLE)dspotter_handle_);
    }
    ESP_LOGI(TAG, "DSpotter stopped");
}

size_t DSpotterWakeWord::GetFeedSize() {
    return kFrameSample;
}

void DSpotterWakeWord::EncodeWakeWordData() {
    {
        std::lock_guard<std::mutex> lock(wake_word_mutex_);
        wake_word_opus_.clear();
    }

    auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    encoder->SetComplexity(0);

    for (auto& pcm : wake_word_pcm_) {
        encoder->Encode(std::move(pcm), [this](std::vector<uint8_t>&& opus) {
            std::lock_guard<std::mutex> queue_lock(wake_word_mutex_);
            wake_word_opus_.emplace_back(std::move(opus));
            wake_word_cv_.notify_all();
        });
    }
    wake_word_pcm_.clear();

    // 空包表示编码结束
    wake_word_opus_.emplace_back(std::vector<uint8_t>());
    wake_word_cv_.notify_all();
}

bool DSpotterWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}

void DSpotterWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

bool DSpotterWakeWord::InitializeDSpotter() {
    if (dspotter_handle_) {
        DSpotter_Release((HANDLE)dspotter_handle_);
        dspotter_handle_ = nullptr;
    }
    if (cyb_model_handle_) {
        CybModelRelease((HANDLE)cyb_model_handle_);
        cyb_model_handle_ = nullptr;
    }

    int model_init_err = 0;
    cyb_model_handle_ = CybModelInit((const BYTE*)kDSpotterModelStart, nullptr, 0, &model_init_err);
    if (!cyb_model_handle_) {
        ESP_LOGE(TAG, "CybModelInit failed, error=%d", model_init_err);
        return false;
    }

    BYTE* model_group[1] = {
        (BYTE*)CybModelGetGroup((HANDLE)cyb_model_handle_, active_group_index_)
    };
    if (model_group[0] == nullptr) {
        ESP_LOGE(TAG, "Failed to get DSpotter model group %d", active_group_index_);
        return false;
    }

    int mem_size = DSpotter_GetMemoryUsage_Multi(
        (BYTE*)CybModelGetBase((HANDLE)cyb_model_handle_),
        model_group,
        1,
        kMaxCommandTimeFrames);
    if (mem_size <= 0) {
        ESP_LOGE(TAG, "DSpotter_GetMemoryUsage_Multi failed, ret=%d", mem_size);
        return false;
    }
    dspotter_memory_.assign(static_cast<size_t>(mem_size), 0);

    const esp_partition_t* lic_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "dspotter");
    if (!lic_part) {
        lic_part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_ANY,
            "license_data");
    }
    if (!lic_part) {
        ESP_LOGE(TAG, "DSpotter license partition not found (label: dspotter/license_data)");
        return false;
    }

    if (license_data_) {
        free(license_data_);
        license_data_ = nullptr;
    }

    license_size_ = lic_part->size < kLicenseReadSize ? lic_part->size : kLicenseReadSize;
    license_data_ = (uint8_t*)malloc(license_size_);
    if (!license_data_) {
        ESP_LOGE(TAG, "Failed to allocate license buffer, size=%u", (unsigned)license_size_);
        return false;
    }

    esp_err_t err = esp_partition_read(lic_part, 0, license_data_, license_size_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read DSpotter license, err=0x%x", err);
        return false;
    }

    int dspotter_init_err = DSPOTTER_SUCCESS;
    dspotter_handle_ = DSpotter_Init_Multi(
        (BYTE*)CybModelGetBase((HANDLE)cyb_model_handle_),
        model_group,
        1,
        kMaxCommandTimeFrames,
        dspotter_memory_.data(),
        mem_size,
        license_data_,
        static_cast<INT>(license_size_),
        &dspotter_init_err);
    if (!dspotter_handle_) {
        ESP_LOGE(TAG, "DSpotter_Init_Multi failed, error=%d", dspotter_init_err);
        return false;
    }

    ESP_LOGI(TAG, "DSpotter version: %s", DSpotter_VerInfo());
    ESP_LOGI(TAG,
             "DSpotter initialized: model_size=%u bytes, work_mem=%d bytes, license=%u bytes",
             (unsigned)(kDSpotterModelEnd - kDSpotterModelStart),
             mem_size,
             (unsigned)license_size_);
    return true;
}