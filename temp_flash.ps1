$env:IDF_PATH = "C:\Users\tiawang\esp\v5.4.3\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;" + $env:PATH

Set-Location "C:\Users\tiawang\my_code\Jabob-main"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "One-Click: Build -> Flash -> Monitor" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

Write-Host ""
Write-Host "[1/3] Building..." -ForegroundColor Yellow
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" build
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed! Exit code: $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Build completed successfully!" -ForegroundColor Green

Write-Host ""
Write-Host "[2/3] Flashing..." -ForegroundColor Yellow
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" "-p" "COM3" "flash"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Flash failed! Exit code: $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Flash completed successfully!" -ForegroundColor Green

Write-Host ""
Write-Host "[3/3] Starting monitor..." -ForegroundColor Yellow
Write-Host "Monitor running. Press Ctrl+C to exit." -ForegroundColor Cyan
& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" "-p" "COM3" "monitor"
