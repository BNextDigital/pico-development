@echo off
setlocal
py -3 "%~dp0pico_sim.py" %*
if errorlevel 1 pause
