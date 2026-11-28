@echo off
rem Najnowszy ReClaw z galezi claw-main-test (samo okno ReClaw), build w builds\reclaw-windows\main-test
rem (pobrane skrypty Windows oznacza jako obce: odblokowane raz, bez pytania przy kazdym uruchomieniu)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Filter *.ps1 | Unblock-File" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0reclaw-windows.ps1" -Galaz claw-main-test -Nazwa main-test %*
if errorlevel 1 pause
