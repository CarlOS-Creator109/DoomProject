#!/usr/bin/env bash
# builder.sh - compila Doom Project en Linux (Arch, Debian/Ubuntu, Fedora, openSUSE, Alpine, Void).
#   ./builder.sh            instala dependencias que falten (pide sudo), compila, baja ffmpeg si no hay, arma la musica
#   ./builder.sh --no-deps  no toca el sistema (solo compila)
#   ./builder.sh --no-music no genera la musica CTTS (se usa el sintetizador de respaldo)
#   ./builder.sh --clean    borra lo compilado
# Necesita DOOM.WAD en esta carpeta (tu copia) para la musica.
set -e
cd "$(dirname "$0")"
DEPS=1; MUSIC=1
for a in "$@"; do case "$a" in --no-deps) DEPS=0;; --no-music) MUSIC=0;; --clean) make clean; rm -rf music ffmpeg; exit 0;; -h|--help) sed -n 2,7p "$0"; exit 0;; esac; done
say(){ printf '\033[1;32m[builder]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[builder]\033[0m %s\n' "$*"; }
have(){ command -v "$1" >/dev/null 2>&1; }

# ---- dependencias ----
need=()
have gcc || have cc || need+=(compilador)
have make || need+=(make)
have sdl2-config || need+=(sdl2)
have python3 || need+=(python3)
have ffmpeg || [ -x ffmpeg/ffmpeg ] || need+=(ffmpeg)
if [ ${#need[@]} -gt 0 ] && [ $DEPS = 1 ]; then
  say "Faltan: ${need[*]}"
  SUDO=""; [ "$(id -u)" != 0 ] && have sudo && SUDO=sudo
  if   have pacman;  then CMD="$SUDO pacman -S --needed --noconfirm base-devel sdl2 python ffmpeg"
  elif have apt-get; then CMD="$SUDO apt-get install -y build-essential libsdl2-dev python3 ffmpeg"
  elif have dnf;     then CMD="$SUDO dnf install -y gcc make SDL2-devel python3 ffmpeg-free"
  elif have zypper;  then CMD="$SUDO zypper install -y gcc make SDL2-devel python3 ffmpeg"
  elif have apk;     then CMD="$SUDO apk add build-base sdl2-dev python3 ffmpeg"
  elif have xbps-install; then CMD="$SUDO xbps-install -Sy base-devel SDL2-devel python3 ffmpeg"
  else CMD=""; fi
  if [ -n "$CMD" ]; then say "Ejecuto: $CMD"; $CMD || warn "no se pudo instalar todo; sigo con lo que haya"; else warn "No conozco tu gestor de paquetes: instala gcc, make, SDL2 (dev), python3 y ffmpeg a mano."; fi
fi
have make || { echo "falta make"; exit 1; }
have sdl2-config || { echo "falta SDL2 (paquete de desarrollo: libsdl2-dev / sdl2)"; exit 1; }

# ---- compilar ----
say "Compilando (el codigo de 1993 da avisos; se silencian)..."
CC="${CC:-$(command -v gcc || command -v cc)}"
make -j"$(nproc 2>/dev/null || echo 2)" CC="$CC" >/dev/null
"$CC" -O2 -o tools/ctts tools/ctts.c -lm
say "Listo: ./doom_game  ./mus2wav  tools/ctts"

# ---- ffmpeg (audio del sistema + --video-music) ----
if ! have ffmpeg && [ ! -x ffmpeg/ffmpeg ]; then
  case "$(uname -m)" in x86_64) F=ffmpeg-linux-x64;; aarch64|arm64) F=ffmpeg-linux-arm64;; armv7*) F=ffmpeg-linux-arm;; i?86) F=ffmpeg-linux-ia32;; *) F="";; esac
  if [ -n "$F" ]; then
    say "ffmpeg no esta instalado: bajando el binario estatico ($F)..."
    mkdir -p ffmpeg
    URL="https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/$F"
    if have curl; then curl -fL -o ffmpeg/ffmpeg "$URL"; elif have wget; then wget -O ffmpeg/ffmpeg "$URL"; else warn "ni curl ni wget"; fi
    chmod +x ffmpeg/ffmpeg 2>/dev/null || true
  fi
fi

# ---- musica ----
WAD=""; for w in DOOM.WAD doom.wad Doom.wad DOOM2.WAD doom2.wad; do [ -f "$w" ] && { WAD="$w"; break; }; done
if [ $MUSIC = 1 ]; then
  if [ -z "$WAD" ]; then warn "No hay DOOM.WAD aqui: copialo a esta carpeta (tu copia, o consigue uno: https://archive.org/download/doom-wads / Freedoom https://freedoom.github.io) y vuelve a correr ./builder.sh (musica CTTS pendiente)."
  elif [ -d music ] && [ -f music/D_E1M1.wav ]; then say "music/ ya existe."
  elif have python3; then say "Generando la musica CTTS de 8 bits desde $WAD (unos segundos)..."; PATH="$PWD/ffmpeg:$PATH" python3 tools/build_music.py "$WAD" music
  else warn "sin python3 no hay musica CTTS (se usara el sintetizador de respaldo)"; fi
fi

# ---- lanzador ----
cat > jugar.sh <<'EOS'
#!/usr/bin/env bash
cd "$(dirname "$0")"
export PATH="$PWD/ffmpeg:$PATH"
WAD=DOOM.WAD; [ -f "$WAD" ] || WAD=$(ls *.WAD *.wad 2>/dev/null | head -1)
exec ./doom_game "$WAD" "$@"
EOS
chmod +x jugar.sh
say "Version portable (USB): ./builderp.sh"
say "Para jugar:  ./jugar.sh        (con video de la musica: ./jugar.sh --video-music)"
