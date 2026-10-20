@echo off
cd /d "%~dp0temp_frontend"
if not exist node_modules npm install
if not exist .env copy .env.example .env
npm run dev
pause
