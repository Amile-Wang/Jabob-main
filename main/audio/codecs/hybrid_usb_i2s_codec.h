#ifndef HYBRID_USB_I2S_CODEC_H
#define HYBRID_USB_I2S_CODEC_H

#include "audio_codec.h"
#include <usb/usb_host.h>
#include "usb/uac_host.h"
#include <deque>

// USB音频缓冲区大小定义
#define USB_AUDIO_BUFFER_SIZE  (16 * 1024)  // 调整为16KB，支持48kHz采样率和60ms帧长

// USB事件标志
#define USB_EVENT_CONNECTED     (1 << 0)

class HybridUsbI2sCodec : public AudioCodec {
public:
    // 修改构造函数：移除 input_sample_rate 参数，使用默认值 0 表示未确定
    HybridUsbI2sCodec(int output_sample_rate, 
                     gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout);
    
    virtual ~HybridUsbI2sCodec() override;

    void Start() override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;
    void SetOutputVolume(int volume) override;

protected:
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;

private:
    // USB 麦克风相关
    static void UacDriverEventCallback(uint8_t addr, uint8_t iface_num,
                                      const uac_host_driver_event_t event, void* arg);
    static void UacDeviceEventCallback(uac_host_device_handle_t uac_device_handle,
                                      const uac_host_device_event_t event, void* arg);

    // 异步数据处理任务
    static void UsbDataProcessingTask(void* arg);

    esp_err_t InitializeUsbHost();
    esp_err_t InitializeI2sSpeaker();
    esp_err_t OpenUsbMicrophone();
    esp_err_t CloseUsbMicrophone();
    esp_err_t CloseI2sSpeaker();
    bool WaitForUsbDevice(int timeout_ms);

    // 添加USB设备状态检查函数
    void CheckUsbDeviceStatus();

    // 添加辅助函数：根据设备能力选择最佳采样率
    uint32_t SelectBestSampleRate(const uac_host_dev_alt_param_t& alt_params);

    // 异步数据处理方法
    void ProcessUsbData();
    esp_err_t ReadUsbData(int16_t* buffer, size_t requested_samples, size_t& samples_read);

    // 新增：位深处理相关方法
    void ConvertAudioDataTo16Bit(const uint8_t* src_data, size_t src_bytes, int16_t* dest, size_t samples);

    // I2S 扬声器相关

    // 成员变量
    EventGroupHandle_t usb_event_group_;

    // USB 相关成员变量
    bool usb_initialized_ = false;
    uac_host_device_handle_t uac_rx_device_ = nullptr;
    TaskHandle_t usb_host_task_handle_ = nullptr;  // USB Host 任务句柄
    TaskHandle_t usb_data_task_handle_ = nullptr;  // USB数据处理任务句柄
    uint8_t usb_device_addr_ = 0;        // USB 设备地址
    uint8_t usb_iface_num_ = 0;          // USB 接口号
    bool usb_microphone_ready_ = false;   // USB 麦克风就绪标志

    // USB音频数据缓冲区，用于暂存从USB读取的原始音频数据
    std::deque<int16_t> usb_audio_buffer_;
    size_t buffer_capacity_;   // 缓冲区容量
    mutable std::mutex buffer_mutex_;  // 缓冲区保护互斥锁，声明为mutable
    bool buffer_overflowed_;  // 缓冲区溢出标志

    // 新增：设备位深信息
    uint8_t device_bit_depth_ = 16;  // 默认16位，从设备能力中获取

    // 统计信息
    uint32_t usb_read_count_;       // USB读取次数
    uint32_t usb_overflow_count_;   // USB缓冲区溢出次数
    uint32_t samples_processed_;     // 处理的样本总数

    // I2S 相关
    i2s_chan_handle_t i2s_tx_handle_;
    gpio_num_t spk_bclk_;
    gpio_num_t spk_ws_;
    gpio_num_t spk_dout_;

    // 状态标志
    bool i2s_initialized_;

    // 添加获取USB缓冲区大小的方法，用于监控
    size_t GetUsbBufferSize() const {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        return usb_audio_buffer_.size();
    }

};

#endif // HYBRID_USB_I2S_CODEC_H