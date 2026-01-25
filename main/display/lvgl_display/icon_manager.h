#ifndef ICON_MANAGER_H
#define ICON_MANAGER_H

#include "lvgl.h"

// 图标ID枚举
typedef enum {
    ICON_BATTERY,
    // ICON_BATTERY_CHARGING,
    // ICON_BATTERY_FULL,
    // ICON_BATTERY_HIGH,
    // ICON_BATTERY_LOW,
    // ICON_BATTERY_LOW_WARNING,
    // ICON_BATTERY_MID,
    // ICON_BATTERY_WARNING,
    // ICON_BLUETOOTH_0,
    // ICON_BLUETOOTH_1,
    // ICON_BLUETOOTH_LARGE = ICON_BLUETOOTH_0,  // 别名保持兼容性
    // ICON_BLUETOOTH_LARGE_DARK = ICON_BLUETOOTH_1,  // 别名保持兼容性
    // ICON_BLUETOOTH_L,
    // ICON_BLUETOOTH_L_DARK,
    // ICON_BLUETOOTH_S,
    // ICON_BLUETOOTH_BLUE,
    // ICON_BLUETOOTH_BLUE2,
    // ICON_BLUETOOTH_DARK,
    // ICON_BLUETOOTH_OFF,
    // ICON_BLUETOOTH_WHITE,
    // ICON_CALLING,
    // ICON_CALLING_GREEN,
    // ICON_CARRIGHT,
    // ICON_CARETLEFT,
    // ICON_CHECKCIRCLE,
    // ICON_CONNECTED,
    // ICON_CURSOR,
    // ICON_DISCONNECTED,
    // ICON_DOT_GREEN,
    // ICON_GRAY,
    // ICON_LOCK,
    // ICON_MENU,
    // ICON_MENU_BUTTON,
    // ICON_MIC,
    // ICON_MIC_OFF,
    // ICON_MIC_ON,
    // ICON_PHONE,
    // ICON_PHONEDISCONNECT,
    // ICON_PHONE_DISCONNECT = ICON_PHONEDISCONNECT,  // 别名保持兼容性
    // ICON_PHONE_DISCONNECT_DARK,
    // ICON_PROHIBIT,
    // ICON_SELECT,
    // ICON_STATUS_CALLING,
    // ICON_STATUS_IDLE,
    // ICON_STATUS_MEETING,
    // ICON_STATUS_MUSIC,
    // ICON_VOLUME_HIGH,
    // ICON_VOLUME_LOW,
    // ICON_VOLUME_MID,
    // ICON_VOLUME_MINUS,
    // ICON_VOLUME_MUTE,
    // ICON_VOLUME_PLUS,
    // ICON_1111,
    // ICON_ANC_OFF,
    // ICON_ANC_ON,
    // ICON_MIC_PNG,
    // ICON_BLUETOOTH_2,  // 新增蓝牙2图标
    // ICON_SETTINGS,
    ICON_MAX
} icon_id_t;

/**
 * @brief 初始化图标管理系统
 */
void icon_manager_init(void);

/**
 * @brief 根据图标ID获取图像描述符
 * 
 * @param icon_id 图标ID
 * @return lv_image_dsc_t* 图像描述符指针
 */
const lv_image_dsc_t* icon_manager_get_icon(icon_id_t icon_id);

/**
 * @brief 创建并显示图标图像对象
 * 
 * @param parent 父对象
 * @param icon_id 图标ID
 * @param x X坐标
 * @param y Y坐标
 * @return lv_obj_t* 图像对象
 */
lv_obj_t* icon_manager_create_image(lv_obj_t* parent, icon_id_t icon_id, lv_coord_t x, lv_coord_t y);

/**
 * @brief 更新图像对象的图标
 * 
 * @param img 图像对象
 * @param icon_id 新的图标ID
 */
void icon_manager_update_image(lv_obj_t* img, icon_id_t icon_id);

#endif /* ICON_MANAGER_H */