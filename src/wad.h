#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char name[9]; // 8 + \0
    int  filepos;
    int  size;
    uint8_t *data; // malloced
} Lump;

typedef struct {
    char magic[5]; // IWAD/PWAD
    int  numlumps;
    int  dir_ofs;
    Lump *lumps;
    int  lumps_cap;
    char path[512];
    char orig_path[512];
} WAD;

// carga WAD (auto-extrae si es ZIP con .wad dentro -> /tmp)
WAD *wad_load(const char *path);
void wad_free(WAD *wad);
int wad_merge(WAD *base, WAD *add); // agrega un PWAD (add se libera)
Lump *wad_get(WAD *wad, const char *name);
uint8_t *wad_get_data(WAD *wad, const char *name, int *out_size);

// mapas E1M1 / MAP01...
char **wad_find_maps(WAD *wad, int *out_count); // cada string malloced, lista malloced
void wad_find_maps_free(char **maps, int count);

// obtiene lumps del mapa (punteros directos a data del WAD, no copiar)
typedef struct {
    uint8_t *things; int things_len;
    uint8_t *linedefs; int linedefs_len;
    uint8_t *sidedefs; int sidedefs_len;
    uint8_t *vertexes; int vertexes_len;
    uint8_t *segs; int segs_len;
    uint8_t *ssectors; int ssectors_len;
    uint8_t *nodes; int nodes_len;
    uint8_t *sectors; int sectors_len;
    uint8_t *reject; int reject_len;
    uint8_t *blockmap; int blockmap_len;
} MapLumps;
int wad_read_map(WAD *wad, const char *mapname, MapLumps *out); // 0 ok, -1 error

// PLAYPAL 14*768
typedef struct { uint8_t r,g,b; } PalColor;
PalColor *wad_get_palette(WAD *wad, int palnum); // malloc 256, caller free
