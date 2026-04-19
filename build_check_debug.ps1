$env:IDF_PATH = "C:\Users\tiawang\esp\v5.4.3\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;" + $env:PATH

Set-Location "C:\Users\tiawang\my_code\Jabob-main"

Write-Host "Starting build with full output..." -ForegroundColor Cyan

$output = & "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" build 2>&1 | Out-String

Write-Host "=== Build Output ===" -ForegroundColor Yellow
Write-Host $output
Write-Host "=== End Build Output ===" -ForegroundColor Yellow

$exitCode = $LASTEXITCODE

Write-Host "Exit code: $exitCode" -ForegroundColor Cyan

if ($exitCode -eq 0) {
    Write-Host "Build successful!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "Build failed with exit code: $exitCode" -ForegroundColor Red
    exit $exitCode
}
