# Doom Project 1.0.2 (híbrido: código de juego original + render propio)

**Novedades 1.0.2:** soporte de PWAD (`./doom_game DOOM.WAD -file BUNKER.WAD`) y `BUNKER.WAD`, un nivel propio para Doom 1 con enemigos que cambian según la dificultad; `jugar bunker` / `./Doom_Project-….AppImage bunker` lo cargan directo.
**Novedades 1.0.1:** Windows 32 bits; AppImage de 32 y 64 bits que instala lo que falte (python3, ffmpeg) con el gestor de paquetes del sistema (apt, dnf, pacman, zypper, apk, xbps...); `jugar.bat`/`builder.bat`/`builderp.bat` descargan e instalan Python solos si no lo hay; ya no hace falta `mido`.

Motor en C/SDL2 que **usa el código original de id Software (linuxdoom-1.10, GPL v2)** para toda la
lógica de juego y dibuja con un renderizador propio (columnas + BSP, luz de Doom, sprites, texturas con transparencia).

## Qué es original (carpeta `orig/`, sin cambios salvo 2 parches de 64 bits)
- Simulación: `p_*.c` (física/colisión con blockmap, armas `p_pspr`, daño/ítems `p_inter`, IA de monstruos `p_enemy`,
  puertas/lifts/pisos/luces/interruptores/teletransportes `p_doors p_floor p_plats p_lights p_switch p_telept p_spec`), `info.c` (tablas de estados), `m_random`, `tables`, `z_zone`.
- Interfaz: barra de estado `st_stuff/st_lib`, HUD `hu_stuff/hu_lib`, automapa `am_map`, trucos `m_cheat`, `v_video`, sonido `s_sound`.
- Flujo de juego completo: `g_game.c` (G_Ticker, nuevo juego, nivel completado, guardar/cargar), `m_menu.c` (menú principal, opciones, partidas, ayuda), `wi_stuff.c` (intermisión), `f_finale.c` (final y reparto), `p_saveg.c`.
## Qué es nuevo (`port/`, `src/`)
- `render.c`: renderizador (rayo por columna + BSP + clipping de portales + sprites + arma + texturas intermedias).
- `game_main.c`: SDL, bucle a 35 tics/s con interpolación de cámara, cambio de nivel (salidas normal/secreta), reaparición.
- `snd_sdl.c`: mezclador de 8 canales para los efectos DMX del WAD.
- `chip.c`: música MUS de Doom con sintetizador de 8 bits (osciladores, ruido LFSR de la NES, ADSR y batería tomados de CTTS/GemTunes). Herramienta `mus2wav`: `./mus2wav DOOM.WAD D_E1M1 e1m1.wav 60`.
- `w_shim.c`, `r_shim.c`, `video_shim.c`...: pegamento entre el código original y `wad.c/tex.c` propios.

## Música y audio (cadena CTTS)
    make music WAD=DOOM.WAD      # MUS -> MIDI (tools/mus2mid.py) -> .rbsong (tools/midi2rbsong.py) -> WAV (motor CTTS en C, tools/ctts.c)
Genera `music/D_*.wav`; el juego carga solo la pieza del nivel y la repite. Sin esa carpeta usa el sintetizador 8 bits interno (`chip.c`).
Requiere `python3` y `gcc` (no hace falta `mido`: `midi2rbsong.py` trae su propio lector MIDI; si `mido` está instalado lo usa). En Windows, `jugar.bat` instala Python 3 por ti si falta.
El audio sale así: música + efectos del WAD (voces, armas, puertas) se mezclan en un solo flujo PCM y el driver (`snd_driver.c`)
lo manda a `ffmpeg`, que lo entrega a PulseAudio/PipeWire (o ALSA). Sin ffmpeg usa SDL.
Variables: `DOOM_AUDIO=ffmpeg|sdl|none`, `DOOM_FFMPEG_OUT="-f alsa default"` (salida a medida; `-f wav x.wav` graba), `DOOM_SYNTH=1` (forzar sintetizador interno), `DOOM_MUSIC_DIR`.
**Video de la música:** `./doom_game DOOM.WAD --video-music [carpeta]` graba mientras juegas un video 1280x720 a 30 fps
(osciloscopio + espectro de la música que suena, con el nombre de la pieza y el tiempo, y en la esquina superior derecha un recuadro con la ventana del juego) con su audio, en `./Video_Music/doom_music_FECHA.mp4`
(H.264 + AAC por ffmpeg, mp4 fragmentado: aunque cierres a la fuerza el archivo se puede ver). Solo la música, sin efectos.
Único cambio en el código original para esto: 1 línea en `s_sound.c` que avisa el nombre de la pieza.

## Compilar y jugar (un script por sistema)
| Sistema | Script | Qué hace |
|---|---|---|
| Linux | `./builder.sh` | instala dependencias con tu gestor (pacman/apt/dnf/zypper/apk/xbps), compila, baja ffmpeg si no hay, genera la música |
| Windows | `builder.bat` | usa MinGW-w64 (si no hay gcc intenta `winget`), baja SDL2 y ffmpeg, instala Python si falta, compila, genera la música |
| macOS | `./builderm.sh` | usa Xcode CLT + Homebrew (SDL2, ffmpeg), compila con clang, genera la música |
Opciones: `--no-deps` / `--no-music` / `--clean` (Windows: `nomusic`, `clean`). Todos esperan tu `DOOM.WAD` en la misma carpeta y dejan un lanzador
(`jugar.sh`, `jugar.bat`, `jugar.command`) que pone el ffmpeg local en el PATH.
**AppImage (Linux 64 y 32 bits):** `Doom_Project-1.0.1-x86_64.AppImage` / `-i686.AppImage`. Pon tu `DOOM.WAD` al lado y ejecútalo; si faltan `python3` o `ffmpeg` los instala con el gestor de tu distro (pide permiso; `DOOM_NOPKG=1` lo desactiva, `DOOM_YES=1` no pregunta). Si no se puede, baja un ffmpeg estático.
Binarios ya compilados: `dist/linux/` (x86-64, glibc ≥ 2.39, necesita libSDL2) y `dist/windows/` (x86-64, trae SDL2.dll; la versión de 32 bits va en su propio .zip). macOS se compila en el Mac (`builderm.sh`).
Audio: en Linux todo (música + efectos) sale por el driver a FFmpeg -> PulseAudio/PipeWire/ALSA; en Windows y macOS FFmpeg no tiene salida directa
a la tarjeta de sonido, así que el MISMO mezclador sale por SDL (FFmpeg se usa igual para `--video-music`; en Windows el audio se une al video al cerrar el juego).
Manual: `make` (Linux/macOS) y `make music WAD=DOOM.WAD`.

Menú original: Nueva partida (episodio/dificultad), Opciones, Cargar/Guardar (F2/F3, 6 ranuras `doomsav0-5.dsg` en el directorio actual),
Ayuda, Salir (F10), sonido (F4), mensajes (F8), gamma (F11).
Controles: WASD / flechas, ratón (girar y disparar), Ctrl o clic = fuego, Espacio o E = usar, Shift = correr, 1-7 = armas,
Tab = automapa, **Pausa** = pausa, Esc = menú. Trucos originales: iddqd, idkfa, idfa, idclip, idclev##, idbehold?, iddt, idmypos.
Al terminar un nivel aparece la intermisión original (bajas/objetos/secretos/tiempo) y al acabar el episodio el final.

## Pruebas sin ventana
    ./doom_game DOOM.WAD --map E1M1 --cheat idkfa --seq "wS:120,f:5,r:20,wSf:60" --shot out.bmp
    ./doom_game DOOM.WAD --shot titulo.bmp --tics 5                      # pantalla de título
    ./doom_game DOOM.WAD --map E1M1 --post esc,down,enter --shot m.bmp   # pulsa teclas (esc enter up down left right space y n f1-f10 o un caracter)
    ./doom_game DOOM.WAD --map E1M1 --exit --tics 150 --shot wi.bmp      # intermisión (--secret = salida secreta)
    ./doom_game DOOM.WAD --map E1M1 --save 2  /  --map E1M3 --load 2 --tics 10
(`--seq` = lista `teclas:tics`; teclas: w s a d l r f e S 1-7).

## Pendiente
Demos grabados, tamaño de ventana 3D desde el menú, juego en red.
## Conseguir un WAD
- **Freedoom** (libre, legal): https://freedoom.github.io
- **Doom shareware** (episodio 1, redistribuible): `Doom (v1.9) (Demo)` en la colección de archive.org
- **Colección de archive.org** (preservación de WADs de Doom y derivados): https://archive.org/download/doom-wads — los `.zip` traen el `.wad` adentro; descomprímelo y llámalo `DOOM.WAD` (o pásalo como argumento). Los WAD comerciales (Doom, Doom II, Final Doom) siguen teniendo copyright de id/ZeniMax: úsalos solo si tienes derecho a ello; este proyecto no los distribuye.

Licencia: el código propio (`port/`, `src/`, `tools/`, scripts) es **GPL v3** (ver `LICENSE`); `orig/` es el código de linuxdoom-1.10 de id Software bajo su GPL (ver `NOTICE`, incluye una advertencia sobre compatibilidad v2/v3). Necesitas tu propio `DOOM.WAD`.

Versión portable: `./builderp.sh` (Linux/macOS) o `builderp.bat` (Windows) arman una carpeta autocontenida (binario + SDL2 + ffmpeg + música + WAD) y su archivo: `.zip` en Windows, `.tar` (sin gzip) en macOS, `.tar.gz` en Linux.
