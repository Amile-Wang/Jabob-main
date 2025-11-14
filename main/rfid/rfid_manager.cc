#include "rfid_manager.h"
#include "application.h"
#include "mcp_server.h"
#include <esp_log.h>

static const char* TAG = "RfidManager";

// RC522标签回调函数
void rc522_tag_callback(uint8_t* serial_number) {
    RfidManager::GetInstance().HandleTag(serial_number);
}

void RfidManager::Initialize(int miso_io, int mosi_io, int sck_io, int sda_io) {
    rc522_config_.miso_io = miso_io;
    rc522_config_.mosi_io = mosi_io;
    rc522_config_.sck_io = sck_io;
    rc522_config_.sda_io = sda_io;
    rc522_config_.callback = rc522_tag_callback;
    rc522_config_.spi_host_id = VSPI_HOST;
    rc522_config_.scan_interval_ms = 125;
    rc522_config_.task_stack_size = 4096;
    rc522_config_.task_priority = 4;

    esp_err_t ret = rc522_start(rc522_config_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RC522: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "RC522 initialized successfully");

    // 注册MCP工具
    auto& mcp_server = McpServer::GetInstance();
    
    // 添加预设的ID到位置映射工具
    mcp_server.AddTool(
        "self.rfid.add_id_location_mapping",
        "添加ID卡到位置的映射关系",
        PropertyList({
            Property("card_id", kPropertyTypeInteger),
            Property("location", kPropertyTypeString)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            uint64_t card_id = static_cast<uint64_t>(properties["card_id"].value<int>());
            std::string location = properties["location"].value<std::string>();
            AddIdLocationMapping(card_id, location);
            return true;
        }
    );

    // 获取当前位置工具
    mcp_server.AddTool(
        "self.rfid.get_current_location",
        "获取当前通过RFID识别的办公室位置",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            return GetCurrentLocation();
        }
    );
}

RfidManager::~RfidManager() {
    rc522_destroy();
}

void RfidManager::AddIdLocationMapping(uint64_t card_id, const std::string& location) {
    id_location_map_[card_id] = location;
    ESP_LOGI(TAG, "Added mapping: Card ID %llu -> Location %s", card_id, location.c_str());
}

void RfidManager::HandleTag(uint8_t* serial_number) {
    uint64_t card_id = rc522_sn_to_u64(serial_number);
    ESP_LOGI(TAG, "Detected RFID card with ID: %llu", card_id);
    
    auto it = id_location_map_.find(card_id);
    if (it != id_location_map_.end()) {
        current_location_ = it->second;
        Application::GetInstance().SetOfficeLocation(current_location_);
        ESP_LOGI(TAG, "Location updated to: %s", current_location_.c_str());
    } else {
        ESP_LOGW(TAG, "Unknown card ID: %llu", card_id);
        current_location_ = "unknown";
    }
}