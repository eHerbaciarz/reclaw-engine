@echo off
rem ReClaw z galezi claw-experimental obok oryginalu (lustro klawiszy), build w builds\reclaw-windows\experimental
rem (pobrane skrypty Windows oznacza jako obce: odblokowane raz, bez pytania przy kazdym uruchomieniu)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Filter *.ps1 | Unblock-File" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0reclaw-windows.ps1" -Galaz claw-experimental -Nazwa experimental -Porownanie %*
if errorlevel 1 pause
