// r_shim.c - lo mínimo de r_data.c / r_sky.c / r_things.c que el código de juego necesita:
// números de textura/flat (sobre tex.c), tablas de traducción (animaciones) y alturas.
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "tex.h"
#include "doomdef.h"
#include "i_system.h"
#include "w_wad.h"
#include "r_data.h"
#include "r_state.h"
#include "r_sky.h"
#include "r_things.h"
#include "tables.h"
int skyflatnum, skytexture, skytexturemid;
int numtextures_r = 0;
char **sprite_namelist = NULL;

int R_CheckTextureNumForName(char *name){
    if(name[0]=='-') return 0;
    for(int i=0;i<num_textures;i++) if(strncasecmp(textures[i].name,name,8)==0) return i;
    return -1;
}
int R_TextureNumForName(char *name){
    int i=R_CheckTextureNumForName(name);
    if(i==-1) I_Error("R_TextureNumForName: %s not found",name);
    return i;
}
int R_FlatNumForName(char *name){
    int i=W_CheckNumForName(name);
    if(i==-1){ char n8[9]; strncpy(n8,name,8); n8[8]=0; I_Error("R_FlatNumForName: %s not found",n8); }
    return i-firstflat;
}
// equivalente a R_InitData (solo lo que usa el juego); llamar después de tex_init()
void R_InitDataShim(void){
    firstflat = W_GetNumForName("F_START")+1;
    textureheight = malloc((num_textures+1)*sizeof(fixed_t));
    texturetranslation = malloc((num_textures+1)*sizeof(int));
    for(int i=0;i<num_textures;i++){ textureheight[i]=textures[i].height<<FRACBITS; texturetranslation[i]=i; }
    flattranslation = malloc((num_flats+1)*sizeof(int));
    for(int i=0;i<num_flats;i++) flattranslation[i]=i;
    skyflatnum = R_FlatNumForName("F_SKY1");
    skytexture = R_TextureNumForName("SKY1");
}
void R_PrecacheLevel(void){}
fixed_t *finecosine = &finesine[FINEANGLES/4];
// variables de vista de r_main.c (las usan R_PointToAngle* y otros)
fixed_t viewx, viewy, viewz; angle_t viewangle; int extralight_unused;
