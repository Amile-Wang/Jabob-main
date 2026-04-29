#include "wifi_board.h"
#include "codecs/hybrid_usb_i2s_codec.h"
#include "codecs/usb_audio_codec.h"
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
 
#define TAG "CompactWifiBoardLCDUAC"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

class CompactWifiBoardLCDUAC : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
    pwm_servo* pwm_servo_;
    TouchButton touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    std::unique_ptr<PowerSaveTimer> power_save_timer_; 
    uint8_t saved_brightness_ = 100; // 添加用于保存亮度的变量
#ifdef BATTERY_ADC_UNIT
    std::unique_ptr<AdcBatteryMonitor> battery_monitor_;
#endif

    void Start_touch_monitor() {
        // 创建一个监控任务
        xTaskCreate([](void* param) {
            auto* board = static_cast<CompactWifiBoardLCDUAC*>(param);

            while(1) {
                uint32_t raw_value = board->touch_button_.read_raw_value();

                ESP_LOGD(TAG, "TouchMonitor,Raw: %" PRIu32, raw_value);
                // board->GetDisplay()->ShowNotification("Raw: " + std::to_string(raw_value));

                vTaskDelay(pdMS_TO_TICKS(3500)); // 每 500ms 检查一次
            }
        }, "TouchMonitor", 2048, this, 5, NULL);
    }

    void Start_boot_button_monitor() {
        // 进入函数立刻打一条，证明 constructor 真的跑到了这里
        ESP_LOGW(TAG, "Start_boot_button_monitor() called, BOOT_BUTTON_GPIO=%d",
                 (int)BOOT_BUTTON_GPIO);

        // 进 task 之前先在主线程读一次电平，绕开 task 创建/栈不够的可能性
        ESP_LOGW(TAG, "BOOT GPIO%d initial level=%d",
                 (int)BOOT_BUTTON_GPIO, gpio_get_level(BOOT_BUTTON_GPIO));

        // 周期性打印 BOOT_BUTTON_GPIO 当前电平，方便排查按键是否有边沿
        // - active_high=true 时：空闲应为 0（内部下拉），按下应为 1
        // - active_high=false 时：空闲应为 1（内部上拉），按下应为 0
        // 采样 50ms 一次，电平变化立即打印；无变化每 2s 打一次心跳
        BaseType_t ok = xTaskCreate([](void* param) {
            (void)param;
            // task 入口立刻打一条，证明 task 真的被调度到了
            ESP_LOGW(TAG, "BootBtnMon task started, polling GPIO%d",
                     (int)BOOT_BUTTON_GPIO);
            int last_level = -1;
            int idle_ticks = 0;
            while (1) {
                int level = gpio_get_level(BOOT_BUTTON_GPIO);
                if (level != last_level) {
                    ESP_LOGW(TAG, "BOOT GPIO%d edge: %d -> %d",
                             (int)BOOT_BUTTON_GPIO, last_level, level);
                    last_level = level;
                    idle_ticks = 0;
                } else if (++idle_ticks >= 40) {
                    ESP_LOGW(TAG, "BOOT GPIO%d idle level=%d",
                             (int)BOOT_BUTTON_GPIO, level);
                    idle_ticks = 0;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }, "BootBtnMon", 4096, nullptr, 5, NULL);
        ESP_LOGW(TAG, "BootBtnMon xTaskCreate returned %d", (int)ok);
    }

    static void InitializeButtonsTask(void* param) {
        CompactWifiBoardLCDUAC* board = static_cast<CompactWifiBoardLCDUAC*>(param);
        
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
        
        board->boot_button_.OnPressDown([]() {
            ESP_LOGI(TAG, "Boot button press_down (debug, edge detected)");
        });

        board->boot_button_.OnPressUp([]() {
            ESP_LOGI(TAG, "Boot button press_up (debug)");
        });

        board->boot_button_.OnClick([board]() {
            ESP_LOGI(TAG, "Boot button clicked (short press)");
            Application::GetInstance().ToggleChatState();
        });

        board->boot_button_.OnLongPress([board]() {
            auto& app = Application::GetInstance();
            auto display = board->GetDisplay();

            ListeningMode prev = app.GetListeningMode();
            bool entering_meeting = prev != kListeningModeMeetingAssistant;
            ListeningMode target = entering_meeting ? kListeningModeMeetingAssistant
                                                    : kListeningModeAutoStop;
            ESP_LOGW(TAG, "Boot long press: device_state=%d, mode %d -> %d (%s)",
                     (int)app.GetDeviceState(), (int)prev, (int)target,
                     entering_meeting ? "ENTER meeting" : "EXIT meeting");

            // 协议 IO（OpenAudioChannel / SendStartListening）必须在主线程跑，
            // 与 ToggleChatState 中的 Schedule 调度一致。
            app.Schedule([target]() {
                Application::GetInstance().SetListeningModePublic(target);
            });

            if (display) {
                display->SetMeetingMode(entering_meeting);
                display->ShowNotification(entering_meeting ? "进入会议模式" : "退出会议模式");
            }
        });

        // 触摸按键回调已禁用：当前 TOUCH_BUTTON_GPIO=GPIO_NUM_0，但 ESP32-S3 的 touch_pad
        // 只支持 GPIO1~14，GPIO0 不支持，触摸初始化必然失败、ISR 还会误触发，
        // 不注册 OnTouch 后 touch_event_task 收到事件会因 on_touch_==nullptr 直接 short-circuit。
        // 如以后把触摸焊盘换到合法引脚，再放开下方注释即可。
        // board->touch_button_.OnTouch([board]() {
        //     ESP_DRAM_LOGI("TouchButton", "callback");
        //     printf("触摸事件被触发了！\n");
        //     ESP_LOGI(TAG, "Touch button pressed");
        //     board->GetDisplay()->ShowNotification("Touch button pressed");
        //     auto& app = Application::GetInstance();
        //     if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
        //         board->ResetWifiConfiguration();
        //     }
        //     app.ToggleChatState();
        // });

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
        // 液晶屏控制 IO 初始化
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
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, 
                                    DISPLAY_SWAP_XY
        );
    }

public:
    CompactWifiBoardLCDUAC() :
        boot_button_(BOOT_BUTTON_GPIO, BOOT_BUTTON_ACTIVE_HIGH),
        touch_button_(TOUCH_BUTTON_GPIO, TOUCH_BUTTON_THRESHOLD),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO)
        {
        InitializeSpi();
        InitializeLcdDisplay();

#ifdef BATTERY_ADC_UNIT
        // 初始化电池监控器（需要在 config.h 中定义相关参数）
        battery_monitor_ = std::make_unique<AdcBatteryMonitor>(
            BATTERY_ADC_UNIT, 
            BATTERY_ADC_CHANNEL, 
            BATTERY_UPPER_RESISTOR, 
            BATTERY_LOWER_RESISTOR,
            BATTERY_CHARGING_PIN
        );
#endif

        // 初始化电源管理定时器，20 秒后进入节能模式
        power_save_timer_ = std::make_unique<PowerSaveTimer>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, 20, -1);
        power_save_timer_->SetEnabled(true);
        ESP_LOGI(TAG, "Power save timer initialized and enabled");

        // 注册进入和退出睡眠模式的回调
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Entering sleep mode");
            auto& board = Board::GetInstance();

            // 保存当前亮度并在进入睡眠时将亮度设为 0
            auto backlight = board.GetBacklight();
            if (backlight) {
                saved_brightness_ = backlight->brightness();
                backlight->SetBrightness(0);
                ESP_LOGI(TAG, "Brightness saved (%d) and set to 0", saved_brightness_);
            }

            // 使用 Application 的 Schedule 方法确保在主线程中执行 UI 更新
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
        });

        power_save_timer_->OnExitSleepMode([this]() {
            ESP_LOGI(TAG, "Exiting sleep mode");
            auto& board = Board::GetInstance();
            
            // 重置睡眠倒计时为 20 秒，避免默认状态下的倒计时长度受到 MCP 影响
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
            
            // 使用 Application 的 Schedule 方法确保在主线程中执行 UI 更新
            Application::GetInstance().Schedule([&board]() {
                auto& board = Board::GetInstance();
                auto display = board.GetDisplay();
                auto codec = board.GetAudioCodec();

                ESP_LOGI(TAG, "Updating exit sleep mode UI");
                if (display) {
                    ESP_LOGI(TAG, "Calling SetEmotion with 'neutral'");
                    display->SetEmotion("neutral");
                    ESP_LOGI(TAG, "Called SetEmotion with 'neutral'");
                    ESP_LOGI(TAG, "Exit sleep mode UI updated");
                } else {
                    ESP_LOGW(TAG, "Display is null");
                }

                // 重新启用 I2S 输出
                if (codec && codec->output_enabled() == false) {
                    ESP_LOGI(TAG, "Re-enabling I2S output after sleep");
                    codec->EnableOutput(true);
                }
            });
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

        // 初始化GPIO17为输出并拉高（电池充电检测或其他用途）
        {
            gpio_config_t gpio17_cfg = {};
            gpio17_cfg.pin_bit_mask = BIT64(GPIO_NUM_17);
            gpio17_cfg.mode = GPIO_MODE_OUTPUT;
            gpio17_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio17_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            gpio17_cfg.intr_type = GPIO_INTR_DISABLE;
            ESP_ERROR_CHECK(gpio_config(&gpio17_cfg));
            
            // 将GPIO17设置为高电平
            ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_17, 1));
        }

        // // 初始化GPIO21为输出并拉高
        // {
        //     gpio_config_t gpio21_cfg = {};
        //     gpio21_cfg.pin_bit_mask = BIT64(GPIO_NUM_21);
        //     gpio21_cfg.mode = GPIO_MODE_OUTPUT;
        //     gpio21_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        //     gpio21_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        //     gpio21_cfg.intr_type = GPIO_INTR_DISABLE;
        //     ESP_ERROR_CHECK(gpio_config(&gpio21_cfg));
            
        //     // 将GPIO21设置为高电平
        //     ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_21, 1));
        // }

        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->SetBrightness(50, true); 
        }
        
        pwm_servo_ = &pwm_servo::GetInstance(); // 初始化舵机

        // 启动触摸监控
        // Start_touch_monitor();

        // 启动 BOOT 按键电平监控（诊断用）
        Start_boot_button_monitor();
    }

    virtual AudioCodec* GetAudioCodec() override {
        // 混合模式：USB 麦克风输入 + I2S 扬声器输出
        // 输入采样率将在运行时自动检测，无需预先指定
        static HybridUsbI2sCodec audio_codec(
            AUDIO_OUTPUT_SAMPLE_RATE,  // I2S 扬声器输出采样率 (24kHz)
            AUDIO_I2S_SPK_GPIO_BCLK,  // I2S 扬声器 BCLK 引脚
            AUDIO_I2S_SPK_GPIO_LRCK,  // I2S 扬声器 LRCK 引脚  
            AUDIO_I2S_SPK_GPIO_DOUT   // I2S 扬声器 DOUT 引脚
        );
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

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
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
        level = 30;
        charging = true;
        discharging = false;  
#endif
        return true;
    }
};

// 注册开发板
DECLARE_BOARD(CompactWifiBoardLCDUAC);
