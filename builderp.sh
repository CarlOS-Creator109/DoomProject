#!/usr/bin/env bash
# builderp.sh - arma la version PORTABLE (carpeta autocontenida que se copia a una memoria USB y corre sola):
#   Linux : portable/DoomProject-Linux-Portable.tar.gz     macOS: portable/DoomProject-macOS-Portable.tar (sin gzip)
# Lleva dentro: doom_game, libSDL2, ffmpeg, music/ (CTTS 8 bits), tu DOOM.WAD y el lanzador jugar.sh / jugar.command.
#   ./builderp.sh            (compila antes con builder.sh / builderm.sh si todavia no hay binario)
set -e
cd "$(dirname "$0")"
say(){ printf '\033[1;32m[builderp]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[builderp]\033[0m %s\n' "$*"; }
have(){ command -v "$1" >/dev/null 2>&1; }
OS=$(uname -s)
case "$OS" in Darwin) NAME=macOS; BUILD=./builderm.sh; LAUNCH=jugar.command;; *) NAME=Linux; BUILD=./builder.sh; LAUNCH=jugar.sh;; esac

# 1. binario: el de aqui, el de dist/, o compilar
if [ ! -x doom_game ]; then
  if [ -x dist/linux/doom_game ] && [ "$NAME" = Linux ]; then BINDIR=dist/linux; else say "No hay binario: compilando con $BUILD..."; $BUILD --no-music; BINDIR=.; fi
else BINDIR=.; fi
[ -x "$BINDIR/doom_game" ] || { echo "no se pudo obtener doom_game"; exit 1; }

OUT="portable/DoomProject-$NAME-Portable"
rm -rf "$OUT"; mkdir -p "$OUT/lib" "$OUT/ffmpeg"
cp "$BINDIR/doom_game" "$OUT/"
[ -x "$BINDIR/mus2wav" ] && cp "$BINDIR/mus2wav" "$OUT/" || true

# 2. SDL2 junto al binario
if [ "$NAME" = Linux ]; then
  L=$(ldd "$OUT/doom_game" | awk '/libSDL2/{print $3}' | head -1)
else
  L=$(otool -L "$OUT/doom_game" | awk '/libSDL2/{print $1}' | head -1)
  case "$L" in @*|"") L="$(sdl2-config --prefix 2>/dev/null)/lib/libSDL2-2.0.0.dylib";; esac
fi
if [ -n "$L" ] && [ -f "$L" ]; then cp -L "$L" "$OUT/lib/"; say "SDL2 incluida: $(basename "$L")"; else warn "no encontre libSDL2 para incluir (el equipo destino la necesitara)"; fi

# 3. ffmpeg: preferir un binario estatico (100% portable); el del sistema solo como ultimo recurso
if [ -x ffmpeg/ffmpeg ]; then cp ffmpeg/ffmpeg "$OUT/ffmpeg/"
else
  case "$NAME-$(uname -m)" in Linux-x86_64) F=ffmpeg-linux-x64;; Linux-aarch64) F=ffmpeg-linux-arm64;; macOS-arm64) F=ffmpeg-darwin-arm64;; macOS-*) F=ffmpeg-darwin-x64;; *) F="";; esac
  if [ -n "$F" ] && { say "Bajando ffmpeg estatico ($F)..."; curl -fL -o "$OUT/ffmpeg/ffmpeg" "https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/$F"; }; then :
  elif have ffmpeg; then cp -L "$(command -v ffmpeg)" "$OUT/ffmpeg/ffmpeg"; warn "sin descarga: copie el ffmpeg del sistema (puede depender de librerias del equipo)"; fi
fi
chmod +x "$OUT/ffmpeg/ffmpeg" 2>/dev/null || true
[ "$NAME" = macOS ] && xattr -d com.apple.quarantine "$OUT/ffmpeg/ffmpeg" 2>/dev/null || true

# 4. musica y WAD
WAD=""; for w in DOOM.WAD doom.wad Doom.wad DOOM2.WAD doom2.wad; do [ -f "$w" ] && { WAD="$w"; break; }; done
[ -n "$WAD" ] && cp "$WAD" "$OUT/DOOM.WAD" || warn "no hay DOOM.WAD aqui: la carpeta portable necesitara el tuyo al lado de doom_game (https://archive.org/download/doom-wads / https://freedoom.github.io)"
if [ -f music/D_E1M1.wav ]; then cp -r music "$OUT/music"
elif [ -n "$WAD" ] && have python3; then
  [ -x tools/ctts ] || { have gcc && gcc -O2 -o tools/ctts tools/ctts.c -lm || clang -O2 -o tools/ctts tools/ctts.c -lm; }
  say "Generando la musica CTTS..."; PATH="$PWD/$OUT/ffmpeg:$PATH" python3 tools/build_music.py "$WAD" "$OUT/music" >/dev/null
else warn "sin musica CTTS (se usara el sintetizador de respaldo)"; fi

# 5. lanzador (libSDL2 y ffmpeg locales)
cat > "$OUT/$LAUNCH" <<'EOS'
#!/usr/bin/env bash
cd "$(dirname "$0")"
export PATH="$PWD/ffmpeg:$PATH" LD_LIBRARY_PATH="$PWD/lib:$LD_LIBRARY_PATH" DYLD_LIBRARY_PATH="$PWD/lib:$DYLD_LIBRARY_PATH"
WAD=DOOM.WAD; [ -f "$WAD" ] || WAD=$(ls *.WAD *.wad 2>/dev/null | head -1)
exec ./doom_game "$WAD" "$@"
EOS
chmod +x "$OUT/$LAUNCH" "$OUT/doom_game"
cp LICENSE NOTICE "$OUT/" 2>/dev/null || true
printf 'Doom Project portable. Ejecuta %s (con --video-music para grabar la musica en Video_Music/).\nSi macOS lo bloquea:  xattr -dr com.apple.quarantine .\n' "$LAUNCH" > "$OUT/LEEME.txt"

# 6. empaquetar
cd portable
if [ "$NAME" = macOS ]; then tar -cf "DoomProject-$NAME-Portable.tar" "DoomProject-$NAME-Portable"; A="DoomProject-$NAME-Portable.tar"
else tar -czf "DoomProject-$NAME-Portable.tar.gz" "DoomProject-$NAME-Portable"; A="DoomProject-$NAME-Portable.tar.gz"; fi
say "Portable listo: portable/$A  (carpeta: portable/DoomProject-$NAME-Portable/)"
