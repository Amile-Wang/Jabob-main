#ifndef HYBRID_USB_I2S_CODEC_H
#define HYBRID_USB_I2S_CODEC_H

#include "audio_codec.h"
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <usb/uac_host.h>
#include <usb/usb_host.h>
#include <freertos/event_groups.h>

#define USB_EVENT_CONNECTED    (1 << 0)
#define USB_EVENT_RX_READY     (1 << 1)

/**
 * @brief 混合 USB-I2S 音频编解码器
 * 
 * 直接基于 ESP-IDF USB Host UAC 组件和 I2S 组件实现
 * - 麦克风: 通过 USB UAC Host 从 USB 麦克风设备获取音频
 * - 扬声器: 通过 I2S 接口输出到扬声器
 */
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
    esp_err_t InitializeUsbHost();
    esp_err_t InitializeI2sSpeaker();
    esp_err_t OpenUsbMicrophone();
    esp_err_t CloseUsbMicrophone();
    esp_err_t CloseI2sSpeaker();
    bool WaitForUsbDevice(int timeout_ms);  // 添加函数声明
    
    // 添加辅助函数：根据设备能力选择最佳采样率
    uint32_t SelectBestSampleRate(const uac_host_dev_alt_param_t& alt_params);
    
    // I2S 扬声器相关
    
    // 成员变量
    EventGroupHandle_t usb_event_group_;
    
    // USB 相关成员变量
    bool usb_initialized_ = false;
    usb_host_client_handle_t usb_client_handle_ = nullptr;
    uac_host_device_handle_t uac_rx_device_ = nullptr;
    TaskHandle_t usb_host_task_handle_ = nullptr;  // 添加 USB Host 任务句柄
    uint8_t usb_device_addr_ = 0;        // USB 设备地址
    uint8_t usb_iface_num_ = 0;          // USB 接口号
    bool usb_microphone_ready_ = false;   // USB 麦克风就绪标志
    
    // I2S 相关
    i2s_chan_handle_t i2s_tx_handle_;
    gpio_num_t spk_bclk_;
    gpio_num_t spk_ws_;
    gpio_num_t spk_dout_;
    int output_sample_rate_;
    
    // 状态标志
    bool i2s_initialized_;

};

#endif // HYBRID_USB_I2S_CODEC_H