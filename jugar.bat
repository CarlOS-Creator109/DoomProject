@echo off
setlocal enableextensions
cd /d "%~dp0"
set "PATH=%CD%\ffmpeg;%CD%\..\..\ffmpeg;%PATH%"

rem ---- DOOM.WAD ----
set "WAD=DOOM.WAD"
if not exist "%WAD%" for %%f in (*.wad) do set "WAD=%%f"

rem ---- musica CTTS: si falta, hace falta Python 3 (se instala solo si no esta) ----
if exist "music\D_E1M1.wav" goto run
if not exist "%WAD%" goto run
if "%DOOM_NOPYTHON%"=="1" goto run
set "TOOLS="
if exist "tools\build_music.py" set "TOOLS=%CD%\tools"
if not defined TOOLS if exist "..\..\tools\build_music.py" set "TOOLS=%CD%\..\..\tools"
if not defined TOOLS goto run
if not exist "%TOOLS%\ctts.exe" if exist "ctts.exe" copy /y "ctts.exe" "%TOOLS%\ctts.exe" >nul

call :findpy
if defined PY goto genmusic

echo.
echo [Doom Project] No encuentro Python 3, que se necesita UNA vez para armar la musica de 8 bits.
echo [Doom Project] Lo voy a descargar de python.org e instalar solo para tu usuario ^(sin permisos de administrador^).
call :installpy
call :findpy
if not defined PY (
  echo [Doom Project] No se pudo instalar Python. El juego arranca con el sintetizador de respaldo.
  echo [Doom Project] Si quieres la musica CTTS instala Python 3 desde https://www.python.org/downloads/ y abre jugar.bat otra vez.
  goto run
)

:genmusic
echo [Doom Project] Generando la musica CTTS de 8 bits ^(unos segundos, solo la primera vez^)...
%PY% "%TOOLS%\build_music.py" "%WAD%" music

:run
doom_game.exe "%WAD%" %*
endlocal
exit /b %ERRORLEVEL%

rem ------------------------------------------------------------
:findpy
set "PY="
python -c "import sys;sys.exit(0 if sys.version_info[0]==3 else 1)" >nul 2>nul && set "PY=python"
if defined PY exit /b 0
py -3 -c "import sys" >nul 2>nul && set "PY=py -3"
if defined PY exit /b 0
for %%v in (313 312 311 310 39 38 313-32 312-32 311-32 310-32 39-32 38-32) do (
  if not defined PY if exist "%LocalAppData%\Programs\Python\Python%%v\python.exe" set PY="%LocalAppData%\Programs\Python\Python%%v\python.exe"
)
if defined PY exit /b 0
if exist "%CD%\python\python.exe" set PY="%CD%\python\python.exe"
exit /b 0

:installpy
rem Windows 7/8 -> Python 3.8.10 ; Windows 10/11 -> Python 3.12.7. 32 o 64 bits segun el sistema.
set "PYV=3.12.7"
ver | findstr /r /c:"Version 6\.[0-3]\." >nul && set "PYV=3.8.10"
set "ARCH=win32"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "ARCH=amd64"
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "ARCH=amd64"
if "%ARCH%"=="amd64" ( set "PYEXE=python-%PYV%-amd64.exe" ) else ( set "PYEXE=python-%PYV%.exe" )
set "PYURL=https://www.python.org/ftp/python/%PYV%/%PYEXE%"
set "PYDL=%TEMP%\%PYEXE%"
echo [Doom Project] Descargando %PYURL%
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; (New-Object Net.WebClient).DownloadFile('%PYURL%','%PYDL%')" 2>nul
if not exist "%PYDL%" curl.exe -L -f -o "%PYDL%" "%PYURL%" 2>nul
if not exist "%PYDL%" ( echo [Doom Project] No pude descargar el instalador de Python. & exit /b 1 )
echo [Doom Project] Instalando Python ^(espera, puede tardar un par de minutos^)...
"%PYDL%" /quiet InstallAllUsers=0 PrependPath=1 Include_test=0 Include_doc=0 Include_launcher=1 Include_pip=0 Include_tcltk=0 Shortcuts=0
del /q "%PYDL%" >nul 2>nul
exit /b 0
