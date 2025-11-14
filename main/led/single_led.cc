#include "single_led.h"
#include "application.h"
#include <esp_log.h> 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define TAG "SingleLed"

#define DEFAULT_BRIGHTNESS 4
#define HIGH_BRIGHTNESS 16
#define LOW_BRIGHTNESS 2

#define BLINK_INFINITE -1


// 定义LED命令队列项
typedef enum {
    LED_CMD_SET_PIXEL,
    LED_CMD_CLEAR,
    LED_CMD_STOP_TIMER
} led_cmd_type_t;

typedef struct {
    led_cmd_type_t type;
    uint8_t r, g, b;
} led_cmd_t;
SingleLed::SingleLed(gpio_num_t gpio) {
    // If the gpio is not connected, you should use NoLed class
    assert(gpio != GPIO_NUM_NC);

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = 1;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    led_strip_clear(led_strip_);

        // 创建命令队列
    cmd_queue_ = xQueueCreate(10, sizeof(led_cmd_t));

    // 创建LED处理任务
    xTaskCreate([](void* arg) {
        SingleLed* led = static_cast<SingleLed*>(arg);
        led->LedTask();
    }, "led_task", 2048, this, 5, nullptr);

    esp_timer_create_args_t blink_timer_args = {
        .callback = [](void *arg) {
            auto led = static_cast<SingleLed*>(arg);
            led->OnBlinkTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "blink_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&blink_timer_args, &blink_timer_));
}

// SingleLed::~SingleLed() {
//     esp_timer_stop(blink_timer_);
//     if (led_strip_ != nullptr) {
//         led_strip_del(led_strip_);
//     }
// }
SingleLed::~SingleLed() {
    esp_timer_stop(blink_timer_);
    if (led_strip_ != nullptr) {
        led_strip_del(led_strip_);
    }
    
    // 发送停止命令并删除队列
    if (cmd_queue_ != nullptr) {
        led_cmd_t cmd = { LED_CMD_STOP_TIMER, 0, 0, 0 };
        xQueueSend(cmd_queue_, &cmd, 0);
        vQueueDelete(cmd_queue_);
    }
}


void SingleLed::SetColor(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r;
    g_ = g;
    b_ = b;
}

void SingleLed::TurnOn() {
    if (led_strip_ == nullptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);

    // led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
    // led_strip_refresh(led_strip_);
    led_cmd_t cmd = { LED_CMD_SET_PIXEL, r_, g_, b_ };
    xQueueSend(cmd_queue_, &cmd, 0);
}

void SingleLed::TurnOff() {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    // led_strip_clear(led_strip_);

    // 发送清除命令到队列
    led_cmd_t cmd = { LED_CMD_CLEAR, 0, 0, 0 };
    xQueueSend(cmd_queue_, &cmd, 0);
}

void SingleLed::BlinkOnce() {
    Blink(1, 100);
}

void SingleLed::Blink(int times, int interval_ms) {
    StartBlinkTask(times, interval_ms);
}

void SingleLed::StartContinuousBlink(int interval_ms) {
    StartBlinkTask(BLINK_INFINITE, interval_ms);
}

void SingleLed::StartBlinkTask(int times, int interval_ms) {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    
    blink_counter_ = times * 2;
    blink_interval_ms_ = interval_ms;
    esp_timer_start_periodic(blink_timer_, interval_ms * 1000);
}

void SingleLed::OnBlinkTimer() {
    std::lock_guard<std::mutex> lock(mutex_);
    blink_counter_--;
    // if (blink_counter_ & 1) {
    //     led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
    //     led_strip_refresh(led_strip_);
    // } else {
    //     led_strip_clear(led_strip_);

    //     if (blink_counter_ == 0) {
    //         esp_timer_stop(blink_timer_);
    //     }
    // }
    if (blink_counter_ & 1) {
        // 发送设置像素命令到队列
        led_cmd_t cmd = { LED_CMD_SET_PIXEL, r_, g_, b_ };
        xQueueSend(cmd_queue_, &cmd, 0);
    } else {
        // 发送清除命令到队列
        led_cmd_t cmd = { LED_CMD_CLEAR, 0, 0, 0 };
        xQueueSend(cmd_queue_, &cmd, 0);

        if (blink_counter_ == 0) {
            esp_timer_stop(blink_timer_);
        }
    }
}

void SingleLed::LedTask() {
    led_cmd_t cmd;
    bool need_refresh = false;

    while (true) {
        // if (xQueueReceive(cmd_queue_, &cmd, portMAX_DELAY)) {
        if (xQueueReceive(cmd_queue_, &cmd, pdMS_TO_TICKS(50))) {
            if (cmd.type == LED_CMD_STOP_TIMER) {
                break; // 退出任务
            }
            
            switch (cmd.type) {
                case LED_CMD_SET_PIXEL:
                    led_strip_set_pixel(led_strip_, 0, cmd.r, cmd.g, cmd.b);
                    // led_strip_refresh(led_strip_);
                    need_refresh = true;

                    break;
                case LED_CMD_CLEAR:
                    led_strip_clear(led_strip_);
                    need_refresh = true;
                    break;
                default:
                    break;
            }
        }
        // 定期刷新LED，避免在处理命令时刷新
        if (need_refresh) {
            led_strip_refresh(led_strip_);
            need_refresh = false;
        }
    }
    vTaskDelete(nullptr);
}

void SingleLed::OnStateChanged() {
    auto& app = Application::GetInstance();
    auto device_state = app.GetDeviceState();
    switch (device_state) {
        case kDeviceStateStarting:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            StartContinuousBlink(100);
            break;
        case kDeviceStateWifiConfiguring:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            StartContinuousBlink(500);
            break;
        case kDeviceStateIdle:
            TurnOff();
            break;
        case kDeviceStateConnecting:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            TurnOn();
            break;
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            if (app.IsVoiceDetected()) {
                SetColor(HIGH_BRIGHTNESS, 0, 0);
            } else {
                SetColor(LOW_BRIGHTNESS, 0, 0);
            }
            TurnOn();
            break;
        case kDeviceStateSpeaking:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            TurnOn();
            break;
        case kDeviceStateUpgrading:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            StartContinuousBlink(100);
            break;
        case kDeviceStateActivating:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            StartContinuousBlink(500);
            break;
        default:
            ESP_LOGW(TAG, "Unknown led strip event: %d", device_state);
            return;
    }
}
