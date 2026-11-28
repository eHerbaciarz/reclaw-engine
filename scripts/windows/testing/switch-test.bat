@echo off
rem ReClaw Switch (galaz claw-switch): pobierz build testowy, uruchom w Ryujinxie, logi do logs\claw-switch
rem (pobrane skrypty Windows oznacza jako obce: odblokowane raz, bez pytania przy kazdym uruchomieniu)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Filter *.ps1 | Unblock-File" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_switch.ps1" %*
if errorlevel 1 pause
