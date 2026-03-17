#ifndef USB_AUDIO_CODEC_H
#define USB_AUDIO_CODEC_H

#include "audio_codec.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <usb_host_uac.h>
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

/**
 * @brief USB Audio Class (UAC) 编解码器
 * 
 * 支持 ESP32-S3 作为 USB Host，连接 USB 麦克风/扬声器设备
 * 兼容 UAC1.0 和 UAC2.0 标准
 */
class UsbAudioCodec : public AudioCodec {
public:
    /**
     * @brief 构造函数
     * @param input_sample_rate 期望的输入采样率（Hz）
     * @param output_sample_rate 期望的输出采样率（Hz）
     */
    UsbAudioCodec(int input_sample_rate, int output_sample_rate);
    ~UsbAudioCodec();

    void Start() override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;
    void SetOutputVolume(int volume) override;

protected:
#ifdef CONFIG_IDF_TARGET_ESP32S3
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;
#else
    // 非 ESP32-S3 平台返回错误
    int Read(int16_t* dest, int samples) override { return 0; }
    int Write(const int16_t* data, int samples) override { return 0; }
#endif

private:
#ifdef CONFIG_IDF_TARGET_ESP32S3
    usb_host_uac_handle_t uac_device_ = nullptr;      ///< UAC 设备句柄
    uac_stream_handle_t uac_rx_stream_ = nullptr;     ///< RX 流（麦克风输入）
    uac_stream_handle_t uac_tx_stream_ = nullptr;     ///< TX 流（扬声器输出）
    
    TaskHandle_t usb_event_task_handle_ = nullptr;    ///< USB 事件处理任务
    EventGroupHandle_t usb_event_group_ = nullptr;    ///< USB 事件组
    
    std::atomic<bool> device_connected_{false};       ///< 设备连接状态
    std::atomic<bool> rx_stream_started_{false};      ///< RX 流运行状态
    std::atomic<bool> tx_stream_started_{false};      ///< TX 流运行状态
    
    uac_device_info_t device_info_;                    ///< 设备信息缓存
    
    /// USB 音频设备事件回调
    static bool usb_event_callback(const usb_host_uac_event_t* event, void* user_data);
    
    /// 初始化 USB Host 和 UAC 设备
    esp_err_t InitializeUsbHost();
    
    /// 配置并打开 RX 流（麦克风）
    esp_err_t OpenRxStream();
    
    /// 配置并打开 TX 流（扬声器）
    esp_err_t OpenTxStream();
    
    /// 关闭所有流
    void CloseStreams();
    
    /// USB 事件处理任务
    static void UsbEventTask(void* arg);
    
    /// 等待设备连接
    bool WaitForDevice(int timeout_ms = 5000);
#endif

    bool usb_host_initialized_ = false;              ///< USB Host 是否已初始化
    int retry_count_ = 0;                            ///< 重连尝试次数
    static constexpr int MAX_RETRY_COUNT = 3;        ///< 最大重连次数
};

#endif // USB_AUDIO_CODEC_H
