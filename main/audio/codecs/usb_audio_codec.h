#ifndef USB_AUDIO_CODEC_H
#define USB_AUDIO_CODEC_H

#include "audio_codec.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <usb/uac_host.h>
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
    // 移除友元函数声明，改为通过公共接口或成员变量访问
    
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
    
    // 设备信息成员变量 - 受保护，供回调函数访问
    uint8_t rx_device_addr_ = 0;                         ///< RX 设备地址
    uint8_t rx_iface_num_ = 0;                           ///< RX 接口号
    uint8_t tx_device_addr_ = 0;                         ///< TX 设备地址  
    uint8_t tx_iface_num_ = 0;                           ///< TX 接口号
    
    TaskHandle_t usb_event_task_handle_ = nullptr;       ///< USB 事件处理任务
    EventGroupHandle_t usb_event_group_ = nullptr;       ///< USB 事件组
    
    std::atomic<bool> device_connected_{false};          ///< 设备连接状态
    std::atomic<bool> rx_stream_started_{false};         ///< RX 流运行状态
    std::atomic<bool> tx_stream_started_{false};         ///< TX 流运行状态
#else
    // 非 ESP32-S3 平台返回错误
    int Read(int16_t* dest, int samples) override { return 0; }
    int Write(const int16_t* data, int samples) override { return 0; }
#endif

private:
#ifdef CONFIG_IDF_TARGET_ESP32S3
    uac_host_device_handle_t uac_device_ = nullptr;      ///< UAC 设备句柄
    uac_host_device_handle_t uac_rx_stream_ = nullptr;   ///< RX 流（麦克风输入）
    uac_host_device_handle_t uac_tx_stream_ = nullptr;   ///< TX 流（扬声器输出）
    
    uac_host_dev_info_t device_info_;                    ///< 设备信息缓存
    
    /// 初始化 USB Host 和 UAC 设备
    esp_err_t InitializeUsbHost();
    
    /// 配置并打开 RX 流（麦克风）
    esp_err_t OpenRxStream(uint8_t addr, uint8_t iface_num);
    
    /// 配置并打开 TX 流（扬声器）
    esp_err_t OpenTxStream(uint8_t addr, uint8_t iface_num);
    
    /// 关闭所有流
    void CloseStreams();
    
    /// USB 事件处理任务
    static void UsbEventTask(void* arg);
    
    /// 等待设备连接
    bool WaitForDevice(int timeout_ms = 5000);
#endif

    // 这些成员变量需要在所有平台上都存在，因为构造函数和析构函数会访问它们
    bool usb_host_initialized_ = false;              ///< USB Host 是否已初始化
    int retry_count_ = 0;                            ///< 重连尝试次数
    static constexpr int MAX_RETRY_COUNT = 3;        ///< 最大重连次数
};

// 在头文件外部声明回调函数为 C 链接
#ifdef CONFIG_IDF_TARGET_ESP32S3
extern "C" void uac_driver_event_callback(uint8_t addr, uint8_t iface_num,
                                      const uac_host_driver_event_t event, void *arg);
#endif

#endif // USB_AUDIO_CODEC_H