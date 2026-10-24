@echo off
setlocal
set "SR_TOOLCHAIN=%~1"
if not defined SR_TOOLCHAIN set "SR_TOOLCHAIN=C:\Download\streamripper-toolchain\mingw64"
set "SR_7ZIP=%~2"
if not defined SR_7ZIP set "SR_7ZIP=C:\Program Files\7-Zip\7z.exe"
if not exist "%SR_7ZIP%" (
    echo 7-Zip not found: "%SR_7ZIP%"
    exit /b 1
)
python "%~dp0package-windows.py" "%SR_TOOLCHAIN%"
if errorlevel 1 exit /b 1
set /p SR_RELEASE_VERSION=<"%~dp0VERSION"
set "SR_ARCHIVE=%~dp0build\release\streamripper-%SR_RELEASE_VERSION%-win64.7z"
set "SR_FILE_LIST=%~dp0build\release\streamripper-%SR_RELEASE_VERSION%-win64-files.txt"
rem Always create a fresh archive so removed files cannot survive from an older build.
if exist "%SR_ARCHIVE%" del /q "%SR_ARCHIVE%"
if exist "%SR_ARCHIVE%" exit /b 1
pushd "%~dp0dist"
"%SR_7ZIP%" a -t7z -m0=lzma2 -mx=9 -scsUTF-8 "%SR_ARCHIVE%" "@%SR_FILE_LIST%"
set "SR_RESULT=%ERRORLEVEL%"
popd
if not "%SR_RESULT%"=="0" exit /b %SR_RESULT%
"%SR_7ZIP%" t "%SR_ARCHIVE%"
if errorlevel 1 exit /b 1
python "%~dp0package-windows.py" --checksum "%SR_ARCHIVE%"
exit /b %ERRORLEVEL%
