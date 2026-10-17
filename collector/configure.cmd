@echo off
cd /d "%~dp0"
if exist .venv\Scripts\python.exe (
  .venv\Scripts\python.exe -X utf8 collect.py configure
) else (
  python -X utf8 collect.py configure
)
pause
