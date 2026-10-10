@echo off
rem builder.bat - compila Doom Project en Windows (MinGW-w64 + SDL2).
rem   builder.bat            compila, baja SDL2 y ffmpeg si faltan, arma la musica CTTS
rem   builder.bat nomusic    no genera la musica (se usa el sintetizador de respaldo)
rem   builder.bat clean      borra lo compilado
rem Necesita: gcc de MinGW-w64 (si no lo tienes, el script intenta instalarlo con winget) y tu DOOM.WAD en esta carpeta.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set MUSIC=1
for %%a in (%*) do (
  if /i "%%a"=="nomusic" set MUSIC=0
  if /i "%%a"=="clean" ( rmdir /s /q obj_win 2>nul & del /q doom_game.exe mus2wav.exe tools\ctts.exe 2>nul & rmdir /s /q music 2>nul & echo limpio & exit /b 0 )
)

rem ---- 1. compilador gcc ----
where gcc >nul 2>nul
if errorlevel 1 (
  for %%d in ("C:\msys64\ucrt64\bin" "C:\msys64\mingw64\bin" "C:\mingw64\bin" "C:\TDM-GCC-64\bin" "C:\ProgramData\mingw64\mingw64\bin") do (
    if exist "%%~d\gcc.exe" set "PATH=%%~d;!PATH!"
  )
  for /d %%d in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do (
    if exist "%%d\mingw64\bin\gcc.exe" set "PATH=%%d\mingw64\bin;!PATH!"
  )
)
where gcc >nul 2>nul
if errorlevel 1 (
  echo [builder] No encuentro gcc ^(MinGW-w64^).
  where winget >nul 2>nul
  if not errorlevel 1 (
    echo [builder] Instalando WinLibs con winget...
    winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --accept-package-agreements --accept-source-agreements
    echo [builder] Cierra esta ventana, abre otra y vuelve a correr builder.bat.
  ) else (
    echo [builder] Instala MinGW-w64 ^(https://winlibs.com o MSYS2 https://www.msys2.org^) y vuelve a correr builder.bat.
  )
  exit /b 1
)
for /f "delims=" %%t in ('gcc -dumpmachine') do set TRIPLE=%%t
set SDLARCH=x86_64-w64-mingw32
echo !TRIPLE! | findstr /i "i686" >nul && set SDLARCH=i686-w64-mingw32
echo [builder] gcc: !TRIPLE!

rem ---- 2. SDL2 (paquete de desarrollo para MinGW) ----
set SDLVER=2.30.9
set SDL=%CD%\deps\SDL2-%SDLVER%\%SDLARCH%
if not exist "%SDL%\include\SDL2\SDL.h" (
  echo [builder] Bajando SDL2 %SDLVER% para MinGW...
  mkdir deps 2>nul
  powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri https://github.com/libsdl-org/SDL/releases/download/release-%SDLVER%/SDL2-devel-%SDLVER%-mingw.tar.gz -OutFile deps\sdl2.tar.gz"
  if errorlevel 1 goto fail
  tar -xzf deps\sdl2.tar.gz -C deps
  if errorlevel 1 goto fail
)

rem ---- 3. compilar ----
set CF=-std=gnu99 -fcommon -fpermissive -Wno-error -w -O2 -Iorig -Isrc -Iport -I"%SDL%\include" -I"%SDL%\include\SDL2"
mkdir obj_win 2>nul
set OBJS=
echo [builder] Compilando (hay unos 60 archivos; el codigo de 1993 da avisos que se silencian)...
for %%f in (orig\*.c) do (
  gcc %CF% -c "%%f" -o "obj_win\o_%%~nf.o"
  if errorlevel 1 goto fail
  set OBJS=!OBJS! obj_win\o_%%~nf.o
)
for %%f in (port\*.c) do (
  gcc %CF% -c "%%f" -o "obj_win\p_%%~nf.o"
  if errorlevel 1 goto fail
  set OBJS=!OBJS! obj_win\p_%%~nf.o
)
for %%f in (src\*.c) do (
  gcc %CF% -c "%%f" -o "obj_win\s_%%~nf.o"
  if errorlevel 1 goto fail
  set OBJS=!OBJS! obj_win\s_%%~nf.o
)
gcc -o doom_game.exe !OBJS! -L"%SDL%\lib" -lSDL2 -lm -static-libgcc
if errorlevel 1 goto fail
gcc %CF% -o mus2wav.exe tools_mus2wav.c port\chip.c src\wad.c -lm
if errorlevel 1 goto fail
gcc -O2 -o tools\ctts.exe tools\ctts.c -lm
if errorlevel 1 goto fail
copy /y "%SDL%\bin\SDL2.dll" . >nul
echo [builder] Listo: doom_game.exe  mus2wav.exe  tools\ctts.exe  SDL2.dll

rem ---- 4. ffmpeg (para --video-music) ----
where ffmpeg >nul 2>nul
if errorlevel 1 if not exist ffmpeg\ffmpeg.exe (
  echo [builder] ffmpeg no esta instalado: bajando el binario estatico para Windows ^(unos 80 MB^)...
  mkdir ffmpeg 2>nul
  powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/ffmpeg-win32-x64 -OutFile ffmpeg\ffmpeg.exe"
  if errorlevel 1 echo [builder] No pude bajar ffmpeg: --video-music no funcionara hasta que lo pongas en ffmpeg\ffmpeg.exe
)

rem ---- 5. musica CTTS ----
if "%MUSIC%"=="0" goto done
set WAD=
for %%w in (DOOM.WAD doom.wad Doom.wad DOOM2.WAD doom2.wad) do if exist "%%w" if not defined WAD set WAD=%%w
if not defined WAD ( echo [builder] No hay DOOM.WAD aqui: copialo ^(tu copia, o consigue uno: https://archive.org/download/doom-wads / Freedoom https://freedoom.github.io^) y vuelve a correr builder.bat para la musica CTTS. & goto done )
if exist music\D_E1M1.wav ( echo [builder] music\ ya existe. & goto done )
call :findpy
if not defined PY (
  echo [builder] No hay Python 3: lo descargo de python.org y lo instalo solo para tu usuario...
  call :installpy
  call :findpy
)
if not defined PY ( echo [builder] Sin Python 3 no hay musica CTTS ^(se usa el sintetizador de respaldo^). Instalalo desde python.org y repite. & goto done )
echo [builder] Generando la musica CTTS de 8 bits desde !WAD! ...
set "PATH=%CD%\ffmpeg;%PATH%"
!PY! tools\build_music.py "!WAD!" music
:done
echo.
echo [builder] Version portable ^(USB^): builderp.bat
echo [builder] Para jugar:  jugar.bat      ^(con video de la musica: jugar.bat --video-music^)
exit /b 0
:fail
echo [builder] Algo fallo ^(mira el mensaje de arriba^).
exit /b 1

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
echo [builder] Descargando %PYURL%
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; (New-Object Net.WebClient).DownloadFile('%PYURL%','%PYDL%')" 2>nul
if not exist "%PYDL%" curl.exe -L -f -o "%PYDL%" "%PYURL%" 2>nul
if not exist "%PYDL%" ( echo [builder] No pude descargar el instalador de Python. & exit /b 1 )
echo [builder] Instalando Python ^(espera, puede tardar un par de minutos^)...
"%PYDL%" /quiet InstallAllUsers=0 PrependPath=1 Include_test=0 Include_doc=0 Include_launcher=1 Include_pip=0 Include_tcltk=0 Shortcuts=0
del /q "%PYDL%" >nul 2>nul
exit /b 0
