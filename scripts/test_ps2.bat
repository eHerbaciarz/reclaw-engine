@echo off
rem ReClaw PS2: pobierz najnowszy build testowy, uruchom w PCSX2, zbierz logi
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_ps2.ps1" %*
if errorlevel 1 pause
