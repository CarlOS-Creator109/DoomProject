Armado de los AppImage (1.0.1): compila `doom_game` (64 bits: `make`; 32 bits: `make CC="gcc -m32" SDL_LIBS=-lSDL2`),
crea `AppDir/usr/bin/doom_game`, `AppDir/usr/lib/libSDL2-2.0.so.0`, `AppDir/usr/share/doomproject/{LICENSE,NOTICE,tools/}` (con `ctts` compilado para la misma arquitectura),
copia `AppRun`, `doomproject.desktop` y `doomproject.svg` a `AppDir/` y ejecuta `ARCH=x86_64 appimagetool AppDir Doom_Project-x86_64.AppImage` (o `ARCH=i686`).
