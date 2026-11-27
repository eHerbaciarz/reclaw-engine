@echo off
rem ReClaw follows the keyboard while another window (e.g. the original CLAW.EXE) is focused.
rem Both games then get the same keys - for side-by-side recordings.
rem 4:3 window showing 640x480 of the level, like CLAW.EXE.
set RECLAW_MIRROR_KEYS=1
set RECLAW_WINDOW=960x720@1.5
start "" "%~dp0ReClaw.exe"
