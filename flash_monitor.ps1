# ESP-IDF 一键三连：编译+烧录+监控
$env:IDF_PATH = "C:\Users\tiawang\esp\v5.4.3\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;" + $env:PATH

Set-Location "c:\Users\tiawang\my_code\Jabob-main"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ESP-IDF 一键三连开始" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 步骤1：编译
Write-Host "【步骤1/3】编译项目..." -ForegroundColor Yellow
$processInfo = New-Object System.Diagnostics.ProcessStartInfo
$processInfo.FileName = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"
$processInfo.Arguments = "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py build"
$processInfo.UseShellExecute = $false
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$processInfo.CreateNoWindow = $true

$process = New-Object System.Diagnostics.Process
$process.StartInfo = $processInfo
$process.Start() | Out-Null

$output = $process.StandardOutput.ReadToEnd()
$errorOutput = $process.StandardError.ReadToEnd()
$process.WaitForExit()

Write-Host $output
if ($errorOutput) {
    Write-Host $errorOutput -ForegroundColor Red
}

$buildExitCode = $process.ExitCode
if ($buildExitCode -eq 0) {
    Write-Host "✓ 编译成功！" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "✗ 编译失败，退出码: $buildExitCode" -ForegroundColor Red
    Write-Host ""
    exit $buildExitCode
}

# 步骤2：烧录
Write-Host "【步骤2/3】烧录固件..." -ForegroundColor Yellow
$processInfo = New-Object System.Diagnostics.ProcessStartInfo
$processInfo.FileName = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"
$processInfo.Arguments = "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py flash"
$processInfo.UseShellExecute = $false
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$processInfo.CreateNoWindow = $false

$process = New-Object System.Diagnostics.Process
$process.StartInfo = $processInfo
$process.Start() | Out-Null

$output = $process.StandardOutput.ReadToEnd()
$errorOutput = $process.StandardError.ReadToEnd()
$process.WaitForExit()

Write-Host $output
if ($errorOutput) {
    Write-Host $errorOutput -ForegroundColor Red
}

$flashExitCode = $process.ExitCode
if ($flashExitCode -eq 0) {
    Write-Host "✓ 烧录成功！" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "✗ 烧录失败，退出码: $flashExitCode" -ForegroundColor Red
    Write-Host ""
    Write-Host "注意：烧录失败可能是因为设备未连接或端口被占用" -ForegroundColor Yellow
    exit $flashExitCode
}

# 步骤3：监控
Write-Host "【步骤3/3】启动监控..." -ForegroundColor Yellow
Write-Host "监控已启动，按 Ctrl+C 退出" -ForegroundColor Cyan
Write-Host ""

& "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" monitor