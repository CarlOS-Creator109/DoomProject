# Doom Project: codigo de juego ORIGINAL de id Software (linuxdoom-1.10, GPL v2) + render/SDL propios
# Linux/macOS:  make          Windows (MinGW):  ver builder.bat (o  make CC=x86_64-w64-mingw32-gcc EXE=.exe SDL_CFLAGS=... SDL_LIBS=...)
CC      ?= gcc
EXE     ?=
OBJDIR  ?= obj
SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS   ?= $(shell sdl2-config --libs)
# -fpermissive solo existe en GCC; con clang (macOS) se relajan los errores del C de 1993
ifneq (,$(findstring clang,$(shell $(CC) --version 2>/dev/null)))
  RELAX := -Wno-error -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-deprecated-non-prototype -Wno-return-type
else
  RELAX := -fpermissive -Wno-error
endif
CFLAGS  := -std=gnu99 -fcommon $(RELAX) -w -O2 -Iorig -Isrc -Iport $(SDL_CFLAGS) $(EXTRA_CFLAGS)
LIBS    := $(SDL_LIBS) -lm $(EXTRA_LIBS)
ORIG    := $(wildcard orig/*.c)
PORT    := $(wildcard port/*.c)
SRC     := src/wad.c src/tex.c src/map.c
OBJ     := $(ORIG:orig/%.c=$(OBJDIR)/o_%.o) $(PORT:port/%.c=$(OBJDIR)/p_%.o) $(SRC:src/%.c=$(OBJDIR)/s_%.o)
all: doom_game$(EXE) mus2wav$(EXE)
mus2wav$(EXE): tools_mus2wav.c port/chip.c src/wad.c
	$(CC) $(CFLAGS) -o $@ $^ -lm $(EXTRA_LIBS)
doom_game$(EXE): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LIBS)
$(OBJDIR)/o_%.o: orig/%.c ; @mkdir -p $(OBJDIR); $(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/p_%.o: port/%.c ; @mkdir -p $(OBJDIR); $(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/s_%.o: src/%.c  ; @mkdir -p $(OBJDIR); $(CC) $(CFLAGS) -c $< -o $@
clean: ; rm -rf obj obj_win doom_game doom_game.exe mus2wav mus2wav.exe

# musica: MUS -> MIDI -> .rbsong -> WAV (motor CTTS en C).  Uso: make music WAD=DOOM.WAD   (necesita python3 y gcc)
WAD ?= DOOM.WAD
music: ; python3 tools/build_music.py $(WAD) music
