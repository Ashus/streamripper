@echo off
setlocal
set "SR_TOOLCHAIN=%~1"
if not defined SR_TOOLCHAIN set "SR_TOOLCHAIN=%~dp0build\toolchain\mingw64"
for %%I in ("%SR_TOOLCHAIN%") do set "SR_TOOLCHAIN=%%~fI"
set "PATH=%SR_TOOLCHAIN%\bin;%PATH%"
set "PKG_CONFIG_PATH=%SR_TOOLCHAIN%\lib\pkgconfig"
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DSR_MODERN_WINDOWS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%SR_TOOLCHAIN%"
if errorlevel 1 exit /b 1
cmake --build "%~dp0build"
