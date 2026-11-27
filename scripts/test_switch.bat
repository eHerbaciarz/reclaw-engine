@echo off
rem ReClaw: pobierz najnowszy build testowy, wgraj do Ryujinxa i uruchom
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_switch.ps1" %*
if errorlevel 1 pause
