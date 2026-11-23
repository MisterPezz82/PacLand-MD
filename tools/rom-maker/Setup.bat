@echo off
setlocal
cd /d "%~dp0..\.."
echo ============================================================
echo  Pac-Land MD ROM maker - one-time setup
echo ============================================================
echo.
if exist "runtime\portable-manifest.json" if exist "runtime\python\python.exe" (
  echo Setup has already been done. Checking it still works...
  goto check
)
echo This downloads the build tools into this folder: Python, Node.js,
echo the Zig compiler and some libraries, about 500 MB in total.
echo Nothing is installed on the rest of your computer and you do not
echo need administrator rights. Every download is checked against a
echo fixed checksum. This can take several minutes.
echo.
if not exist build mkdir build
if not exist build\bootstrap mkdir build\bootstrap
echo [1/3] Downloading Python...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;" ^
  "$p=(Get-Content -Raw 'tools\rom-maker\portable-runtimes.json' | ConvertFrom-Json).platforms.'windows-x64'.python;" ^
  "$f='build\bootstrap\python.tar.gz'; $ProgressPreference='SilentlyContinue';" ^
  "Invoke-WebRequest -UseBasicParsing -Uri $p.url -OutFile $f;" ^
  "if ((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $p.sha256) { throw 'Python download failed its checksum. Try again.' }"
if errorlevel 1 goto fail
tar -xzf build\bootstrap\python.tar.gz -C build\bootstrap
if errorlevel 1 goto fail
echo [2/3] Downloading Node.js, Zig and libraries, then the build packages...
set "NODE_OPTIONS="
set "NODE_PATH="
set "PYTHONHOME="
set "PYTHONPATH="
build\bootstrap\python\python.exe tools\rom-maker\portable.py --platform windows-x64 --root .
if errorlevel 1 goto fail
rmdir /s /q build\bootstrap 2>nul
rmdir /s /q build\portable-cache 2>nul
:check
echo [3/3] Checking the tools...
set "PYTHONHOME="
set "PYTHONNOUSERSITE=1"
set "NODE_PATH="
set "NODE_OPTIONS="
set "PYTHONPATH=%CD%\tools\rom-maker\offline"
runtime\python\python.exe tools\rom-maker\maker.py --check >nul
if errorlevel 1 goto fail
echo.
echo Setup finished. Now drag your pacland.zip (or the folder it is in)
echo onto Make-ROM.bat.
if not "%ROM_MAKER_NO_PAUSE%"=="1" pause
exit /b 0
:fail
echo.
echo Setup did not finish. The most common cause is no internet connection
echo or a security program blocking the download. Run Setup.bat again; it
echo carries on from where it stopped. See docs\rom-maker.md for help.
if not "%ROM_MAKER_NO_PAUSE%"=="1" pause
exit /b 1
