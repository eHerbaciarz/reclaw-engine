@echo off
rem ReClaw PS2 (galaz claw-ps2): pobierz build testowy, uruchom w PCSX2, logi do logs\claw-ps2
rem (pobrane skrypty Windows oznacza jako obce: odblokowane raz, bez pytania przy kazdym uruchomieniu)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Filter *.ps1 | Unblock-File" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_ps2.ps1" %*
if errorlevel 1 pause
