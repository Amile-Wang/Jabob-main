DSpotter 唤醒词引擎集成文档
1. 集成概述
DSpotter 是 Cyberon 提供的商业级语音唤醒词解决方案，本项目集成了 Jabra 定制版试用 SDK，支持 "Hey Jabra" 唤醒词检测。

关键特性
原生唤醒词: 使用内置固化模型，无需外部模型文件
低延迟: 实时音频流处理，响应迅速
资源优化: 针对 ESP32-S3 平台优化，占用内存少
License 管理: 独立 License 文件，设备绑定验证
2. 文件结构
components/DSpotter/
├── CMakeLists.txt          # 组件构建配置
├── include/                # DSpotter SDK 头文件
│   ├── DSpotter.h
│   ├── DSpotterApi.h  
│   ├── DSpotterDefine.h
│   ├── DSpotterSDKApi.h
│   └── DSpotterType.h
└── libDSpotter_Jabra_Trial.a  # Jabra 定制试用版静态库

main/audio/wake_words/
├── dspotter_wake_word.h    # DSpotter 封装接口声明
├── dspotter_wake_word.cc   # DSpotter 封装实现
├── DSpotterSimple.h        # 简化调用封装
└── DSpotterSimple.c        # 简化调用实现

LicenseTool/                # License 管理工具
├── DSpotterLicenseForESP32.exe     # License 生成工具
├── CybServer_DSpotterDev_Jabra.bin # Jabra 认证文件
├── burn_license.bat        # License 烧录脚本
└── License.dat             # 设备专属 License 文件

partitions/v1/16m_dspotter_native.csv  # 专用分区表
3. 构建配置
3.1 分区表配置
csv
# ESP-IDF Partition Table for DSpotter Native Wake Word
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,    0x4000,
dspotter, data, unknown, 0xe000,    0x1000,  # License 专用分区
otadata,  data, ota,     0xf000,    0x2000,
phy_init, data, phy,     0x11000,   0x1000,
ota_0,    app,  ota_0,   0x12000,   7M,      # 对称 OTA 分区
ota_1,    app,  ota_1,   0x712000,  7M,
3.2 SDK 配置选项
在 menuconfig 中启用：

CONFIG_USE_DSPOTTER_WAKE_WORD=y
CONFIG_WAKE_WORD=dspotter
3.3 CMake 集成
components/DSpotter/CMakeLists.txt:

cmake
idf_component_register(
    SRCS ""
    INCLUDE_DIRS "include"
)
target_link_libraries(${COMPONENT_LIB} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libDSpotter_Jabra_Trial.a)
target_link_options(${COMPONENT_LIB} INTERFACE -Wl,--gc-sections)
4. 运行时架构
4.1 初始化流程
AudioService 初始化 → 检测配置选项
创建 DSpotterWakeWord 实例 → 调用 DSpotter_Init()
启动检测任务 → 在独立任务中运行 DSpotter_Process()
License 验证 → 自动从 Flash 地址 0xE000 读取验证
4.2 数据流处理
USB 麦克风 (48kHz) 
    ↓ 重采样 + 单声道混音
16kHz PCM 数据流
    ↓ 分帧 (480 samples = 30ms)
DSpotter_Process() 处理
    ↓ 唤醒词检测结果
回调通知 AudioService
4.3 内存管理
输入缓冲区: PSRAM 分配，480 samples × 2 bytes = 960 bytes
DSpotter 工作内存: SDK 内部管理，约 20-30KB
任务栈: 8192 words (32KB)，确保足够处理深度
5. License 管理
5.1 License 机制
存储位置: Flash 物理地址 0xE000 (56KB)
文件格式: 二进制加密文件 (License.dat)
设备绑定: 与 ESP32-S3 芯片唯一 ID 绑定
授权限制: Jabra 试用版，共 8 台设备配额
5.2 License 生成步骤
powershell
# 1. 进入 LicenseTool 目录
cd LicenseTool

# 2. 生成设备专属 License
.\DSpotterLicenseForESP32.exe --cert_file "CybServer_DSpotterDev_Jabra.bin" --com_port COM3 --esp_tool "C:\Users\tiawang\esp\v5.4.3\esp-idf\components\esptool_py\esptool\esptool.py" --out_file "License.dat"

# 3. 烧录 License 到设备
.\burn_license.bat
5.3 当前授权状态
总授权数: 8 台设备
已使用: 3 台设备
剩余: 5 台设备
6. API 接口说明
6.1 统一接口 (WakeWordInterface)
cpp
class DSpotterWakeWord : public WakeWordInterface {
public:
    void Initialize(int sample_rate, int channels) override;
    bool Process(const int16_t* audio_data, size_t num_samples) override;
    void SetDetectionCallback(std::function<void()> callback) override;
    void Start() override;
    void Stop() override;
};
6.2 核心方法
Initialize(): 设置采样率 (16kHz) 和通道数 (1)
Process(): 处理音频数据，返回是否检测到唤醒词
Start()/Stop(): 控制检测任务启停
6.3 回调机制
检测到 "Hey Jabra" 时，通过回调通知上层应用，触发语音交互流程。

7. 日志输出
7.1 正常工作日志
I (xxxxxx) DSpotterWakeWord: DSpotter wake word detected!
I (xxxxxx) AudioService: Wake word detected, starting voice interaction
7.2 错误日志
E (xxxxxx) DSpotterWakeWord: DSpotter initialization failed
E (xxxxxx) DSpotterWakeWord: License validation failed
8. 使用限制与注意事项
8.1 技术限制
仅支持 "Hey Jabra": 模型固化，无法更改唤醒词
试用版功能: 可能有时间或性能限制
设备绑定: License 与特定设备绑定，不可迁移
8.2 合规要求
开发测试: 试用版适用于个人学习和开发测试
商业部署: 需联系 Cyberon 获取正式商业授权
分发限制: 不得公开分发包含闭源库的项目
8.3 性能指标
CPU 占用: 约 15-20% (ESP32-S3 @ 240MHz)
内存占用: 约 30KB RAM
检测延迟: < 200ms
误唤醒率: < 1次/24小时 (安静环境)
9. 故障排除
9.1 常见问题
License 验证失败: 确认 License.dat 已正确烧录到 0xE000
无唤醒响应: 检查音频输入电平和采样率配置
系统崩溃: 检查任务栈大小和内存分配
9.2 调试命令
bash
# 查看分区表
idf.py partition-table

# 监控串口日志  
idf.py monitor

# 验证 License 状态
DSpotterLicenseForESP32.exe --cert_file "CybServer_DSpotterDev_Jabra.bin"
文档版本: 1.0
最后更新: 2026-03-31
适用版本: Jabob v2.0.5 Beta