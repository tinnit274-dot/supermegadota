@echo off
setlocal
set "ROOT=%~dp0"
set "PATH=C:\msys64\ucrt64\bin;%ROOT%build;%PATH%"
if exist "%ROOT%venv\Scripts\python.exe" (
  "%ROOT%venv\Scripts\python.exe" "%ROOT%python\setup_gsi.py"
)
start "" "%ROOT%build\DotaBot.exe"
endlocal
