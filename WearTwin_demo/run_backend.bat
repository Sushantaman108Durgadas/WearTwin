@echo off
cd /d "%~dp0temp_backend"
if not exist .venv py -m venv .venv
call .venv\Scripts\activate.bat
python -m pip install -r requirements.txt
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
pause
