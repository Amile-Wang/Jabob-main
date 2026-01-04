NEW_FILE_CODE
#include "gif_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
    {battery_anim_gif_start, battery_anim_gif_end, NULL, false},      // GIF_BATTERY_ANIM
    {bluetooth_anim_gif_start, bluetooth_anim_gif_end, NULL, false},  // GIF_BLUETOOTH_ANIM
    {wifi_anim_gif_start, wifi_anim_gif_end, NULL, false},            // GIF_WIFI_ANIM
    {loading_anim_gif_start, loading_anim_gif_end, NULL, false}       // GIF_LOADING_ANIM
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
        
        // 获取图像数据大小
        size_t data_size = gifs[gif_id].data_end - gifs[gif_id].data_start;
        
        // 在PSRAM中分配内存来存储图像数据副本
        uint8_t *img_data_copy = (uint8_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
        if (img_data_copy == NULL) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM for GIF data copy %d", gif_id);
            return NULL;
        }
        
        // 复制图像数据到PSRAM
        std::memcpy(img_data_copy, gifs[gif_id].data_start, data_size);
        
        // 在PSRAM中分配内存来存储图像描述符
        lv_img_dsc_t *img_dsc = (lv_img_dsc_t*)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);
        if (img_dsc == NULL) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM for image descriptor %d", gif_id);
            free(img_data_copy);
            return NULL;
        }
        
        // 获取图像信息
        lv_img_header_t header;
        lv_result_t result = lv_img_decoder_get_info(img_data_copy, &header);
        if (result != LV_RESULT_OK) {
            ESP_LOGE(TAG, "Failed to get image info for GIF %d", gif_id);
            free(img_data_copy);
            free(img_dsc);
            return NULL;
        }
        
        // 设置图像描述符
        img_dsc->header = header;
        img_dsc->data = img_data_copy;
        img_dsc->data_size = data_size;
        
        // 保存到GIF信息结构中
        gifs[gif_id].img_dsc = img_dsc;
        gifs[gif_id].loaded = true;
        
        ESP_LOGD(TAG, "GIF %d loaded successfully into PSRAM (size: %d bytes)", gif_id, (int)data_size);
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

std::unique_ptr<LvglGif> gif_manager_play_gif(lv_obj_t* gif_obj, gif_id_t gif_id)
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
    
    // 创建GIF控制器
    auto gif_controller = std::make_unique<LvglGif>(img_dsc);
    
    if (!gif_controller->IsLoaded()) {
        ESP_LOGE(TAG, "Failed to load GIF animation for GIF %d", gif_id);
        return nullptr;
    }
    
    // 设置帧更新回调
    gif_controller->SetFrameCallback([gif_obj, gif_controller_ptr = gif_controller.get()]() {
        lv_img_set_src(gif_obj, gif_controller_ptr->image_dsc());
    });
    
    // 设置初始帧并开始动画
    lv_img_set_src(gif_obj, gif_controller->image_dsc());
    gif_controller->Start();
    
    ESP_LOGI(TAG, "GIF animation started successfully for GIF ID %d", gif_id);
    
    return gif_controller;
}

void gif_manager_stop_gif(std::unique_ptr<LvglGif>& gif_controller)
{
    if (gif_controller) {
        gif_controller->Stop();
        gif_controller.reset();
        ESP_LOGD(TAG, "GIF animation stopped and cleaned up");
    }
}