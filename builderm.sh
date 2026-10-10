#!/usr/bin/env bash
# builderm.sh - compila Doom Project en macOS (Intel y Apple Silicon).
#   ./builderm.sh            instala lo que falte con Homebrew, compila, arma ffmpeg y la musica
#   ./builderm.sh --no-deps  no instala nada (solo compila)
#   ./builderm.sh --no-music no genera la musica CTTS
#   ./builderm.sh --clean    borra lo compilado
# Necesita DOOM.WAD en esta carpeta (tu copia) para la musica.
set -e
cd "$(dirname "$0")"
DEPS=1; MUSIC=1
for a in "$@"; do case "$a" in --no-deps) DEPS=0;; --no-music) MUSIC=0;; --clean) make clean; rm -rf music ffmpeg; exit 0;; -h|--help) sed -n 2,7p "$0"; exit 0;; esac; done
say(){ printf '\033[1;32m[builderm]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[builderm]\033[0m %s\n' "$*"; }
have(){ command -v "$1" >/dev/null 2>&1; }
[ "$(uname -s)" = Darwin ] || { echo "builderm.sh es para macOS; en Linux usa ./builder.sh"; exit 1; }

# Homebrew (rutas de Apple Silicon e Intel)
for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do [ -x "$b" ] && eval "$($b shellenv)" && break; done
# Compilador: Xcode Command Line Tools
if ! have clang; then
  warn "Faltan las Command Line Tools de Xcode. Abriendo el instalador (acepta, y despues vuelve a correr ./builderm.sh)..."
  xcode-select --install || true; exit 1
fi
if [ $DEPS = 1 ]; then
  if ! have brew; then warn "No tienes Homebrew. Instalalo desde https://brew.sh y vuelve a correr ./builderm.sh (o instala SDL2 a mano y usa --no-deps)."; fi
  if have brew; then
    pk=(); have sdl2-config || pk+=(sdl2); have python3 || pk+=(python)
    have ffmpeg || [ -x ffmpeg/ffmpeg ] || pk+=(ffmpeg)
    [ ${#pk[@]} -gt 0 ] && { say "brew install ${pk[*]}"; brew install "${pk[@]}" || warn "brew no pudo instalar todo"; }
  fi
fi
have sdl2-config || { echo "falta SDL2: brew install sdl2"; exit 1; }

say "Compilando con clang ($(uname -m))..."
make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 2)" CC=clang
clang -O2 -o tools/ctts tools/ctts.c -lm
say "Listo: ./doom_game  ./mus2wav  tools/ctts"

# ffmpeg: si brew no lo dio, binario estatico para macOS
if ! have ffmpeg && [ ! -x ffmpeg/ffmpeg ]; then
  case "$(uname -m)" in arm64) F=ffmpeg-darwin-arm64;; *) F=ffmpeg-darwin-x64;; esac
  say "Bajando ffmpeg estatico ($F)..."; mkdir -p ffmpeg
  curl -fL -o ffmpeg/ffmpeg "https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/$F"
  chmod +x ffmpeg/ffmpeg; xattr -d com.apple.quarantine ffmpeg/ffmpeg 2>/dev/null || true
fi

WAD=""; for w in DOOM.WAD doom.wad Doom.wad DOOM2.WAD doom2.wad; do [ -f "$w" ] && { WAD="$w"; break; }; done
if [ $MUSIC = 1 ]; then
  if [ -z "$WAD" ]; then warn "No hay DOOM.WAD aqui: copialo (tu copia, o consigue uno: https://archive.org/download/doom-wads / Freedoom https://freedoom.github.io) y vuelve a correr ./builderm.sh (musica CTTS pendiente)."
  elif [ -f music/D_E1M1.wav ]; then say "music/ ya existe."
  elif have python3; then say "Generando la musica CTTS de 8 bits desde $WAD..."; PATH="$PWD/ffmpeg:$PATH" CC=clang python3 tools/build_music.py "$WAD" music
  else warn "sin python3 no hay musica CTTS (se usa el sintetizador de respaldo)"; fi
fi

cat > jugar.command <<'EOS'
#!/usr/bin/env bash
cd "$(dirname "$0")"
export PATH="$PWD/ffmpeg:$PATH"
WAD=DOOM.WAD; [ -f "$WAD" ] || WAD=$(ls *.WAD *.wad 2>/dev/null | head -1)
exec ./doom_game "$WAD" "$@"
EOS
chmod +x jugar.command
say "Version portable (USB): ./builderp.sh"
say "Para jugar:  ./jugar.command   (o doble clic).  Con video de la musica: ./jugar.command --video-music"
say "Si macOS bloquea el binario:  xattr -dr com.apple.quarantine .   (audio en macOS: mismo mezclador, salida SDL)"
