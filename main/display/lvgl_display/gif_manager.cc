
#include "gif_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <cstring>

// 嵌入的GIF图像数据 - 从assets/gif目录
extern const uint8_t _1_gif_start[] asm("_binary_1_gif_start");
extern const uint8_t _1_gif_end[]   asm("_binary_1_gif_end");
extern const uint8_t jingle_gif_start[] asm("_binary_jingle_gif_start");
extern const uint8_t jingle_gif_end[]   asm("_binary_jingle_gif_end");
extern const uint8_t jingxia_gif_start[] asm("_binary_jingxia_gif_start");
extern const uint8_t jingxia_gif_end[]   asm("_binary_jingxia_gif_end");
extern const uint8_t jingxing_gif_start[] asm("_binary_jingxing_gif_start");
extern const uint8_t jingxing_gif_end[]   asm("_binary_jingxing_gif_end");
extern const uint8_t mengle_gif_start[] asm("_binary_mengle_gif_start");
extern const uint8_t mengle_gif_end[]   asm("_binary_mengle_gif_end");
extern const uint8_t mingbai_gif_start[] asm("_binary_mingbai_gif_start");
extern const uint8_t mingbai_gif_end[]   asm("_binary_mingbai_gif_end");
extern const uint8_t mingbaile_gif_start[] asm("_binary_mingbaile_gif_start");
extern const uint8_t mingbaile_gif_end[]   asm("_binary_mingbaile_gif_end");
extern const uint8_t qingxing_gif_start[] asm("_binary_qingxing_gif_start");
extern const uint8_t qingxing_gif_end[]   asm("_binary_qingxing_gif_end");
extern const uint8_t shenma_gif_start[] asm("_binary_shenma_gif_start");
extern const uint8_t shenma_gif_end[]   asm("_binary_shenma_gif_end");
extern const uint8_t yaotu_gif_start[] asm("_binary_yaotu_gif_start");
extern const uint8_t yaotu_gif_end[]   asm("_binary_yaotu_gif_end");
extern const uint8_t yunle_gif_start[] asm("_binary_yunle_gif_start");
extern const uint8_t yunle_gif_end[]   asm("_binary_yunle_gif_end");

static const char *TAG = "gif_manager";

// GIF信息结构体
typedef struct {
    const uint8_t *data_start;
    const uint8_t *data_end;
    lv_img_dsc_t *img_dsc;  // 存储解码后的图像描述符
    bool loaded;
} gif_info_t;

// GIF信息数组
static gif_info_t gifs[GIF_MAX] = {
    {_1_gif_start, _1_gif_end, NULL, false},           // GIF_1
    {jingle_gif_start, jingle_gif_end, NULL, false},   // GIF_JINGLE
    {jingxia_gif_start, jingxia_gif_end, NULL, false}, // GIF_JINGXIA
    {jingxing_gif_start, jingxing_gif_end, NULL, false}, // GIF_JINGXING
    {mengle_gif_start, mengle_gif_end, NULL, false},   // GIF_MENGLE
    {mingbai_gif_start, mingbai_gif_end, NULL, false}, // GIF_MINGBAI
    {mingbaile_gif_start, mingbaile_gif_end, NULL, false}, // GIF_MINGBAILE
    {qingxing_gif_start, qingxing_gif_end, NULL, false}, // GIF_QINGXING
    {shenma_gif_start, shenma_gif_end, NULL, false},   // GIF_SHENMA
    {yaotu_gif_start, yaotu_gif_end, NULL, false},     // GIF_YAOTU
    {yunle_gif_start, yunle_gif_end, NULL, false}      // GIF_YUNLE
};

void gif_manager_init(void)
{
    ESP_LOGD(TAG, "GIF manager initialized");
}

const lv_img_dsc_t* gif_manager_get_gif(gif_id_t gif_id)
{
    // 检查GIF ID有效性
    if (gif_id >= GIF_MAX) {
        ESP_LOGE(TAG, "Invalid GIF ID: %d", gif_id);
        return NULL;
    }

    // 如果GIF尚未加载，则进行加载
    if (!gifs[gif_id].loaded) {
        ESP_LOGD(TAG, "Loading GIF %d into PSRAM", gif_id);
        
        // 获取GIF数据大小
        size_t gif_data_size = gifs[gif_id].data_end - gifs[gif_id].data_start;
        
        // 复制GIF数据到PSRAM
        uint8_t *gif_data_copy = (uint8_t *)heap_caps_malloc(gif_data_size, MALLOC_CAP_SPIRAM);
        if (!gif_data_copy) {
            ESP_LOGE(TAG, "Failed to allocate memory for GIF %d data", gif_id);
            return NULL;
        }
        memcpy(gif_data_copy, gifs[gif_id].data_start, gif_data_size);
        
        // 为图像描述符分配内存
        lv_img_dsc_t *img_dsc = (lv_img_dsc_t *)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);
        if (!img_dsc) {
            ESP_LOGE(TAG, "Failed to allocate memory for image descriptor");
            free(gif_data_copy);
            return NULL;
        }

        // 设置图像描述符 - 由于是GIF数据，使用RAW格式
        lv_image_header_t header;
        header.magic = LV_IMAGE_HEADER_MAGIC;
        header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
        header.cf = LV_COLOR_FORMAT_RAW; // GIF数据使用RAW格式
        header.w = 0; // 宽高将在使用时由LVGL解码器解析
        header.h = 0;
        header.stride = 0;
        img_dsc->header = header;
        img_dsc->data = gif_data_copy;
        img_dsc->data_size = gif_data_size;
        
        // 保存到GIF信息结构中
        gifs[gif_id].img_dsc = img_dsc;
        gifs[gif_id].loaded = true;
        
        ESP_LOGD(TAG, "GIF %d loaded successfully into PSRAM (size: %d bytes)", gif_id, (int)gif_data_size);
    }

    return gifs[gif_id].img_dsc;
}

lv_obj_t* gif_manager_create_gif(lv_obj_t* parent, gif_id_t gif_id, lv_coord_t x, lv_coord_t y)
{
    ESP_LOGD(TAG, "Creating GIF object for GIF %d", gif_id);
    
    const lv_img_dsc_t* img_dsc = gif_manager_get_gif(gif_id);
    if (img_dsc == NULL) {
        ESP_LOGD(TAG, "Failed to get GIF descriptor for GIF %d", gif_id);
        return NULL;
    }

    lv_obj_t* img = lv_img_create(parent);
    if (img == NULL) {
        ESP_LOGD(TAG, "Failed to create image object");
        return NULL;
    }

    ESP_LOGD(TAG, "Setting image source");
    lv_img_set_src(img, img_dsc);
    lv_obj_set_pos(img, x, y);
    
    ESP_LOGD(TAG, "GIF object created successfully");
    return img;
}

void gif_manager_update_gif(lv_obj_t* img, gif_id_t gif_id)
{
    ESP_LOGD(TAG, "Updating image object to GIF %d", gif_id);
    
    if (img == NULL) {
        ESP_LOGE(TAG, "Image object is NULL");
        return;
    }
    
    const lv_img_dsc_t* img_dsc = gif_manager_get_gif(gif_id);
    if (img_dsc == NULL) {
        ESP_LOGE(TAG, "Failed to get GIF descriptor for GIF %d", gif_id);
        return;
    }

    lv_img_set_src(img, img_dsc);
    ESP_LOGD(TAG, "GIF updated successfully");
}

LvglGif* gif_manager_play_gif(lv_obj_t* gif_obj, gif_id_t gif_id)
{
    ESP_LOGD(TAG, "Playing GIF animation for GIF ID %d", gif_id);
    
    if (gif_obj == NULL) {
        ESP_LOGE(TAG, "GIF object is NULL");
        return nullptr;
    }
    
    const lv_img_dsc_t* img_dsc = gif_manager_get_gif(gif_id);
    if (img_dsc == NULL) {
        ESP_LOGE(TAG, "Failed to get GIF descriptor for GIF %d", gif_id);
        return nullptr;
    }
    
    // 创建GIF控制器，使用原始指针而不是unique_ptr
    LvglGif* gif_controller = new LvglGif(img_dsc);
    
    if (!gif_controller->IsLoaded()) {
        ESP_LOGE(TAG, "Failed to load GIF animation for GIF %d", gif_id);
        delete gif_controller;
        return nullptr;
    }
    
    // 设置帧更新回调
    gif_controller->SetFrameCallback([gif_obj, gif_controller_ptr = gif_controller]() {
        lv_img_set_src(gif_obj, gif_controller_ptr->image_dsc());
    });
    
    // 设置初始帧并开始动画
    lv_img_set_src(gif_obj, gif_controller->image_dsc());
    gif_controller->Start();
    
    ESP_LOGI(TAG, "GIF animation started successfully for GIF ID %d", gif_id);
    
    return gif_controller;
}

void gif_manager_stop_gif(LvglGif* gif_controller)
{
    if (gif_controller) {
        gif_controller->Stop();
        delete gif_controller;
        gif_controller = nullptr;
        ESP_LOGD(TAG, "GIF animation stopped and cleaned up");
    }
}