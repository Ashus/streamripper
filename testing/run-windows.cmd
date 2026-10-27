@echo off
setlocal
set "SR_TOOLCHAIN=%~1"
if not defined SR_TOOLCHAIN set "SR_TOOLCHAIN=%~dp0..\build\toolchain\mingw64"
for %%I in ("%SR_TOOLCHAIN%") do set "SR_TOOLCHAIN=%%~fI"
set "PATH=%SR_TOOLCHAIN%\bin;%PATH%"
pushd "%~dp0.."
ctest --test-dir build --output-on-failure
if errorlevel 1 goto failed
python testing\http_security.py build\http_security_regression.exe
if errorlevel 1 goto failed
python testing\cli_options.py
if errorlevel 1 goto failed
python testing\console_output.py
if errorlevel 1 goto failed
python testing\console_close.py
if errorlevel 1 goto failed
python testing\package_smoke.py "%SR_TOOLCHAIN%"
if errorlevel 1 goto failed
python testing\sequence_number.py
if errorlevel 1 goto failed
python testing\recording_lock.py
if errorlevel 1 goto failed
python testing\metadata_filter.py
if errorlevel 1 goto failed
python testing\station_names.py dist\streamripper.exe
if errorlevel 1 goto failed
python testing\live_updates.py dist\streamripper.exe
if errorlevel 1 goto failed
python testing\stream_capture.py dist\streamripper.exe "%SR_TOOLCHAIN%\bin\openssl.exe"
if errorlevel 1 goto failed
python testing\metadata_encoding.py
if errorlevel 1 goto failed
python testing\audio_headers.py
if errorlevel 1 goto failed
python testing\aac_silence.py dist\streamripper.exe build\aac_silence_regression.exe --faac "%SR_TOOLCHAIN%\bin\faac.exe"
if errorlevel 1 goto failed
python testing\incomplete_duration.py
if errorlevel 1 goto failed
python testing\silence_output.py build\core_regression.exe build\sample-5s.mp3 "%SR_TOOLCHAIN%\bin"
if errorlevel 1 goto failed
python testing\https_smoke.py dist\streamripper.exe build\sample-5s.mp3 "%SR_TOOLCHAIN%\bin\openssl.exe"
if errorlevel 1 goto failed
python testing\http_framing.py dist\streamripper.exe build\sample-5s.mp3
if errorlevel 1 goto failed
python testing\ipv6_transport.py "%SR_TOOLCHAIN%"
if errorlevel 1 goto failed
python testing\proxy_connect.py "%SR_TOOLCHAIN%"
if errorlevel 1 goto failed
python testing\relay_stress.py dist\streamripper.exe build\sample-5s.mp3
if errorlevel 1 goto failed
python testing\relay_ports.py
if errorlevel 1 goto failed
python testing\wav_smoke.py dist\streamripper.exe build\core_regression.exe "%SR_TOOLCHAIN%\bin\lame.exe" build\sample-3s.wav build\sample-5s.mp3
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
