# ESP-IDF Build Script with output capture
$env:IDF_PATH = "C:\Users\tiawang\esp\v5.4.3\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;" + $env:PATH

Set-Location "c:\Users\tiawang\my_code\Jabob-main"

Write-Host "Starting ESP-IDF build..." -ForegroundColor Cyan

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

$exitCode = $process.ExitCode
Write-Host "Build completed with exit code: $exitCode" -ForegroundColor Cyan

exit $exitCode