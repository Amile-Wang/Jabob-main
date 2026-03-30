echo Please ron this batch file in ESP-IDF CMD console.

@echo off
set COM_PORT=COM3
set LICENSE_FILE="License.dat"
set LICENSE_ADDR=0xE000
set ESP_TOOL="%IDF_PATH%\components\esptool_py\esptool\esptool.py"

rem Burn license binary file to flash at address LICENSE_ADDR.
if defined COM_PORT (
	python %ESP_TOOL% --chip esp32s3 --port %COM_PORT% write_flash %LICENSE_ADDR% %LICENSE_FILE%
) else (
	rem If no COM_PORT defined, esp_tool will detect and select automatically.
	python %ESP_TOOL% --chip esp32s3 write_flash %LICENSE_ADDR% %LICENSE_FILE%
)

set COM_PORT=
set LICENSE_FILE=
set LICENSE_ADDR=
set ESP_TOOL=

pause