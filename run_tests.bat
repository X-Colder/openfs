@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\vcpkg\installed\x64-windows\debug\bin;C:\vcpkg\installed\x64-windows\bin;%PATH%
cd /d d:\workspace\llm\openfs\build\tests

echo === Running test_block_bitmap ===
test_block_bitmap.exe
echo EXIT_CODE=%ERRORLEVEL%

echo === Running test_wal_manager ===
test_wal_manager.exe
echo EXIT_CODE=%ERRORLEVEL%

echo === Running test_disk_manager ===
test_disk_manager.exe
echo EXIT_CODE=%ERRORLEVEL%