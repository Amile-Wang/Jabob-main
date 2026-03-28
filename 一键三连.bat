@echo off
cd /d c:\Users\tiawang\my_code\Jabob-main
set IDF_PATH=C:\Users\tiawang\esp\v5.4.3\esp-idf
set IDF_PYTHON_ENV_PATH=C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env
set PATH=C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Users\tiawang\.espressif\tools\cmake\3.30.2\bin;C:\Users\tiawang\.espressif\tools\ninja\1.12.1;C:\Users\tiawang\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin;C:\Users\tiawang\esp\v5.4.3\esp-idf\tools;%PATH%

echo ========================================
echo ESP-IDF 一键三连：编译+烧录+监控
echo ========================================

echo 【步骤1/3】编译...
C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py build
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo 【步骤2/3】烧录...
C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py flash
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo 【步骤3/3】监控...
echo 监控已启动，按 Ctrl+C 退出
C:\Users\tiawang\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe C:\Users\tiawang\esp\v5.4.3\esp-idf\tools\idf.py monitor