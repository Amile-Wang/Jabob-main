echo Please ron this batch file in ESP-IDF CMD console.

@echo off
rem set COM_PORT=COM19
set CERT_FILE=".\CustomerCertification\CybServer_DSpotterDev_Jabra.bin"
set LICENSE_FILE="License.dat"
set LICENSE_ADDR=0xE000
set ESP_TOOL="C:\Espressif\frameworks\esp-idf-v5.4.1\components\esptool_py\esptool\esptool.py"

if exist "%LICENSE_FILE%" (
	del %LICENSE_FILE%
)

rem Communicate with bootloader to get device unique ID, get license data from Cyberon license server then save to file.
if defined COM_PORT (
	if defined LICENSE_FILE (
		DSpotterLicenseForESP32.exe --cert_file %CERT_FILE% --com_port %COM_PORT% --esp_tool %ESP_TOOL% --out_file %LICENSE_FILE%
	) else (
		rem If no LICENSE_FILE defined, License_XXX.bin will be generated. XXX is the device ID.
		DSpotterLicenseForESP32.exe --cert_file %CERT_FILE% --com_port %COM_PORT% --esp_tool %ESP_TOOL%
	)
) else (
	rem If no COM_PORT defined, esp_tool will detect and select automatically.
	if defined LICENSE_FILE (
		DSpotterLicenseForESP32.exe --cert_file %CERT_FILE% --esp_tool %ESP_TOOL% --out_file %LICENSE_FILE%
	) else (
		rem If no LICENSE_FILE defined, License_XXX.bin will be generated. XXX is the device ID.
		DSpotterLicenseForESP32.exe --cert_file %CERT_FILE% --esp_tool %ESP_TOOL%
	)
)

if defined COM_PORT (
	python %ESP_TOOL% --chip esp32s3 --port %COM_PORT% write_flash %LICENSE_ADDR% %LICENSE_FILE%
) else (
	rem If no COM_PORT defined, esp_tool will detect and select automatically.
	python %ESP_TOOL% --chip esp32s3 write_flash %LICENSE_ADDR% %LICENSE_FILE%
)

set COM_PORT=
set CERT_FILE=
set LICENSE_FILE=
set LICENSE_ADDR=
set ESP_TOOL=

pause
