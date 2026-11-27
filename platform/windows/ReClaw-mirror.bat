@echo off
rem ReClaw follows the keyboard while another window (e.g. the original CLAW.EXE) is focused.
rem Both games then get the same keys - for side-by-side recordings.
set RECLAW_MIRROR_KEYS=1
start "" "%~dp0ReClaw.exe"
