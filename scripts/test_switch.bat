@echo off
rem ReClaw: pobierz najnowszy build testowy, wgraj do Ryujinxa i uruchom
rem (Windows marks downloaded scripts: unblocked once, so it does not ask every time)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Unblock-File -LiteralPath '%~dp0test_switch.ps1'" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_switch.ps1" %*
if errorlevel 1 pause
