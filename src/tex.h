#pragma once
#include "wad.h"
#include <SDL2/SDL.h>
#include <stdint.h>

extern uint8_t doom_palette[256][3]; // RGB 0-255

typedef struct {
    char name[9];
    int width, height;
    uint8_t *pixels; // width*height, 255 = transparent (for masked)
} Texture;

typedef struct {
    char name[9];
    uint8_t data[4096]; // 64x64
} Flat;

// init cargando PLAYPAL, PNAMES, TEXTURE1/2, flats, etc desde WAD
int tex_init(WAD *wad);
void tex_free(void);

// lookup
Texture *tex_find(const char *name); // NULL si "-"/no encontrado
Flat    *flat_find(const char *name);
int tex_num_for_name(const char *name); // -1 si no

// sampling
// devuelve índice paleta 0-255, 255 transparente (no dibujar)
int tex_get_pixel(Texture *t, int x, int y);
// flats son 64x64 wrap
int flat_get_pixel(Flat *f, int x, int y);

// patch (para HUD) : decodifica patch_t cola y dibuja en renderer
// patch_data es el lump crudo de un patch (ej STBAR)
void patch_draw(SDL_Renderer *ren, uint8_t *patch_data, int lump_size, int x, int y, int scale); // scale 1 o 2
void patch_draw_to_texture(SDL_Texture *tex, uint8_t *patch_data, int lump_size); // opcional


// helpers exposición
extern Texture *textures;
extern int num_textures;
extern Flat *flats;
extern int num_flats;
