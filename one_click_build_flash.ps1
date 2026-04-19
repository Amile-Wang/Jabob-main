# ESP-IDF 一键三连执行脚本
$env:IDF_PATH = "C:\Users\tiawang\esp\v5.4.3\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;" + $env:PATH

Set-Location "c:\Users\tiawang\my_code\Jabob-main"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ESP-IDF 一键三连开始" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# 步骤1：编译
Write-Host ""
Write-Host "【步骤1/3】编译项目..." -ForegroundColor Yellow
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" build
$buildResult = $LASTEXITCODE

if ($buildResult -eq 0) {
    Write-Host "✓ 编译成功！" -ForegroundColor Green
} else {
    Write-Host "✗ 编译失败，退出码: $buildResult" -ForegroundColor Red
    exit $buildResult
}

# 步骤2：烧录
Write-Host ""
Write-Host "【步骤2/3】烧录固件..." -ForegroundColor Yellow
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" flash
$flashResult = $LASTEXITCODE

if ($flashResult -eq 0) {
    Write-Host "✓ 烧录成功！" -ForegroundColor Green
} else {
    Write-Host "✗ 烧录失败，退出码: $flashResult" -ForegroundColor Red
    Write-Host "注意：请确保ESP32已连接" -ForegroundColor Yellow
    exit $flashResult
}

# 步骤3：监控
Write-Host ""
Write-Host "【步骤3/3】启动监控..." -ForegroundColor Yellow
Write-Host "监控已启动，按 Ctrl+C 退出监控" -ForegroundColor Cyan
Write-Host ""

& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" monitor