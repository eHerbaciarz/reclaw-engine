@echo off
rem ReClaw PS2: pobierz najnowszy build testowy, uruchom w PCSX2, zbierz logi
rem (Windows marks downloaded scripts: unblocked once, so it does not ask every time)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Unblock-File -LiteralPath '%~dp0test_ps2.ps1'" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_ps2.ps1" %*
if errorlevel 1 pause
