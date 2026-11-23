@echo off
setlocal
set "MAKER_ROOT=%~dp0..\.."
if exist "%MAKER_ROOT%\runtime\portable-manifest.json" if not exist "%MAKER_ROOT%\runtime\python\python.exe" (
  echo Portable runtime missing. Extract the complete builder package.
  exit /b 1
)
if exist "%MAKER_ROOT%\runtime\python\python.exe" (
  set "PYTHONHOME="
  set "PYTHONNOUSERSITE=1"
  set "NODE_PATH="
  set "NODE_OPTIONS="
  set "PYTHONPATH=%MAKER_ROOT%\tools\rom-maker\offline"
  "%MAKER_ROOT%\runtime\python\python.exe" "%~dp0maker.py" %*
) else if exist "%MAKER_ROOT%\.venv\Scripts\python.exe" (
  "%MAKER_ROOT%\.venv\Scripts\python.exe" "%~dp0maker.py" %*
) else (
  python "%~dp0maker.py" %*
)
set "MAKER_RESULT=%ERRORLEVEL%"
if not "%ROM_MAKER_NO_PAUSE%"=="1" pause
exit /b %MAKER_RESULT%
