#include "button.h"

#include <button_gpio.h>

#include <esp_log.h>
#include "driver/touch_pad.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "Button"

#if CONFIG_SOC_ADC_SUPPORTED
AdcButton::AdcButton(const button_adc_config_t& adc_config) : Button(nullptr) {
    button_config_t btn_config = {
        .long_press_time = 2000,
        .short_press_time = 0,
    };
    ESP_ERROR_CHECK(iot_button_new_adc_device(&btn_config, &adc_config, &button_handle_));
}
#endif

Button::Button(button_handle_t button_handle) : button_handle_(button_handle) {
}

Button::Button(gpio_num_t gpio_num, bool active_high, uint16_t long_press_time, uint16_t short_press_time, bool enable_power_save) : gpio_num_(gpio_num) {
    if (gpio_num == GPIO_NUM_NC) {
        return;
    }
    button_config_t button_config = {
        .long_press_time = long_press_time,
        .short_press_time = short_press_time
    };
    button_gpio_config_t gpio_config = {
        .gpio_num = gpio_num,
        .active_level = static_cast<uint8_t>(active_high ? 1 : 0),
        .enable_power_save = enable_power_save,
        .disable_pull = false
    };
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&button_config, &gpio_config, &button_handle_));
}

// 实现触摸按钮功能
TouchButton::TouchButton(gpio_num_t gpio_num, uint16_t threshold, uint16_t long_press_time, uint16_t short_press_time) : Button(nullptr), on_touch_(nullptr) {
    // 先设置gpio_num_，因为它是protected成员
    gpio_num_ = gpio_num;

    // 初始化触摸传感器（如果尚未初始化）
    esp_err_t err = touch_pad_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch pad: %s", esp_err_to_name(err));
        return;
    }

        // 3. 校准基线（首次上电必须校准）
    touch_pad_reset_benchmark((touch_pad_t)gpio_num);
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待基线稳定
    uint32_t baseline_val;
    touch_pad_read_benchmark((touch_pad_t)gpio_num, &baseline_val);

    // 配置触摸传感器参数
    err = touch_pad_config((touch_pad_t)gpio_num);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure touch pad: %s", esp_err_to_name(err));
        return;
    }
    
    // 设置阈值
    err = touch_pad_set_thresh((touch_pad_t)gpio_num, threshold);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set touch pad threshold: %s", esp_err_to_name(err));
        return;
    }
    
    // 设置充电放电时间，增加灵敏度
    err = touch_pad_set_charge_discharge_times(255); // 只需要一个参数
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set charge discharge times: %s", esp_err_to_name(err));
        return;
    }

    // 设置 FSM 模式为定时器模式
    err = touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set FSM mode: %s", esp_err_to_name(err));
        return;
    }

    // 启用触摸传感器中断 - 使用正确的枚举类型
    err = touch_pad_intr_enable(TOUCH_PAD_INTR_MASK_ACTIVE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable touch pad interrupt: %s", esp_err_to_name(err));
        return;
    }
    
    // 注册触摸传感器中断处理程序
    err = touch_pad_isr_register(TouchButton::touch_isr_handler, this, TOUCH_PAD_INTR_MASK_ACTIVE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register touch pad ISR: %s", esp_err_to_name(err));
        return;
    }

    // 启动触摸传感器FSM
    err = touch_pad_fsm_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start FSM: %s", esp_err_to_name(err));
        return;
    }

    // 等待触摸传感器稳定
    vTaskDelay(100 / portTICK_PERIOD_MS); // 延迟100ms等待稳定
}


// 添加静态中断处理函数
void TouchButton::touch_isr_handler(void* arg) {
    TouchButton* instance = static_cast<TouchButton*>(arg);
    // ESP_LOGI(TAG, "Touch pad pressed 1");
    
    // 读取中断状态
    uint32_t intr_mask = touch_pad_read_intr_status_mask();
    
    // 检查哪个触摸板被触发
    for (int i = 0; i < TOUCH_PAD_MAX; i++) {
        if (((intr_mask >> i) & 1)) {
            // ESP_LOGI(TAG, "Touch pad pressed 3"); 
            // 确认是当前触摸引脚触发的中断
            if (static_cast<int>(instance->gpio_num_) == i) {
                // ESP_LOGI(TAG, "Touch pad pressed ");
                // 处理触摸事件 - 分别调用touch和press down回调
                if (instance->on_touch_) {
                    instance->on_touch_();
                }
                
                // if (instance->on_press_down_) {
                //     instance->on_press_down_();
                //     // ESP_LOGI(TAG, "Touch pad pressed");
                // }
            }
        }
    }
    
    // 清除中断
    touch_pad_intr_clear(TOUCH_PAD_INTR_MASK_ACTIVE);
}

// ... existing code ...

// 添加触摸回调注册方法

void TouchButton::OnTouch(std::function<void()> callback) {
    ESP_LOGI(TAG, "Touch pad pressed 2");
    on_touch_ = callback;
    
}

// 添加读取原始值的方法实现
uint32_t TouchButton::read_raw_value() {
    uint32_t raw_value = 0;
    esp_err_t ret = touch_pad_read_raw_data((touch_pad_t)gpio_num_, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read touch pad raw data: %s", esp_err_to_name(ret));
        return 0;
    }
    return raw_value;
}

// 读取稳定的原始值
uint32_t TouchButton::read_stable_raw_value(int attempts) {
    uint32_t raw_value = 0;
    for (int i = 0; i < attempts; i++) {
        esp_err_t ret = touch_pad_read_raw_data((touch_pad_t)gpio_num_, &raw_value);
        if (ret == ESP_OK && raw_value > 0) {  // 确保返回值有效
            return raw_value;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);  // 等待10ms后重试
    }
    return raw_value;  // 返回最后一次读取值
}

Button::~Button() {
    if (button_handle_ != NULL) {
        iot_button_delete(button_handle_);
    }
}

void Button::OnPressDown(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        // 对于没有button_handle_的情况（如TouchButton），直接存储回调
        // on_press_down_ = callback;
        return;
    }
    on_press_down_ = callback;// 存储回调函数
    iot_button_register_cb(button_handle_, BUTTON_PRESS_DOWN, nullptr, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);// 从用户数据获取Button实例
        if (button->on_press_down_) {// 检查是否有回调函数
            button->on_press_down_(); // 执行回调
        }
    }, this); // 传递this指针作为用户数据
}

void Button::OnPressUp(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        // 对于没有button_handle_的情况（如TouchButton），直接存储回调
        on_press_up_ = callback;
        return;
    }
    on_press_up_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, nullptr, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_press_up_) {
            button->on_press_up_();
        }
    }, this);
}

void Button::OnLongPress(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        // 对于没有button_handle_的情况（如TouchButton），直接存储回调
        on_long_press_ = callback;
        return;
    }
    on_long_press_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, nullptr, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_long_press_) {
            button->on_long_press_();
        }
    }, this);
}

void Button::OnClick(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        // 对于没有button_handle_的情况（如TouchButton），直接存储回调
        on_click_ = callback;
        return;
    }
    on_click_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, nullptr, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_click_) {
            button->on_click_();
        }
    }, this);
}

void Button::OnDoubleClick(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        // 对于没有button_handle_的情况（如TouchButton），直接存储回调
        on_double_click_ = callback;
        return;
    }
    on_double_click_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_DOUBLE_CLICK, nullptr, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_double_click_) {
            button->on_double_click_();
        }
    }, this);
}

void Button::OnMultipleClick(std::function<void()> callback, uint8_t click_count) {
    if (button_handle_ == nullptr) {
        // 对于没有button_handle_的情况（如TouchButton），直接存储回调
        on_multiple_click_ = callback;
        return;
    }
    on_multiple_click_ = callback;
    button_event_args_t event_args = {
        .multiple_clicks = {
            .clicks = click_count
        }
    };
    iot_button_register_cb(button_handle_, BUTTON_MULTIPLE_CLICK, &event_args, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_multiple_click_) {
            button->on_multiple_click_();
        }
    }, this);
}