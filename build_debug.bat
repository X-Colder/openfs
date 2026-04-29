@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d d:\workspace\llm\openfs
rmdir /s /q build 2>nul
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows
if %ERRORLEVEL% NEQ 0 (
    echo CMAKE_CONFIGURE_FAILED
    exit /b 1
)
cmake --build build --config Debug
if %ERRORLEVEL% NEQ 0 (
    echo BUILD_FAILED
    exit /b 1
)
echo BUILD_SUCCESS