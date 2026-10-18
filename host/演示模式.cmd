@echo off
cd /d "%~dp0"
python io4_hid_gui.py --demo
if errorlevel 1 pause
