#ifndef _OTA_H
#define _OTA_H

#include <functional>
#include <string>

#include <esp_err.h>
#include "board.h"

class Ota {
public:
    Ota();
    ~Ota();

    bool CheckVersion();
    esp_err_t Activate();
    bool HasActivationChallenge() { return has_activation_challenge_; }
    bool HasNewVersion() { return has_new_version_; }
    bool HasMqttConfig() { return has_mqtt_config_; }
    bool HasWebsocketConfig() { return has_websocket_config_; }
    bool HasActivationCode() { return has_activation_code_; }
    bool HasServerTime() { return has_server_time_; }
    bool StartUpgrade(std::function<void(int progress, size_t speed)> callback);
    void MarkCurrentVersionValid();

    const std::string& GetFirmwareVersion() const { return firmware_version_; }
    const std::string& GetCurrentVersion() const { return current_version_; }
    const std::string& GetActivationMessage() const { return activation_message_; }
    const std::string& GetActivationCode() const { return activation_code_; }
    std::string GetCheckVersionUrl();

private:
    std::string activation_message_;
    std::string activation_code_;
    bool has_new_version_ = false;
    // ----------------------------------------------------------------------------
    // force_upgrade_：服务端 OTA check 响应里 firmware.force == 1 时置 true。
    //
    // 作用：让 StartUpgrade 流程跳过 ota.cc 内部"image header 里的新固件版本号
    // == 当前固件版本号 → return false"那道硬编码同版本拒绝（见 ota.cc 同版本判定处）。
    //
    // 为什么要这个标志：之前 force=1 实际上只让外层 `if (ota.HasNewVersion())` 通过、
    // 设备 *开始* 下载，但内层下载到 image header 时 `memcmp(new, current) == 0`
    // 又把同版本号刷拒绝了。客户端要在同版本号上重刷 .bin 内容（比如改 wifi 配置、
    // 加一行 log 但版本号忘了 bump）就走不通，只能靠 bump PROJECT_VER 强迫触发。
    //
    // 加这个标志后：服务端 force=1 → 客户端不仅愿意开始下载，下载流也不再被同版本号
    // 拦截；只要 .bin 通过 ESP-IDF 镜像校验，就真的写 ota_X 分区并 reboot。
    //
    // 注意：本字段每次 CheckVersion 时重置（基于服务端最新响应）。生命周期跟随
    // has_new_version_，单次 OTA 检查的快照状态。
    // ----------------------------------------------------------------------------
    bool force_upgrade_ = false;
    bool has_mqtt_config_ = false;
    bool has_websocket_config_ = false;
    bool has_server_time_ = false;
    bool has_activation_code_ = false;
    bool has_serial_number_ = false;
    bool has_activation_challenge_ = false;
    std::string current_version_;
    std::string firmware_version_;
    std::string firmware_url_;
    std::string activation_challenge_;
    std::string serial_number_;
    int activation_timeout_ms_ = 30000;

    bool Upgrade(const std::string& firmware_url);
    std::function<void(int progress, size_t speed)> upgrade_callback_;
    std::vector<int> ParseVersion(const std::string& version);
    bool IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion);
    std::string GetActivationPayload();
    std::unique_ptr<Http> SetupHttp();
};

#endif // _OTA_H
