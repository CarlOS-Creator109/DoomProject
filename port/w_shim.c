// w_shim.c - API de linuxdoom-1.10/w_wad.c sobre el cargador de WAD de este motor (wad.c).
// Misma semántica que el original: los lumps se copian a la zona con su tag (PU_*), de modo que
// el código original puede hacer Z_Free()/Z_ChangeTag() sobre ellos.
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "wad.h"
#include "doomdef.h"
#include "z_zone.h"
#include "i_system.h"
#include "w_wad.h"

lumpinfo_t *lumpinfo = NULL;
int numlumps = 0;
void **lumpcache = NULL;
static WAD *gw = NULL;

void W_Init(WAD *w){
    gw = w; numlumps = w->numlumps;
    lumpinfo = calloc(numlumps, sizeof(lumpinfo_t));
    lumpcache = calloc(numlumps, sizeof(void*));
    for(int i=0;i<numlumps;i++){
        memset(lumpinfo[i].name,0,8);
        for(int k=0;k<8 && w->lumps[i].name[k];k++) lumpinfo[i].name[k]=toupper((unsigned char)w->lumps[i].name[k]);
        lumpinfo[i].size = w->lumps[i].size; lumpinfo[i].position = w->lumps[i].filepos;
    }
}
void W_Reload(void){}
void W_InitMultipleFiles(char **filenames){ (void)filenames; }

int W_CheckNumForName(char *name){
    char n8[9]; memset(n8,0,9);
    for(int i=0;i<8 && name[i];i++) n8[i]=toupper((unsigned char)name[i]);
    for(int i=numlumps-1;i>=0;i--)            // como el original: busca desde el final (gana el último)
        if(strncmp(lumpinfo[i].name,n8,8)==0) return i;
    return -1;
}
int W_GetNumForName(char *name){
    int i=W_CheckNumForName(name);
    if(i==-1) I_Error("W_GetNumForName: %s not found!",name);
    return i;
}
int W_LumpLength(int lump){
    if(lump>=numlumps) I_Error("W_LumpLength: %i >= numlumps",lump);
    return lumpinfo[lump].size;
}
void W_ReadLump(int lump, void *dest){
    if(lump>=numlumps) I_Error("W_ReadLump: %i >= numlumps",lump);
    if(gw->lumps[lump].data && lumpinfo[lump].size) memcpy(dest,gw->lumps[lump].data,lumpinfo[lump].size);
}
void *W_CacheLumpNum(int lump, int tag){
    if(lump>=numlumps) I_Error("W_CacheLumpNum: %i >= numlumps",lump);
    if(!lumpcache[lump]){
        Z_Malloc(W_LumpLength(lump),tag,&lumpcache[lump]);
        W_ReadLump(lump,lumpcache[lump]);
    } else Z_ChangeTag(lumpcache[lump],tag);
    return lumpcache[lump];
}
void *W_CacheLumpName(char *name, int tag){ return W_CacheLumpNum(W_GetNumForName(name),tag); }
