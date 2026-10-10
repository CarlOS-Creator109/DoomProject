// video_shim.c - i_video.c / i_system.c (I_AllocLow) / r_draw.c / m_menu.c / g_game.c: lo que el HUD,
// la barra de estado y el automapa originales necesitan para dibujar en screens[] (320x200, 8 bits).
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "v_video.h"
#include "g_game.h"
#include "d_player.h"
int viewwidth = SCREENWIDTH, scaledviewwidth = SCREENWIDTH, viewheight = SCREENHEIGHT - 32;
int viewwindowx = 0, viewwindowy = 0;
byte *I_AllocLow(int length){ return (byte*)calloc(1,length); }

// I_SetPalette: el render lee game_palette (768 bytes) cuando palette_serial cambia
byte game_palette[768]; int palette_serial = 0;
void I_SetPalette(byte *palette){ memcpy(game_palette,palette,768); palette_serial++; }

// R_VideoErase: restaura desde screens[1]; aquí screens[1] es el "fondo transparente" (0xFF)
void R_VideoErase(unsigned ofs,int count){ memcpy(screens[0]+ofs, screens[1]+ofs, count); }

// idclev: pide cambiar de nivel (lo atiende el bucle principal)
