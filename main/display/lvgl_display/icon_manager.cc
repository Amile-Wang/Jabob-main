#include "icon_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <cstring>

// 确保包含LODEPNG库
// #include "lodepng.h"
// #include "src/libs/lodepng/lv_lodepng.h"

// 嵌入的PNG图像数据
extern const uint8_t battery_png_start[] asm("_binary_battery_png_start");
extern const uint8_t battery_png_end[]   asm("_binary_battery_png_end");
extern const uint8_t battery_charging_png_start[] asm("_binary_battery_charging_png_start");
extern const uint8_t battery_charging_png_end[]   asm("_binary_battery_charging_png_end");
extern const uint8_t battery_full_png_start[] asm("_binary_battery_full_png_start");
extern const uint8_t battery_full_png_end[]   asm("_binary_battery_full_png_end");
extern const uint8_t battery_high_png_start[] asm("_binary_battery_high_png_start");
extern const uint8_t battery_high_png_end[]   asm("_binary_battery_high_png_end");
extern const uint8_t battery_low_png_start[] asm("_binary_battery_low_png_start");
extern const uint8_t battery_low_png_end[]   asm("_binary_battery_low_png_end");
extern const uint8_t battery_low_warning_png_start[] asm("_binary_battery_low_warning_png_start");
extern const uint8_t battery_low_warning_png_end[]   asm("_binary_battery_low_warning_png_end");
extern const uint8_t battery_mid_png_start[] asm("_binary_battery_mid_png_start");
extern const uint8_t battery_mid_png_end[]   asm("_binary_battery_mid_png_end");
extern const uint8_t battery_warning_png_start[] asm("_binary_battery_warning_png_start");
extern const uint8_t battery_warning_png_end[]   asm("_binary_battery_warning_png_end");
extern const uint8_t bluetooth_0_png_start[] asm("_binary_bluetooth_0_png_start");
extern const uint8_t bluetooth_0_png_end[]   asm("_binary_bluetooth_0_png_end");
extern const uint8_t bluetooth_1_png_start[] asm("_binary_bluetooth_1_png_start");
extern const uint8_t bluetooth_1_png_end[]   asm("_binary_bluetooth_1_png_end");
extern const uint8_t bluetooth_2_png_start[] asm("_binary_bluetooth_2_png_start");
extern const uint8_t bluetooth_2_png_end[]   asm("_binary_bluetooth_2_png_end");
extern const uint8_t bluetooth_L_png_start[] asm("_binary_bluetooth_L_png_start");
extern const uint8_t bluetooth_L_png_end[]   asm("_binary_bluetooth_L_png_end");
extern const uint8_t bluetooth_L_dark_png_start[] asm("_binary_bluetooth_L_dark_png_start");
extern const uint8_t bluetooth_L_dark_png_end[]   asm("_binary_bluetooth_L_dark_png_end");
extern const uint8_t bluetooth_S_png_start[] asm("_binary_bluetooth_S_png_start");
extern const uint8_t bluetooth_S_png_end[]   asm("_binary_bluetooth_S_png_end");
extern const uint8_t bluetooth_blue_png_start[] asm("_binary_bluetooth_blue_png_start");
extern const uint8_t bluetooth_blue_png_end[]   asm("_binary_bluetooth_blue_png_end");
extern const uint8_t bluetooth_blue2_png_start[] asm("_binary_bluetooth_blue2_png_start");
extern const uint8_t bluetooth_blue2_png_end[]   asm("_binary_bluetooth_blue2_png_end");
extern const uint8_t bluetooth_dark_png_start[] asm("_binary_bluetooth_dark_png_start");
extern const uint8_t bluetooth_dark_png_end[]   asm("_binary_bluetooth_dark_png_end");
extern const uint8_t bluetooth_off_png_start[] asm("_binary_bluetooth_off_png_start");
extern const uint8_t bluetooth_off_png_end[]   asm("_binary_bluetooth_off_png_end");
extern const uint8_t bluetooth_white_png_start[] asm("_binary_bluetooth_white_png_start");
extern const uint8_t bluetooth_white_png_end[]   asm("_binary_bluetooth_white_png_end");
extern const uint8_t calling_png_start[] asm("_binary_calling_png_start");
extern const uint8_t calling_png_end[]   asm("_binary_calling_png_end");
extern const uint8_t calling_green_png_start[] asm("_binary_calling_green_png_start");
extern const uint8_t calling_green_png_end[]   asm("_binary_calling_green_png_end");
extern const uint8_t careright_png_start[] asm("_binary_careright_png_start");
extern const uint8_t careright_png_end[]   asm("_binary_careright_png_end");
extern const uint8_t caretleft_png_start[] asm("_binary_caretleft_png_start");
extern const uint8_t caretleft_png_end[]   asm("_binary_caretleft_png_end");
extern const uint8_t checkcircle_png_start[] asm("_binary_checkcircle_png_start");
extern const uint8_t checkcircle_png_end[]   asm("_binary_checkcircle_png_end");
extern const uint8_t connected_png_start[] asm("_binary_connected_png_start");
extern const uint8_t connected_png_end[]   asm("_binary_connected_png_end");
extern const uint8_t cursor_png_start[] asm("_binary_cursor_png_start");
extern const uint8_t cursor_png_end[]   asm("_binary_cursor_png_end");
extern const uint8_t disconnected_png_start[] asm("_binary_disconnected_png_start");
extern const uint8_t disconnected_png_end[]   asm("_binary_disconnected_png_end");
extern const uint8_t dot_green_png_start[] asm("_binary_dot_green_png_start");
extern const uint8_t dot_green_png_end[]   asm("_binary_dot_green_png_end");
extern const uint8_t gray_png_start[] asm("_binary_gray_png_start");
extern const uint8_t gray_png_end[]   asm("_binary_gray_png_end");
extern const uint8_t lock_png_start[] asm("_binary_lock_png_start");
extern const uint8_t lock_png_end[]   asm("_binary_lock_png_end");
extern const uint8_t menu_png_start[] asm("_binary_menu_png_start");
extern const uint8_t menu_png_end[]   asm("_binary_menu_png_end");
extern const uint8_t menu_button_png_start[] asm("_binary_menu_button_png_start");
extern const uint8_t menu_button_png_end[]   asm("_binary_menu_button_png_end");
extern const uint8_t mic_png_start[] asm("_binary_mic_png_start");
extern const uint8_t mic_png_end[]   asm("_binary_mic_png_end");
extern const uint8_t mic_off_png_start[] asm("_binary_mic_off_png_start");
extern const uint8_t mic_off_png_end[]   asm("_binary_mic_off_png_end");
extern const uint8_t mic_on_png_start[] asm("_binary_mic_on_png_start");
extern const uint8_t mic_on_png_end[]   asm("_binary_mic_on_png_end");
extern const uint8_t phone_png_start[] asm("_binary_phone_png_start");
extern const uint8_t phone_png_end[]   asm("_binary_phone_png_end");
extern const uint8_t phonedisconnect_png_start[] asm("_binary_phonedisconnect_png_start");
extern const uint8_t phonedisconnect_png_end[]   asm("_binary_phonedisconnect_png_end");
extern const uint8_t phonedisconnect_dark_png_start[] asm("_binary_phonedisconnect_dark_png_start");
extern const uint8_t phonedisconnect_dark_png_end[]   asm("_binary_phonedisconnect_dark_png_end");
extern const uint8_t prohibit_png_start[] asm("_binary_prohibit_png_start");
extern const uint8_t prohibit_png_end[]   asm("_binary_prohibit_png_end");
extern const uint8_t select_png_start[] asm("_binary_select_png_start");
extern const uint8_t select_png_end[]   asm("_binary_select_png_end");
extern const uint8_t status_calling_png_start[] asm("_binary_status_calling_png_start");
extern const uint8_t status_calling_png_end[]   asm("_binary_status_calling_png_end");
extern const uint8_t status_idle_png_start[] asm("_binary_status_idle_png_start");
extern const uint8_t status_idle_png_end[]   asm("_binary_status_idle_png_end");
extern const uint8_t status_meeting_png_start[] asm("_binary_status_meeting_png_start");
extern const uint8_t status_meeting_png_end[]   asm("_binary_status_meeting_png_end");
extern const uint8_t status_music_png_start[] asm("_binary_status_music_png_start");
extern const uint8_t status_music_png_end[]   asm("_binary_status_music_png_end");
extern const uint8_t volume_high_png_start[] asm("_binary_volume_high_png_start");
extern const uint8_t volume_high_png_end[]   asm("_binary_volume_high_png_end");
extern const uint8_t volume_low_png_start[] asm("_binary_volume_low_png_start");
extern const uint8_t volume_low_png_end[]   asm("_binary_volume_low_png_end");
extern const uint8_t volume_mid_png_start[] asm("_binary_volume_mid_png_start");
extern const uint8_t volume_mid_png_end[]   asm("_binary_volume_mid_png_end");
extern const uint8_t volume_minus_png_start[] asm("_binary_volume_minus_png_start");
extern const uint8_t volume_minus_png_end[]   asm("_binary_volume_minus_png_end");
extern const uint8_t volume_mute_png_start[] asm("_binary_volume_mute_png_start");
extern const uint8_t volume_mute_png_end[]   asm("_binary_volume_mute_png_end");
extern const uint8_t volume_plus_png_start[] asm("_binary_volume_plus_png_start");
extern const uint8_t volume_plus_png_end[]   asm("_binary_volume_plus_png_end");
extern const uint8_t _1111_png_start[] asm("_binary_1111_png_start");
extern const uint8_t _1111_png_end[]   asm("_binary_1111_png_end");
extern const uint8_t anc_off_png_start[] asm("_binary_anc_off_png_start");
extern const uint8_t anc_off_png_end[]   asm("_binary_anc_off_png_end");
extern const uint8_t anc_on_png_start[] asm("_binary_anc_on_png_start");
extern const uint8_t anc_on_png_end[]   asm("_binary_anc_on_png_end");
extern const uint8_t settings_png_start[] asm("_binary_settings_png_start");
extern const uint8_t settings_png_end[]   asm("_binary_settings_png_end");


static const char *TAG = "icon_manager";

// 图标信息结构体
typedef struct {
    const uint8_t *data_start;
    const uint8_t *data_end;
    lv_image_dsc_t *img_dsc;  // 存储解码后的图像描述符
    bool loaded;
} icon_info_t;

// 图标信息数组
static icon_info_t icons[ICON_MAX] = {
    {battery_png_start, battery_png_end, NULL, false},                           // ICON_BATTERY
    {battery_charging_png_start, battery_charging_png_end, NULL, false},         // ICON_BATTERY_CHARGING
    {battery_full_png_start, battery_full_png_end, NULL, false},                 // ICON_BATTERY_FULL
    {battery_high_png_start, battery_high_png_end, NULL, false},                 // ICON_BATTERY_HIGH
    {battery_low_png_start, battery_low_png_end, NULL, false},                   // ICON_BATTERY_LOW
    {battery_low_warning_png_start, battery_low_warning_png_end, NULL, false},   // ICON_BATTERY_LOW_WARNING
    {battery_mid_png_start, battery_mid_png_end, NULL, false},                   // ICON_BATTERY_MID
    {battery_warning_png_start, battery_warning_png_end, NULL, false},           // ICON_BATTERY_WARNING
    {bluetooth_0_png_start, bluetooth_0_png_end, NULL, false},                   // ICON_BLUETOOTH_0
    {bluetooth_1_png_start, bluetooth_1_png_end, NULL, false},                   // ICON_BLUETOOTH_1
    {bluetooth_L_png_start, bluetooth_L_png_end, NULL, false},                   // ICON_BLUETOOTH_L
    {bluetooth_L_dark_png_start, bluetooth_L_dark_png_end, NULL, false},         // ICON_BLUETOOTH_L_DARK
    {bluetooth_S_png_start, bluetooth_S_png_end, NULL, false},                   // ICON_BLUETOOTH_S
    {bluetooth_blue_png_start, bluetooth_blue_png_end, NULL, false},             // ICON_BLUETOOTH_BLUE
    {bluetooth_blue2_png_start, bluetooth_blue2_png_end, NULL, false},           // ICON_BLUETOOTH_BLUE2
    {bluetooth_dark_png_start, bluetooth_dark_png_end, NULL, false},             // ICON_BLUETOOTH_DARK
    {bluetooth_off_png_start, bluetooth_off_png_end, NULL, false},               // ICON_BLUETOOTH_OFF
    {bluetooth_white_png_start, bluetooth_white_png_end, NULL, false},           // ICON_BLUETOOTH_WHITE
    {calling_png_start, calling_png_end, NULL, false},                           // ICON_CALLING
    {calling_green_png_start, calling_green_png_end, NULL, false},               // ICON_CALLING_GREEN
    {careright_png_start, careright_png_end, NULL, false},                       // ICON_CARRIGHT
    {caretleft_png_start, caretleft_png_end, NULL, false},                       // ICON_CARETLEFT
    {checkcircle_png_start, checkcircle_png_end, NULL, false},                   // ICON_CHECKCIRCLE
    {connected_png_start, connected_png_end, NULL, false},                       // ICON_CONNECTED
    {cursor_png_start, cursor_png_end, NULL, false},                             // ICON_CURSOR
    {disconnected_png_start, disconnected_png_end, NULL, false},                 // ICON_DISCONNECTED
    {dot_green_png_start, dot_green_png_end, NULL, false},                       // ICON_DOT_GREEN
    {gray_png_start, gray_png_end, NULL, false},                                 // ICON_GRAY
    {lock_png_start, lock_png_end, NULL, false},                                 // ICON_LOCK
    {menu_png_start, menu_png_end, NULL, false},                                 // ICON_MENU
    {menu_button_png_start, menu_button_png_end, NULL, false},                   // ICON_MENU_BUTTON
    {mic_png_start, mic_png_end, NULL, false},                                   // ICON_MIC
    {mic_off_png_start, mic_off_png_end, NULL, false},                           // ICON_MIC_OFF
    {mic_on_png_start, mic_on_png_end, NULL, false},                             // ICON_MIC_ON
    {phone_png_start, phone_png_end, NULL, false},                               // ICON_PHONE
    {phonedisconnect_png_start, phonedisconnect_png_end, NULL, false},           // ICON_PHONEDISCONNECT
    {phonedisconnect_dark_png_start, phonedisconnect_dark_png_end, NULL, false}, // ICON_PHONE_DISCONNECT_DARK
    {prohibit_png_start, prohibit_png_end, NULL, false},                         // ICON_PROHIBIT
    {select_png_start, select_png_end, NULL, false},                             // ICON_SELECT
    {status_calling_png_start, status_calling_png_end, NULL, false},             // ICON_STATUS_CALLING
    {status_idle_png_start, status_idle_png_end, NULL, false},                   // ICON_STATUS_IDLE
    {status_meeting_png_start, status_meeting_png_end, NULL, false},              // ICON_STATUS_MEETING
    {status_music_png_start, status_music_png_end, NULL, false},                 // ICON_STATUS_MUSIC
    {volume_high_png_start, volume_high_png_end, NULL, false},                   // ICON_VOLUME_HIGH
    {volume_low_png_start, volume_low_png_end, NULL, false},                     // ICON_VOLUME_LOW
    {volume_mid_png_start, volume_mid_png_end, NULL, false},                     // ICON_VOLUME_MID
    {volume_minus_png_start, volume_minus_png_end, NULL, false},                 // ICON_VOLUME_MINUS
    {volume_mute_png_start, volume_mute_png_end, NULL, false},                   // ICON_VOLUME_MUTE
    {volume_plus_png_start, volume_plus_png_end, NULL, false},                   // ICON_VOLUME_PLUS
    {_1111_png_start, _1111_png_end, NULL, false},                               // ICON_1111
    {anc_off_png_start, anc_off_png_end, NULL, false},                           // ICON_ANC_OFF
    {anc_on_png_start, anc_on_png_end, NULL, false},                             // ICON_ANC_ON
    {mic_png_start, mic_png_end, NULL, false} ,                                   // ICON_MIC_PNG
    {bluetooth_2_png_start, bluetooth_2_png_end, NULL, false},                   // ICON_BLUETOOTH_2
    {settings_png_start, settings_png_end, NULL, false}                            // ICON_SETTINGS
};



void icon_manager_init(void)
{
    ESP_LOGD(TAG, "Icon manager initialized");
    // 初始化LODEPNG解码器
    lv_lodepng_init();
}

const lv_image_dsc_t* icon_manager_get_icon(icon_id_t icon_id)
{
    // 检查图标ID有效性
    if (icon_id >= ICON_MAX) {
        ESP_LOGE(TAG, "Invalid icon ID: %d", icon_id);
        return NULL;
    }

    // 如果图标尚未加载，则进行加载
    if (!icons[icon_id].loaded) {
        ESP_LOGD(TAG, "Loading icon %d into PSRAM", icon_id);
        
        // 获取图像数据大小
        size_t data_size = icons[icon_id].data_end - icons[icon_id].data_start;
        
        // 在PSRAM中分配内存来存储图像数据副本
        uint8_t *img_data_copy = (uint8_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
        if (img_data_copy == NULL) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM for icon data copy %d", icon_id);
            return NULL;
        }
        
        // 复制图像数据到PSRAM
        std::memcpy(img_data_copy, icons[icon_id].data_start, data_size);
        
        // 在PSRAM中分配内存来存储图像描述符
        lv_image_dsc_t *img_dsc = (lv_image_dsc_t*)heap_caps_malloc(sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM);
        if (img_dsc == NULL) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM for image descriptor %d", icon_id);
            free(img_data_copy);
            return NULL;
        }
        
        // 获取图像信息
        lv_image_header_t header;
        lv_result_t result = lv_image_decoder_get_info(img_data_copy, &header);
        if (result != LV_RESULT_OK) {
            ESP_LOGE(TAG, "Failed to get image info for icon %d", icon_id);
            free(img_data_copy);
            free(img_dsc);
            return NULL;
        }
        
        // 设置图像描述符
        img_dsc->header = header;
        img_dsc->data = img_data_copy;
        img_dsc->data_size = data_size;
        
        // 保存到图标信息结构中
        icons[icon_id].img_dsc = img_dsc;
        icons[icon_id].loaded = true;
        
        ESP_LOGD(TAG, "Icon %d loaded successfully into PSRAM (size: %d bytes)", icon_id, (int)data_size);
    }

    return icons[icon_id].img_dsc;
}

lv_obj_t* icon_manager_create_image(lv_obj_t* parent, icon_id_t icon_id, lv_coord_t x, lv_coord_t y)
{
    ESP_LOGD(TAG, "Creating image object for icon %d", icon_id);
    
    const lv_image_dsc_t* img_dsc = icon_manager_get_icon(icon_id);
    if (img_dsc == NULL) {
        ESP_LOGD(TAG, "Failed to get icon descriptor for icon %d", icon_id);
        return NULL;
    }

    lv_obj_t* img = lv_image_create(parent);
    if (img == NULL) {
        ESP_LOGD(TAG, "Failed to create image object");
        return NULL;
    }

    ESP_LOGD(TAG, "Setting image source");
    lv_image_set_src(img, img_dsc);
    lv_obj_set_pos(img, x, y);
    
    ESP_LOGD(TAG, "Image created successfully");
    return img;
}

void icon_manager_update_image(lv_obj_t* img, icon_id_t icon_id)
{
    ESP_LOGD(TAG, "Updating image object to icon %d", icon_id);
    
    if (img == NULL) {
        ESP_LOGE(TAG, "Image object is NULL");
        return;
    }
    
    const lv_image_dsc_t* img_dsc = icon_manager_get_icon(icon_id);
    if (img_dsc == NULL) {
        ESP_LOGE(TAG, "Failed to get icon descriptor for icon %d", icon_id);
        return;
    }

    lv_image_set_src(img, img_dsc);
    ESP_LOGD(TAG, "Image updated successfully");
}