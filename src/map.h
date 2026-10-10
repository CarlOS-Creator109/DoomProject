#pragma once
#include <stdint.h>

#pragma pack(push,1)
typedef struct { int16_t x, y; } RawVertex;
typedef struct { uint16_t v1, v2; uint16_t flags; uint16_t special; uint16_t tag; int16_t front; int16_t back; } RawLinedef;
typedef struct { int16_t xofs, yofs; char upper[8]; char lower[8]; char middle[8]; int16_t sector; } RawSidedef;
typedef struct { int16_t floorh, ceilh; char floorpic[8]; char ceilpic[8]; int16_t light; int16_t special; int16_t tag; } RawSector;
typedef struct { int16_t x, y; int16_t angle; uint16_t type; uint16_t flags; } RawThing;
typedef struct { int16_t v1, v2; int16_t angle; uint16_t linedef; int16_t side; uint16_t offset; } RawSeg;
typedef struct { uint16_t numsegs; uint16_t firstseg; } RawSSector;
typedef struct { int16_t x, y, dx, dy; int16_t bbox[8]; uint16_t children[2]; } RawNode;
#pragma pack(pop)

// helpers
void tex_name(char out[9], const char in[8]);
void pic_name(char out[9], const char in[8]);
uint32_t hash_str(const char *s); // djb2
