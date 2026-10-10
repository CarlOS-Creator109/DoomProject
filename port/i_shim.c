// i_shim.c - servicios de sistema (i_system.c) y sonido/música sin implementar aún (i_sound.c)
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "doomdef.h"
#include "d_ticcmd.h"
#include "i_system.h"
#include "m_argv.h"
int myargc = 0; char **myargv = NULL;
int M_CheckParm(char *check){ for(int i=1;i<myargc;i++) if(strcasecmp(check,myargv[i])==0) return i; return 0; }
byte *I_ZoneBase(int *size){ *size = 32*1024*1024; return (byte*)malloc(*size); }
int I_GetTime(void){ return (int)((unsigned long long)SDL_GetTicks()*TICRATE/1000); }
ticcmd_t *I_BaseTiccmd(void){ static ticcmd_t emptycmd; return &emptycmd; }
void I_Tactile(int on,int off,int total){ (void)on;(void)off;(void)total; }
void I_Error(char *error, ...){
    va_list ap; va_start(ap,error); fprintf(stderr,"Error: "); vfprintf(stderr,error,ap); fprintf(stderr,"\n"); va_end(ap);
    fflush(stderr); exit(1);
}
