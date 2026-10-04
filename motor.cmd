@echo off
REM Prepara el proyecto y abre la web:  motor run
REM
REM Windows resuelve "motor" a este motor.cmd aunque exista la carpeta motor\,
REM asi que el comando se escribe tal cual. En Linux y macOS no se puede: un
REM archivo y una carpeta no pueden llamarse igual, y ahi el lanzador es ./bd2
REM
REM Acciones: run (por defecto), setup, test, bench, clean. Ver tools\motor.py
setlocal
where python >nul 2>nul
if errorlevel 1 (
  echo Error: no se encontro Python. Instala Python 3.10 o superior desde https://python.org 1>&2
  exit /b 1
)
python "%~dp0tools\motor.py" %*
