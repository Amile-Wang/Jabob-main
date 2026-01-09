#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "pwm/pwm_servo.h"
#include "power_save_timer.h"
#include "adc_battery_monitor.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <assets/lang_config.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"

static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardLCD"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

class CompactWifiBoardLCD : public WifiBoard {
private:
 
    Button boot_button_;
    LcdDisplay* display_;
    pwm_servo* pwm_servo_;
    // Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    std::unique_ptr<PowerSaveTimer> power_save_timer_; 
    uint8_t saved_brightness_ = 100; // 添加用于保存亮度的变量
#ifdef BATTERY_ADC_UNIT
    std::unique_ptr<AdcBatteryMonitor> battery_monitor_;
#endif

public:
    // 实现获取PowerSaveTimer的方法
    PowerSaveTimer* GetPowerSaveTimer() override { 
        return power_save_timer_.get(); 
    }


    static void InitializeButtonsTask(void* param) {
        CompactWifiBoardLCD* board = static_cast<CompactWifiBoardLCD*>(param);
        
        // 添加延迟以避免与其他初始化过程竞争资源
        vTaskDelay(pdMS_TO_TICKS(1500));

        // 注册设备状态变化监听器
        DeviceStateEventManager::GetInstance().RegisterStateChangeCallback([board](DeviceState previous_state, DeviceState new_state) {
            ESP_LOGI(TAG, "Device state changed from %d to %d", previous_state, new_state);
            if (new_state == kDeviceStateIdle) {
                // 设备进入空闲状态，启用电源管理定时器
                if (board->power_save_timer_) {
                    board->power_save_timer_->SetEnabled(true);
                    ESP_LOGI(TAG, "Power save timer enabled due to entering idle state");
                }
            } else {
                // 设备离开空闲状态，唤醒电源管理器
                if (board->power_save_timer_) {
                    board->power_save_timer_->WakeUp();
                    ESP_LOGI(TAG, "Power save timer woken up due to leaving idle state");
                }
            }
        });
        
        board->boot_button_.OnPressDown([board]() {
            ESP_LOGI(TAG, "Boot button pressed");
            board->GetDisplay()->ShowNotification("Boot button pressed");
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                board->ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        
// 只有当按钮指针不为空时才注册回调
        // if (board->volume_up_button_) {
            board->volume_up_button_.OnClick([board]() {
                auto codec = board->GetAudioCodec();
                auto volume = codec->output_volume() + 10;
                if (volume > 100) {
                    volume = 100;
                }
                codec->SetOutputVolume(volume);
                ESP_LOGI(TAG, "Volume up button pressed, volume: %d", volume);
                
                
                board->GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
                
            });
        // }

        // 只有当按钮指针不为空时才注册回调
        // if (board->volume_down_button_) {
            board->volume_down_button_.OnClick([board]() {
                auto codec = board->GetAudioCodec();
                auto volume = codec->output_volume() - 10;
                if (volume < 0) {
                    volume = 0;
                }
                codec->SetOutputVolume(volume);
                ESP_LOGI(TAG, "Volume down button pressed, volume: %d", volume);
                board->GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
                

            });
        // }

    // #if CONFIG_USE_DEVICE_AEC
    //     board->boot_button_.OnDoubleClick([board]() {
    //         auto& app = Application::GetInstance();
    //         if (app.GetDeviceState() == kDeviceStateIdle) {
    //             app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
    //         }
    //     });
    // #endif

        // 任务完成，删除自身
        vTaskDelete(NULL);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);
 

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY
//                                     {
//                                         .text_font = &font_puhui_16_4,
//                                         .icon_font = &font_awesome_16_4,
// #if CONFIG_USE_WECHAT_MESSAGE_STYLE
//                                         .emoji_font = font_emoji_32_init(),
// #else
//                                         .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
// #endif
//                                     }
        );
    }


 
    // void InitializeButtons() {

    //     // 添加延迟以避免与其他初始化过程竞争资源
    //     // vTaskDelay(pdMS_TO_TICKS(5000));
    //     boot_button_.OnClick([this]() {
    //         auto& app = Application::GetInstance();
    //         if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
    //             ResetWifiConfiguration();
    //         }
    //         app.ToggleChatState();
    //     });

    //     volume_up_button_.OnClick([this]() {
    //         auto codec = GetAudioCodec();
    //         auto volume = codec->output_volume() + 10;
    //         if (volume > 100) {
    //             volume = 100;
    //         }
    //         codec->SetOutputVolume(volume);
    //         // ESP_LOGI(TAG, "Volume up button pressed");
            
    //         // GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
    //     });


    //     volume_down_button_.OnClick([this]() {
    //         auto codec = GetAudioCodec();
    //         auto volume = codec->output_volume() - 10;
    //         if (volume < 0) {
    //             volume = 0;
    //         }
    //         codec->SetOutputVolume(volume);
    //         // ESP_LOGI(TAG, "Volume down button pressed");
    //         // GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
    //     });


    

    // #if CONFIG_USE_DEVICE_AEC
    //     boot_button_.OnDoubleClick([this]() {
    //         auto& app = Application::GetInstance();
    //         if (app.GetDeviceState() == kDeviceStateIdle) {
    //             app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
    //         }
    //     });
    // #endif

    // }

    // 物联网初始化，添加对 AI 可见设备
    // void InitializeTools() {
    //     static LampController lamp(LAMP_GPIO);
    // }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO),
        // touch_button_(TOUCH_BUTTON_GPIO), 
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO)
        {
        InitializeSpi();

        InitializeLcdDisplay();

        // gpio_set_level(AUDIO_I2S_PDM_MIC_GPIO_LR, 1);

#ifdef BATTERY_ADC_UNIT
        // 初始化电池监控器（需要在config.h中定义相关参数）
        battery_monitor_ = std::make_unique<AdcBatteryMonitor>(
            BATTERY_ADC_UNIT, 
            BATTERY_ADC_CHANNEL, 
            BATTERY_UPPER_RESISTOR, 
            BATTERY_LOWER_RESISTOR, 
            BATTERY_CHARGING_PIN
        );
#endif

        // 初始化电源管理定时器，20秒后进入节能模式
        power_save_timer_ = std::make_unique<PowerSaveTimer>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, 20, -1);
        power_save_timer_->SetEnabled(true);
        ESP_LOGI(TAG, "Power save timer initialized and enabled");

        // 注册进入和退出睡眠模式的回调
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Entering sleep mode");
            auto& board = Board::GetInstance();

            // 保存当前亮度并在进入睡眠时将亮度设为0
            auto backlight = board.GetBacklight();
            if (backlight) {
                saved_brightness_ = backlight->brightness();
                backlight->SetBrightness(0);
                ESP_LOGI(TAG, "Brightness saved (%d) and set to 0", saved_brightness_);
            }

            // 使用Application的Schedule方法确保在主线程中执行UI更新
            Application::GetInstance().Schedule([&board]() {
                auto& board = Board::GetInstance();
                auto display = board.GetDisplay();
                
                ESP_LOGI(TAG, "Updating sleep mode UI");
                if (display) {
                    ESP_LOGI(TAG, "Calling SetEmotion with 'relaxed'");
                    display->SetEmotion("relaxed");
                    ESP_LOGI(TAG, "Called SetEmotion with 'relaxed'");
                    display->ShowNotification(Lang::Strings::SLEEPING);
                    ESP_LOGI(TAG, "Sleep mode UI updated");
                } else {
                    ESP_LOGW(TAG, "Display is null");
                }
            });
            
            // 可以在这里添加进入睡眠模式时需要执行的操作
        });

        power_save_timer_->OnExitSleepMode([this]() {
            ESP_LOGI(TAG, "Exiting sleep mode");
            auto& board = Board::GetInstance();
            
            // 重置睡眠倒计时为20秒，避免默认状态下的倒计时长度受到MCP影响
            if (power_save_timer_) {
                power_save_timer_->SetSleepDelay(20);
                ESP_LOGI(TAG, "Reset sleep delay to 20 seconds");
            }

            
             // 恢复之前的亮度
            auto backlight = board.GetBacklight();
            if (backlight) {
                backlight->SetBrightness(saved_brightness_);
                ESP_LOGI(TAG, "Brightness restored to %d", saved_brightness_);
            }
            
            // 使用Application的Schedule方法确保在主线程中执行UI更新
            Application::GetInstance().Schedule([&board]() {
                auto& board = Board::GetInstance();
                auto display = board.GetDisplay();
                
                ESP_LOGI(TAG, "Updating exit sleep mode UI");
                if (display) {
                    ESP_LOGI(TAG, "Calling SetEmotion with 'neutral'");
                    display->SetEmotion("neutral");
                    ESP_LOGI(TAG, "Called SetEmotion with 'neutral'");
                    ESP_LOGI(TAG, "Exit sleep mode UI updated");
                } else {
                    ESP_LOGW(TAG, "Display is null");
                }
            });
            // 可以在这里添加退出睡眠模式时需要执行的操作
        });

        // 在单独的任务中初始化按钮，避免阻塞主流程
        xTaskCreate(InitializeButtonsTask, "buttons_init", 4096, this, 5, NULL);
        

        // 初始化扬声器电源控制引脚，防止悬空导致扬声器电源被切断
        if (SPK_GPIO_POWERSAVE != GPIO_NUM_NC) {
            gpio_config_t spk_power_save_cfg = {};
            spk_power_save_cfg.pin_bit_mask = BIT64(SPK_GPIO_POWERSAVE);
            spk_power_save_cfg.mode = GPIO_MODE_OUTPUT;
            spk_power_save_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            spk_power_save_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            spk_power_save_cfg.intr_type = GPIO_INTR_DISABLE;
            ESP_ERROR_CHECK(gpio_config(&spk_power_save_cfg));
            
            // 将引脚设置为高电平，确保扬声器电源正常供电
            ESP_ERROR_CHECK(gpio_set_level(SPK_GPIO_POWERSAVE, 1));
        }

        // 初始化扬声器电源控制引脚，防止悬空导致扬声器电源被切断
        if (AUDIO_I2S_PDM_MIC_GPIO_LR != GPIO_NUM_NC) {
            gpio_config_t spk_power_save_cfg = {};
            spk_power_save_cfg.pin_bit_mask = BIT64(AUDIO_I2S_PDM_MIC_GPIO_LR);
            spk_power_save_cfg.mode = GPIO_MODE_OUTPUT;
            spk_power_save_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            spk_power_save_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            spk_power_save_cfg.intr_type = GPIO_INTR_DISABLE;
            ESP_ERROR_CHECK(gpio_config(&spk_power_save_cfg));
            
            // 将引脚设置为高电平，确保扬声器电源正常供电
            ESP_ERROR_CHECK(gpio_set_level(AUDIO_I2S_PDM_MIC_GPIO_LR, 1));
        }

        
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            // GetBacklight()->RestoreBrightness();
            GetBacklight()->SetBrightness(50, true); 
        }
        
        pwm_servo_ = &pwm_servo::GetInstance();//初始化舵机

    }

    // virtual Led* GetLed() override {
    //             // 暂时禁用LED功能以避免冲突
    //     static NoLed led;
    //     // static SingleLed led(BUILTIN_LED_GPIO);
    //     return &led;
    // }



    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#elif defined(AUDIO_I2S_METHOD_SIMPLEX_PDM)
        static NoAudioCodecSimplexPdm audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, 
            AUDIO_I2S_PDM_MIC_GPIO_SCK, AUDIO_I2S_PDM_MIC_GPIO_DIN);
#elif defined(AUDIO_I2S_METHOD_SIMPLEX_I2S_PDM)
        // gpio_set_level(AUDIO_I2S_PDM_MIC_GPIO_LR, 1);

        static NoAudioCodecSimplexI2sPdm audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, 
            AUDIO_I2S_PDM_MIC_GPIO_SCK, AUDIO_I2S_PDM_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    // virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
    // // 临时实现，返回模拟电池电量值，用于测试显示功能
    // // 后续添加ADC分压电路检测时，需要修改此部分代码
    // level = 0;          // 模拟电量0%
    // charging = true;    // 在充电状态
    // discharging = false;  
    // return true;         // 返回true表示成功获取电池状态
    // }

    // virtual pwm_servo* GetPwmServo() override {
    //     return pwm_servo_;
    // }
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
    // 当ADC引脚定义时，使用真实的ADC读数；否则使用模拟值
#ifdef BATTERY_ADC_UNIT
    if (battery_monitor_) {
        level = battery_monitor_->GetBatteryLevel();
        charging = battery_monitor_->IsCharging();
        discharging = battery_monitor_->IsDischarging();
    } else {
        // 如果电池监控器未初始化，则返回默认值
        level = 60;
        charging = false;
        discharging = true;
    }
#else
    // 临时实现，返回模拟电池电量值，用于测试显示功能
    // 后续添加ADC分压电路检测时，需要修改此部分代码
    level = 30;          // 模拟电量0%
    charging = true;    // 在充电状态
    discharging = false;  
#endif
    return true;         // 返回true表示成功获取电池状态
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);
