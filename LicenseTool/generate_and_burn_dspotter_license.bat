@echo off
setlocal enabledelayedexpansion

REM DSpotter License Generation and Burning Script
REM Usage: generate_and_burn_dspotter_license.bat [COM_PORT]

REM Set default COM port if not provided
if "%1"=="" (
    set COM_PORT=COM3
) else (
    set COM_PORT=%1
)

echo DSpotter License Generation and Burning Tool
echo ========================================
echo Target COM Port: !COM_PORT!
echo.

REM Change to LicenseTool directory
cd /d "%~dp0"

REM Generate License.dat
echo Generating DSpotter License...
.\DSpotterLicenseForESP32.exe --cert_file "CybServer_DSpotterDev_Jabra.bin" --com_port "!COM_PORT!" --esp_tool "C:\Users\tiawang\esp\v5.4.3\esp-idf\components\esptool_py\esptool\esptool.py" --out_file "License.dat"

if %errorlevel% neq 0 (
    echo ERROR: Failed to generate License.dat
    pause
    exit /b 1
)

echo.
echo License generated successfully!

REM Burn License to flash address 0xE000 (56KB)
echo.
echo Burning License to flash address 0xE000...
python "C:\Users\tiawang\esp\v5.4.3\esp-idf\components\esptool_py\esptool\esptool.py" --port "!COM_PORT!" --baud 921600 write_flash 0xe000 License.dat

if %errorlevel% neq 0 (
    echo ERROR: Failed to burn License to flash
    pause
    exit /b 1
)

echo.
echo License burned successfully to address 0xE000!
echo Device is now ready for DSpotter wake word detection.

pause