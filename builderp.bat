@echo off
rem builderp.bat - arma la version PORTABLE para Windows: portable\DoomProject-Windows-Portable\ y su .zip
rem Lleva dentro: doom_game.exe, SDL2.dll, ffmpeg\ffmpeg.exe, music\ (CTTS 8 bits), tu DOOM.WAD y jugar.bat. Se copia a una USB y corre sola.
rem Usa dist\windows\doom_game.exe si ya viene compilado; si no, llama a builder.bat (necesita MinGW-w64).
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set BIN=.
if not exist doom_game.exe (
  if exist dist\windows\doom_game.exe ( set "BIN=dist\windows" ) else (
    echo [builderp] No hay doom_game.exe: compilando con builder.bat...
    call builder.bat nomusic
    if errorlevel 1 exit /b 1
  )
)
set OUT=portable\DoomProject-Windows-Portable
rmdir /s /q "%OUT%" 2>nul
mkdir "%OUT%\ffmpeg"
copy /y "%BIN%\doom_game.exe" "%OUT%\" >nul
copy /y "%BIN%\SDL2.dll" "%OUT%\" >nul
if exist "%BIN%\mus2wav.exe" copy /y "%BIN%\mus2wav.exe" "%OUT%\" >nul
copy /y jugar.bat "%OUT%\" >nul
copy /y LICENSE "%OUT%\" >nul 2>nul
copy /y NOTICE "%OUT%\" >nul 2>nul

rem ffmpeg: el de la carpeta ffmpeg\, el del PATH, o se baja
if exist ffmpeg\ffmpeg.exe ( copy /y ffmpeg\ffmpeg.exe "%OUT%\ffmpeg\" >nul ) else (
  set FFP=
  for /f "delims=" %%i in ('where ffmpeg 2^>nul') do if not defined FFP set "FFP=%%i"
  if defined FFP ( copy /y "!FFP!" "%OUT%\ffmpeg\" >nul ) else (
    echo [builderp] Bajando ffmpeg estatico ^(unos 80 MB^)...
    powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/ffmpeg-win32-x64 -OutFile '%OUT%\ffmpeg\ffmpeg.exe'"
  )
)

rem DOOM.WAD y musica
set WAD=
for %%w in (DOOM.WAD doom.wad Doom.wad DOOM2.WAD doom2.wad) do if exist "%%w" if not defined WAD set WAD=%%w
if defined WAD ( copy /y "!WAD!" "%OUT%\DOOM.WAD" >nul ) else echo [builderp] No hay DOOM.WAD aqui: la carpeta portable necesitara el tuyo junto a doom_game.exe ^(https://archive.org/download/doom-wads / https://freedoom.github.io^).
if exist music\D_E1M1.wav (
  xcopy /e /i /q /y music "%OUT%\music" >nul
) else if defined WAD (
  call :findpy
  if not defined PY (
    echo [builderp] No hay Python 3: lo descargo de python.org y lo instalo solo para tu usuario...
    call :installpy
    call :findpy
  )
  if defined PY (
    if not exist tools\ctts.exe if exist dist\windows\ctts.exe copy /y dist\windows\ctts.exe tools\ctts.exe >nul
    echo [builderp] Generando la musica CTTS de 8 bits...
    set "PATH=%CD%\%OUT%\ffmpeg;%PATH%"
    !PY! tools\build_music.py "!WAD!" "%OUT%\music" >nul
  ) else echo [builderp] Sin Python 3 no hay musica CTTS ^(se usara el sintetizador de respaldo^).
)

echo Doom Project portable. Doble clic en jugar.bat ^(con --video-music se graba la musica en Video_Music\^).> "%OUT%\LEEME.txt"

rem empaquetar en .zip (PowerShell viene con Windows; si no, tar de Windows 10+)
del /q "portable\DoomProject-Windows-Portable.zip" 2>nul
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%' -DestinationPath 'portable\DoomProject-Windows-Portable.zip' -Force"
if errorlevel 1 tar -a -c -f "portable\DoomProject-Windows-Portable.zip" -C portable DoomProject-Windows-Portable
echo [builderp] Portable listo: portable\DoomProject-Windows-Portable.zip ^(carpeta: %OUT%\^)
exit /b 0
exit /b 0

rem ---- subrutinas de Python ----
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
echo [builderp] Descargando %PYURL%
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; (New-Object Net.WebClient).DownloadFile('%PYURL%','%PYDL%')" 2>nul
if not exist "%PYDL%" curl.exe -L -f -o "%PYDL%" "%PYURL%" 2>nul
if not exist "%PYDL%" ( echo [builderp] No pude descargar el instalador de Python. & exit /b 1 )
echo [builderp] Instalando Python ^(espera, puede tardar un par de minutos^)...
"%PYDL%" /quiet InstallAllUsers=0 PrependPath=1 Include_test=0 Include_doc=0 Include_launcher=1 Include_pip=0 Include_tcltk=0 Shortcuts=0
del /q "%PYDL%" >nul 2>nul
exit /b 0
