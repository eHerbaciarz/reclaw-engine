@echo off
rem Pobiera najnowszy testowy ReClaw, podpina dane gry z oryginalu
rem i uruchamia oba okna obok siebie (szczegoly w porownanie.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0porownanie.ps1" %*
if errorlevel 1 pause
