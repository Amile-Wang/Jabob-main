# Kconfig 修改说明

## ✅ 已完成的修改

### 1. 文件位置
`main/Kconfig.projbuild`

### 2. 修改内容

#### (1) 添加板型配置选项（第 38-41 行）

在 `BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO` 之后添加了新的板型：

```kconfig
config BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO_UAC
    bool "面包板新版接线（WiFi）+ LCD + tianhao UAC 麦克风"
    depends on IDF_TARGET_ESP32S3
    select CONFIG_USB_HOST_ENABLE
    select CONFIG_USB_HOST_UAC_ENABLE
```

**说明：**
- 板型名称：`BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO_UAC`
- 显示名称：`面包板新版接线（WiFi）+ LCD + tianhao UAC 麦克风`
- 依赖芯片：`ESP32S3`
- 自动选择：`USB Host 驱动` 和 `USB Host UAC 驱动`

#### (2) 更新 LCD 类型选择依赖（第 340 行）

在 `DISPLAY_LCD_TYPE` choice 的 depends on 中添加了新板型：

**修改前：**
```kconfig
depends on BOARD_TYPE_BREAD_COMPACT_WIFI_LCD || BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO || ...
```

**修改后：**
```kconfig
depends on BOARD_TYPE_BREAD_COMPACT_WIFI_LCD || BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO || BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO_UAC || ...
```

**说明：** 确保新板型也能在 menuconfig 中选择 LCD 屏幕型号。

## 📋 使用方法

### 在 menuconfig 中选择板型

```bash
idf.py menuconfig
```

导航路径：
```
Xiaozhi Assistant → Board Type → 面包板新版接线（WiFi）+ LCD + tianhao UAC 麦克风
```

### 选择 LCD 型号

```
Xiaozhi Assistant → LCD Type → 选择你的 LCD 型号
```

### 验证 USB 驱动是否启用

选择板型后，以下选项会自动启用：
```
Component config → USB Support → Enable USB Host Driver [✓]
Component config → USB Support → USB Host UAC Driver [✓]
```

## 🔍 验证步骤

1. **打开 menuconfig**
   ```bash
   idf.py menuconfig
   ```

2. **检查板型选项**
   - 导航到 `Xiaozhi Assistant → Board Type`
   - 确认能看到 `面包板新版接线（WiFi）+ LCD + tianhao UAC 麦克风` 选项

3. **选择板型**
   - 选中该选项并按空格键

4. **保存配置**
   - 按 `S` 键保存
   - 按 `Q` 键退出

5. **验证编译**
   ```bash
   idf.py build
   ```

## ⚠️ 注意事项

1. **Kconfig 语法**
   - 缩进必须使用空格（通常是 4 个空格）
   - `bool` 后面的字符串用双引号包裹
   - `depends on` 和 `select` 语句要对齐

2. **依赖关系**
   - 新板型依赖于 `IDF_TARGET_ESP32S3`
   - 自动选择 USB 相关驱动，无需手动配置

3. **与其他文件的关系**
   - Kconfig 定义 menuconfig 选项
   - CMakeLists.txt 根据配置编译对应的源代码
   - config.json 提供构建时的 sdkconfig 追加项

## 📝 相关文件清单

完整的 BSP 包含以下文件：

```
main/
├── Kconfig.projbuild                      # ✅ 已更新 - menuconfig 配置
├── CMakeLists.txt                         # ✅ 已更新 - 构建系统配置
└── boards/bread-compact-wifi-lcd-tianhao-uac/
    ├── config.h                           # ✅ GPIO 引脚配置
    ├── config.json                        # ✅ 构建配置
    ├── compact_wifi_board_lcd_uac.cc      # ✅ 板级实现
    ├── README.md                          # ✅ 使用文档
    └── IMPLEMENTATION_SUMMARY.md          # ✅ 技术总结
```

## 🎯 配置文件对应关系

| 文件 | 作用 | 配置项 |
|------|------|--------|
| Kconfig.projbuild | menuconfig 界面 | BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO_UAC |
| CMakeLists.txt | 编译源码 | CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO_UAC |
| config.json | sdkconfig 追加 | CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD_TIANHAO_UAC=y |
| config.h | GPIO 定义 | AUDIO_INPUT_SAMPLE_RATE 等 |

## ✅ 完成状态

- [x] 创建板级目录和源文件
- [x] 编写 config.h 配置文件
- [x] 实现 compact_wifi_board_lcd_uac.cc
- [x] 创建 config.json 构建配置
- [x] 更新 main/CMakeLists.txt
- [x] **更新 main/Kconfig.projbuild**
- [x] 编写文档（README.md, IMPLEMENTATION_SUMMARY.md）
- [x] 代码语法检查通过

---

**修改日期**: 2026-03-17  
**修改者**: AI Assistant  
**状态**: ✅ 完成
