@echo off
setlocal
py -3 "%~dp0tests\test_backend.py"
if errorlevel 1 pause
