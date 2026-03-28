# ESP-IDF 一键三连（简化版）
$env:IDF_PATH = "C:\Users\tiawang\esp\v5.4.3\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;" + $env:PATH

Set-Location "c:\Users\tiawang\my_code\Jabob-main"

Write-Host "开始一键三连：编译→烧录→监控" -ForegroundColor Cyan

# 编译
Write-Host "【1/3】编译..." -ForegroundColor Yellow
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 烧录
Write-Host "【2/3】烧录..." -ForegroundColor Yellow
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" flash
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 监控
Write-Host "【3/3】监控..." -ForegroundColor Yellow
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" monitor