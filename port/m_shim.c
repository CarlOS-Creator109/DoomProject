// m_shim.c - lo de m_misc.c / d_main.c / d_net.c / i_system.c que usan g_game.c, m_menu.c, f_finale.c y wi_stuff.c.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_net.h"
#include "d_event.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "m_misc.h"
#include "r_main.h"
#include "s_sound.h"
#include "sounds.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

// ---- m_misc.c (verbatim): M_WriteFile / M_ReadFile ----
#ifndef O_BINARY
#define O_BINARY 0
#endif

boolean
M_WriteFile
( char const*	name,
  void*		source,
  int		length )
{
    int		handle;
    int		count;
	
    handle = open ( name, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);

    if (handle == -1)
	return false;

    count = write (handle, source, length);
    close (handle);
	
    if (count < length)
	return false;
		
    return true;
}


//
// M_ReadFile
//
int
M_ReadFile
( char const*	name,
  byte**	buffer )
{
    int	handle, count, length;
    struct stat	fileinfo;
    byte		*buf;
	
    handle = open (name, O_RDONLY | O_BINARY, 0666);
    if (handle == -1)
	I_Error ("Couldn't read file %s", name);
    if (fstat (handle,&fileinfo) == -1)
	I_Error ("Couldn't read file %s", name);
    length = fileinfo.st_size;
    buf = Z_Malloc (length, PU_STATIC, NULL);
    count = read (handle, buf, length);
    close (handle);
	
    if (count < length)
	I_Error ("Couldn't read file %s", name);
		
    *buffer = buf;
    return length;
}



// captura de pantalla (original: PCX): aquí BMP de 8 bits con la paleta actual
extern byte game_palette[768];
void M_ScreenShot(void){
    char name[32]; int i; FILE *f=NULL;
    for(i=0;i<100;i++){ sprintf(name,"doom%02d.bmp",i); if(access(name,F_OK)!=0) break; }
    if(i==100) return; f=fopen(name,"wb"); if(!f) return;
    unsigned char h[54]={'B','M'}; unsigned size=54+1024+320*200; memcpy(h+2,&size,4); unsigned off=54+1024; memcpy(h+10,&off,4);
    unsigned d=40; memcpy(h+14,&d,4); int w=320,hh=200; memcpy(h+18,&w,4); memcpy(h+22,&hh,4); h[26]=1; h[28]=8; unsigned n=256; memcpy(h+46,&n,4);
    fwrite(h,1,54,f); for(int c=0;c<256;c++){ unsigned char q[4]={game_palette[c*3+2],game_palette[c*3+1],game_palette[c*3],0}; fwrite(q,1,4,f); }
    for(int y=199;y>=0;y--) fwrite(screens[0]+y*320,1,320,f);
    fclose(f); players[consoleplayer].message="SCREEN SHOT";
}
void M_LoadDefaults(void){}
void M_SaveDefaults(void){}

// ---- globales de d_main.c / d_net.c / i_system.c / r_*.c ----
boolean nomonsters, respawnparm, fastparm, devparm;
int ticdup=1, maketic; boolean singletics=false;
gamestate_t wipegamestate = GS_LEVEL;
ticcmd_t netcmds[MAXPLAYERS][BACKUPTICS];
boolean setsizeneeded;
int g_quit_request=0;
void I_Quit(void){ g_quit_request=1; }
void I_WaitVBL(int count){ (void)count; }
void R_SetViewSize(int blocks,int detail){ (void)blocks; (void)detail; setsizeneeded=true; }
void R_FillBackScreen(void){}
void R_ExecuteSetViewSize(void){ setsizeneeded=false; }

// ---- pantalla de título (D_PageTicker / D_AdvanceDemo / D_StartTitle) ----
char *pagename="TITLEPIC"; int pagetic=0, demosequence=-1;
void D_PageTicker(void){ if(--pagetic<0) D_AdvanceDemo(); }
void D_PageDrawer(void){ V_DrawPatch(0,0,0,(patch_t*)W_CacheLumpName(pagename,PU_CACHE)); }
void D_AdvanceDemo(void){
    // sin demos grabados: alterna TITLEPIC / CREDIT
    gamestate=GS_DEMOSCREEN; demosequence=(demosequence+1)%2;
    if(demosequence==0){ pagetic=(gamemode==commercial)?35*11:170; pagename="TITLEPIC"; S_StartMusic(gamemode==commercial? mus_dm2ttl : mus_intro); }
    else { pagetic=200; pagename="CREDIT"; if(W_CheckNumForName(pagename)<0) pagename="TITLEPIC"; }
}
void D_StartTitle(void){ gameaction=ga_nothing; demosequence=-1; D_AdvanceDemo(); }
