@echo off
set IDF_PATH=C:\Users\tiawang\esp\v5.4.3\esp-idf
set IDF_PYTHON_ENV_PATH=C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env
set PATH=C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;%PATH%

cd /d C:\Users\tiawang\my_code\Jabob-main
echo Starting build...
"C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" "C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py" build

if %ERRORLEVEL% EQU 0 (
    echo Build successful!
    exit 0
) else (
    echo Build failed with exit code: %ERRORLEVEL%
    exit %ERRORLEVEL%
)
