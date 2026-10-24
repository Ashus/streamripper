@echo off
setlocal
set "SR_TOOLCHAIN=%~1"
if not defined SR_TOOLCHAIN set "SR_TOOLCHAIN=C:\Download\streamripper-toolchain\mingw64"
set "PATH=%SR_TOOLCHAIN%\bin;%PATH%"
pushd "%~dp0.."
ctest --test-dir build --output-on-failure
if errorlevel 1 goto failed
python testing\package_smoke.py "%SR_TOOLCHAIN%"
if errorlevel 1 goto failed
python testing\sequence_number.py
if errorlevel 1 goto failed
python testing\metadata_filter.py
if errorlevel 1 goto failed
python testing\audio_headers.py
if errorlevel 1 goto failed
python testing\silence_output.py build\core_regression.exe build\sample-5s.mp3 "%SR_TOOLCHAIN%\bin"
if errorlevel 1 goto failed
python testing\https_smoke.py dist\streamripper.exe build\sample-5s.mp3 "%SR_TOOLCHAIN%\bin\openssl.exe"
if errorlevel 1 goto failed
python testing\http_framing.py dist\streamripper.exe build\sample-5s.mp3
if errorlevel 1 goto failed
python testing\relay_stress.py dist\streamripper.exe build\sample-5s.mp3
if errorlevel 1 goto failed
python testing\wav_smoke.py dist\streamripper.exe build\core_regression.exe "%SR_TOOLCHAIN%\bin\lame.exe" build\sample-3s.wav build\sample-5s.mp3
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
