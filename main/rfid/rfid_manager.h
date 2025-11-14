#ifndef RFID_MANAGER_H
#define RFID_MANAGER_H

#include <map>
#include <string>
#include <cstdint>
#include "rc522.h"

class RfidManager {
public:
    static RfidManager& GetInstance() {
        static RfidManager instance;
        return instance;
    }

    // 初始化RFID读卡器
    void Initialize(int miso_io, int mosi_io, int sck_io, int sda_io);
    
    // 添加ID到位置的映射
    void AddIdLocationMapping(uint64_t card_id, const std::string& location);
    
    // 获取当前读取到的位置
    std::string GetCurrentLocation() const { return current_location_; }
    
    // 处理读取到的标签
    void HandleTag(uint8_t* serial_number);

private:
    RfidManager() = default;
    ~RfidManager();
    
    // ID到位置的映射表
    std::map<uint64_t, std::string> id_location_map_;
    
    // 当前位置
    std::string current_location_;
    
    // RC522配置
    rc522_config_t rc522_config_;
};

#endif // RFID_MANAGER_H