#include "pwm_servo.h"
#include "driver/ledc.h"
#include <vector>
#include <cstring>
#include <esp_log.h>

// // 硬件配置参数
// #define PWM_GPIO_PIN      GPIO_NUM_18      // PWM输出引脚
// #define PWM_FREQUENCY     50               // 频率50Hz（保持不变）
// #define PWM_RESOLUTION    10               // 10位分辨率(0-1023) 适配50Hz
// #define PWM_FIXED_DUTY    512              // 固定50%占空比(1023*50%=511.5)
// #define DUTY_MIN          26               // 2.5%占空比对应值 (1023*0.025=25.575)
// #define DUTY_MAX          100              // 12.5%占空比对应值 (1023*0.125=127.875)
// #define DUTY_STEP         1                // 占空比步进值

// 硬件PWM初始化
void pwm_servo::Initialize()
{
    if (initialized_) {
        ESP_LOGW("PWM_SERVO", "pwm_servo already initialized, skipping");
        return;
    }

    // 配置LED PWM定时器 - 使用高速模式以避免与低速LEDC冲突
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,  // 改用高速模式
        .duty_resolution = LEDC_TIMER_10_BIT, // 10位分辨率
        .timer_num = LEDC_TIMER_2,  // 使用LEDC_TIMER_2
        .freq_hz = PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    esp_err_t timer_err = ledc_timer_config(&ledc_timer);
    if (timer_err != ESP_OK) {
        ESP_LOGE("PWM_SERVO", "Failed to configure LEDC timer: %s", esp_err_to_name(timer_err));
        return;
    }

    // 配置LED PWM通道
    ledc_channel_config_t ledc_channel = {
        .gpio_num = PWM_GPIO_PIN, 
        .speed_mode = LEDC_LOW_SPEED_MODE,  
        .channel = LEDC_CHANNEL_3,  
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_3,
        .duty = 0,
        .hpoint = 0,
    };
    esp_err_t channel_err = ledc_channel_config(&ledc_channel);
    if (channel_err != ESP_OK) {
        ESP_LOGE("PWM_SERVO", "Failed to configure LEDC channel: %s", esp_err_to_name(channel_err));
        return;
    }

    initialized_ = true;
    ESP_LOGI("PWM_SERVO", "PWM servo initialized successfully on high-speed mode");
}



// 设置指定占空比
void pwm_servo::SetDutyCycle(uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3));
    printf("PWM duty set to %0.1f%% (value: %lu)\n", (duty * 100.0) / 1023, duty);
}

void pwm_servo::emoact(const char* emotion)
{

    struct Emoact {
        uint32_t emoduty;
        const char* text;
    };    

    static const std::vector<Emoact> Emoacts = {
        {26, "neutral"},
        {52, "happy"},
        {78, "laughing"},
        {26, "funny"},
        {26, "sad"},
        {100, "angry"},
        {26, "crying"},
        {26, "loving"},
        {26, "embarrassed"},
        {26, "surprised"},
        {26, "shocked"},
        {26, "thinking"},
        {26, "winking"},
        {26, "cool"},
        {26, "relaxed"},
        {26, "delicious"},
        {26, "kissy"},
        {26, "confident"},
        {26, "sleepy"},
        {26, "silly"},
        {26, "confused"}
    };

    // 查找匹配的表情
    for (const auto& emo : Emoacts) {
        if (strcmp(emo.text, emotion) == 0) {
            SetDutyCycle(emo.emoduty);
            break;
        }
    }



    printf("PWM Servo Control Finished\n");

    

    // 循环改变占空比
}


// void motor_main(void)
// {
//     printf("Starting PWM Motor Control with duty cycle cycling\n");

//     // 初始化硬件PWM
//     pwm_init();

//     uint32_t duty_cycle = DUTY_MIN;  // 初始化占空比
//     int8_t direction = 2.5;            // 1:增加, -1:减少

//     // 循环改变占空比
//     while (1) {
//         // 设置当前占空比
//         pwm_set_duty(duty_cycle);

//         // 更新占空比值
//         duty_cycle += DUTY_STEP * direction;

//         // 边界处理
//         if (duty_cycle > DUTY_MAX || duty_cycle < DUTY_MIN) {
//             direction *= -1;  // 反向
//             duty_cycle += DUTY_STEP * direction;  // 回退一步
//         }

//         // 延时
//         vTaskDelay(pdMS_TO_TICKS(100));
//     }
// }