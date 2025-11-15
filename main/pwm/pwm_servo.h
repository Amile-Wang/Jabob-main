#ifndef PWM_SERVO_H
#define PWM_SERVO_H 

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#define PWM_GPIO_PIN      GPIO_NUM_18      // PWM输出引脚
#define PWM_FREQUENCY     50               // 频率50Hz（保持不变）
#define PWM_RESOLUTION    10               // 10位分辨率(0-1023) 适配50Hz
#define PWM_FIXED_DUTY    512              // 固定50%占空比(1023*50%=511.5)
#define DUTY_MIN          26               // 2.5%占空比对应值 (1023*0.025=25.575)
#define DUTY_MAX          100              // 12.5%占空比对应值 (1023*0.125=127.875)
#define DUTY_STEP         1                // 占空比步进值

class pwm_servo{
public:
    static pwm_servo& GetInstance() {
        static pwm_servo instance;
        return instance;
    }

    void Initialize();
    void SetDutyCycle(uint32_t duty);
    void emoact(const char* emotion);

private:
    pwm_servo() = default;
    ~pwm_servo() = default;

    uint32_t current_duty_ = 0;
    bool initialized_ = false;
};


#endif