@echo off
rem ReClaw Dreamcast (galaz claw-dreamcast): pobierz build testowy, zrob obraz plyty, uruchom we Flycascie, logi do logs\claw-dreamcast
rem (pobrane skrypty Windows oznacza jako obce: odblokowane raz, bez pytania przy kazdym uruchomieniu)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Filter *.ps1 | Unblock-File" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_dreamcast.ps1" %*
if errorlevel 1 pause
