#ifndef GIF_MANAGER_H
#define GIF_MANAGER_H

#include "lvgl.h"
#include "lvgl_display/gif/lvgl_gif.h"
// 移除std::memory相关包含，使用C风格指针
#include "lvgl_display/gif/lvgl_gif.h"

// GIF ID枚举
typedef enum {
    GIF_1,
    GIF_JINGLE,
    GIF_JINGXIA,
    GIF_JINGXING,
    GIF_MENGLE,
    GIF_MINGBAI,
    GIF_MINGBAILE,
    GIF_QINGXING,
    GIF_SHENMA,
    GIF_YAOTU,
    GIF_YUNLE,
    GIF_MAX
} gif_id_t;

/**
 * @brief 初始化GIF管理系统
 */
void gif_manager_init(void);

/**
 * @brief 根据GIF ID获取GIF图像描述符
 * 
 * @param gif_id GIF ID
 * @return lv_img_dsc_t* 图像描述符指针
 */
const lv_img_dsc_t* gif_manager_get_gif(gif_id_t gif_id);

/**
 * @brief 创建并显示GIF动画对象
 * 
 * @param parent 父对象
 * @param gif_id GIF ID
 * @param x X坐标
 * @param y Y坐标
 * @return lv_obj_t* 图像对象
 */
lv_obj_t* gif_manager_create_gif(lv_obj_t* parent, gif_id_t gif_id, lv_coord_t x, lv_coord_t y);

/**
 * @brief 更新图像对象的GIF
 * 
 * @param img 图像对象
 * @param gif_id 新的GIF ID
 */
void gif_manager_update_gif(lv_obj_t* img, gif_id_t gif_id);

/**
 * @brief 播放指定的GIF动画
 * 
 * @param gif_obj GIF对象
 * @param gif_id GIF ID
 * @return LvglGif* 返回GIF控制器指针，用于控制动画
 */
LvglGif* gif_manager_play_gif(lv_obj_t* gif_obj, gif_id_t gif_id);

/**
 * @brief 停止GIF动画
 * 
 * @param gif_controller GIF控制器指针
 */
void gif_manager_stop_gif(LvglGif* gif_controller);

#endif /* GIF_MANAGER_H */