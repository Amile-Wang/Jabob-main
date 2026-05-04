#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <memory>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <opus_resampler.h>

#include "audio_codec.h"
#include "audio_processor.h"
#include "processors/audio_debugger.h"
#include "wake_word.h"
#include "protocol.h"


/*
 * There are two types of audio data flow:
 * 1. (MIC) -> [Processors] -> {Encode Queue} -> [Opus Encoder] -> {Send Queue} -> (Server)
 * 2. (Server) -> {Decode Queue} -> [Opus Decoder] -> {Playback Queue} -> (Speaker)
 *
 * We use one task for MIC / Speaker / Processors, and one task for Opus Encoder / Opus Decoder.
 * 
 * Decode Queue and Send Queue are the main queues, because Opus packets are quite smaller than PCM packets.
 * 
 */

#define OPUS_FRAME_DURATION_MS 60
#define MAX_ENCODE_TASKS_IN_QUEUE 2
#define MAX_PLAYBACK_TASKS_IN_QUEUE 20
#define MAX_DECODE_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)  // 优化：从2400减到1200
#define MAX_SEND_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)  // 优化：从2400减到1200
#define AUDIO_TESTING_MAX_DURATION_MS 10000
#define MAX_TIMESTAMPS_IN_QUEUE 3

#define AUDIO_POWER_TIMEOUT_MS 15000
#define AUDIO_POWER_CHECK_INTERVAL_MS 1000


#define AS_EVENT_AUDIO_TESTING_RUNNING      (1 << 0)
#define AS_EVENT_WAKE_WORD_RUNNING          (1 << 1)
#define AS_EVENT_AUDIO_PROCESSOR_RUNNING    (1 << 2)
#define AS_EVENT_PLAYBACK_NOT_EMPTY         (1 << 3)

struct AudioServiceCallbacks {
    std::function<void(void)> on_send_queue_available;
    std::function<void(const std::string&)> on_wake_word_detected;
    std::function<void(bool)> on_vad_change;
    std::function<void(void)> on_audio_testing_queue_full;
};


enum AudioTaskType {
    kAudioTaskTypeEncodeToSendQueue,
    kAudioTaskTypeEncodeToTestingQueue,
    kAudioTaskTypeDecodeToPlaybackQueue,
};

struct AudioTask {
    AudioTaskType type;
    std::vector<int16_t> pcm;
    uint32_t timestamp;
    bool is_sound_effect = false;  // 解码自本地提示音的 PCM；ResetDecoder 时保留不清
};

struct DebugStatistics {
    uint32_t input_count = 0;
    uint32_t decode_count = 0;
    uint32_t encode_count = 0;
    uint32_t playback_count = 0;
};

class AudioService {
public:
    AudioService();
    ~AudioService();

    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();
    void EncodeWakeWord();
    std::unique_ptr<AudioStreamPacket> PopWakeWordPacket();
    const std::string& GetLastWakeWord() const;
    
    // 添加核心任务监控函数
    void PrintCoreTaskInfo(int core_id);
    
    bool ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples);
    
    // 添加后备数据消费者任务声明
    void BackupDataConsumerTask();
    
    // 添加 IsVoiceDetected 公共方法
    bool IsVoiceDetected() const { return voice_detected_; }
    
    bool IsIdle();
    bool IsWakeWordRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING; }
    bool IsAudioProcessorRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING; }
    
    // 添加 USB 设备就绪状态检查方法
    bool IsUsbDeviceReady() const { return usb_device_ready_; }

    void EnableWakeWordDetection(bool enable);
    void EnableVoiceProcessing(bool enable);
    void EnableAudioTesting(bool enable);
    void EnableDeviceAec(bool enable);
    void SetWakeWordAudioPassthrough(bool enable);

    void SetCallbacks(AudioServiceCallbacks& callbacks);

    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait = false);
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();
    void PlaySound(const std::string_view& sound);
    void ResetDecoder();
    
    // 添加说话模式开始标志控制方法
    void SetStartToSpeakFlag(bool flag) { 
        start_to_speak_ = flag; 
        if (flag) {
            start_to_speak_time_ = xTaskGetTickCount();
        }
    }
    bool GetStartToSpeakFlag() const { return start_to_speak_; }

private:
    AudioCodec* codec_ = nullptr;
    AudioServiceCallbacks callbacks_;
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<WakeWord> wake_word_;
    std::unique_ptr<AudioDebugger> audio_debugger_;
    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;
    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;
    DebugStatistics debug_statistics_;

    EventGroupHandle_t event_group_;

    // Audio encode / decode
    TaskHandle_t audio_input_task_handle_ = nullptr;
    TaskHandle_t audio_output_task_handle_ = nullptr;
    TaskHandle_t opus_codec_task_handle_ = nullptr;
    TaskHandle_t backup_consumer_task_handle_ = nullptr;
    std::mutex audio_queue_mutex_;
    std::condition_variable audio_queue_cv_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_decode_queue_;          // 网络流入（TTS 等），可被 OpusCodecTask 直接消费
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_decode_pending_queue_;  // 网络流入的暂存：当 pipeline 里还有 sound 在跑时，TTS 帧先来这里待命；sound 全部播完那一刻整体 splice 到 audio_decode_queue_
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_sound_decode_queue_;    // 本地提示音（PlaySound 注入）；OpusCodecTask 优先消费此队列；ResetDecoder 不清此队列
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_send_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_testing_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_encode_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_playback_queue_;

    // For server AEC
    std::deque<uint32_t> timestamp_queue_;
    std::mutex timestamp_mutex_;

    bool wake_word_initialized_ = false;
    bool audio_processor_initialized_ = false;
    bool voice_detected_ = false;
    bool wake_word_audio_passthrough_enabled_ = false;
    bool service_stopped_ = true;
    bool audio_input_need_warmup_ = false;

    // 采样率状态跟踪
    int last_configured_input_sample_rate_ = 0;   // 最后配置的输入采样率
    int last_detected_input_sample_rate_ = 0;      // 最后检测到的实际输入采样率
    uint32_t sample_rate_change_count_ = 0;        // 采样率变化计数
    const uint32_t MAX_SAMPLE_RATE_CHANGES = 10;   // 最大允许的采样率变化次数
    int fallback_input_sample_rate_ = 16000;        // Fallback采样率
    bool usb_device_ready_ = false;                 // USB设备就绪标志

    // 输出采样率状态跟踪
    int last_configured_output_sample_rate_ = 0;  // 最后配置的输出采样率
    int locked_output_sample_rate_ = 0;            // 锁定的输出采样率（防止回滚）
    bool output_sample_rate_locked_ = false;          // 输出采样率锁定标志

    // 新增：说话模式开始标志和时间戳
    bool start_to_speak_ = false;
    TickType_t start_to_speak_time_ = 0;

    esp_timer_handle_t audio_power_timer_ = nullptr;
    std::chrono::steady_clock::time_point last_input_time_;
    std::chrono::steady_clock::time_point last_output_time_;
    std::vector<int16_t> wake_word_preprocessed_buffer_;

    void AudioInputTask();
    void AudioOutputTask();
    void OpusCodecTask();
    void PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm);
    void FeedWakeWordWithProcessedAudio(const std::vector<int16_t>& pcm);
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    void CheckAndUpdateAudioPowerState();
};

#endif