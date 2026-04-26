#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>


#include "application.h"
#include "system_info.h"

#define TAG "main"

// 自定义日志输出函数，添加时间戳
static int custom_vprintf(const char *fmt, va_list args) {
    // 获取时间戳（微秒）
    int64_t time_us = esp_timer_get_time();
    int64_t time_ms = time_us / 1000;
    int64_t seconds = time_ms / 1000;
    int64_t minutes = seconds / 60;
    int64_t hours = minutes / 60;
    
    // 格式化时间显示为 HH:MM:SS.mmm
    uint32_t ms = time_ms % 1000;
    uint8_t sec = seconds % 60;
    uint8_t min = minutes % 60;
    uint8_t hour = hours % 24;
    
    // 打印时间戳和原始日志内容
    printf("[%02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 ".%03" PRIu32 "] ", hour, min, sec, ms);
    return vprintf(fmt, args);
}
extern "C" void app_main(void)
{
    // 设置ML307相关组件的日志级别为INFO，用于调试HTTPS SSL问题
    esp_log_level_set("AtUart", ESP_LOG_INFO);
    esp_log_level_set("Ml307Ssl", ESP_LOG_INFO);
    esp_log_level_set("HttpClient", ESP_LOG_INFO);
    
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Launch the application
    Application::GetInstance().Start();
}
