@echo off
rem Abre la app de control del ESP32 con doble clic.
rem pythonw = Python sin ventana de consola negra.

cd /d "%~dp0"

set PY=C:\Users\meola\AppData\Local\Python\pythoncore-3.14-64\pythonw.exe
if not exist "%PY%" set PY=pythonw

start "" "%PY%" app.py
