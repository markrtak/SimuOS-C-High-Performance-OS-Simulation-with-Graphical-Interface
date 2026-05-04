@echo off
cd /d "%~dp0.."
python gui\os_simulator_gui.py
if errorlevel 1 pause
