// game_main.c - bucle principal: SDL + P_Ticker/ST/HU/AM ORIGINALES a 35 tics/s + render propio.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "compat.h"
#include "wad.h"
#include "tex.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_player.h"
#include "z_zone.h"
#include "w_wad.h"
#include "p_local.h"
#include "p_setup.h"
#include "p_tick.h"
#include "g_game.h"
#include "m_menu.h"
#include "f_finale.h"
#include "wi_stuff.h"
#include "d_main.h"
#include "m_swap.h"
#include "info.h"
#include "r_data.h"
#include "r_sky.h"
#include "v_video.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "am_map.h"
#include "s_sound.h"
#include "m_random.h"
#include "render.h"

#define OUTW 640
#define OUTH 480
extern int myargc; extern char **myargv;
extern byte game_palette[768]; extern int palette_serial;
void W_Init(WAD*); void R_InitDataShim(void);
extern int g_vm_on; void vm_game_frame(const unsigned int*); void snd_video_music(const char*); void I_InitSound(void); void I_ShutdownSound(void);
void G_BuildTiccmd(ticcmd_t *cmd);
extern int key_right,key_left,key_up,key_down,key_strafeleft,key_straferight,key_fire,key_use,key_strafe,key_speed;
extern int mousebfire,mousebstrafe,mousebforward,mousex,mousey;
extern int levelstarttic; extern boolean gamekeydown[256], *mousebuttons;

static uint32_t out[RV_WIDE*OUTH];
static int outw=OUTW;
extern int g_quit_request, mouseSensitivity, showMessages, screenblocks;
extern int usegamma, detailLevel;
#ifndef SAVEGAMENAME
#define SAVEGAMENAME "doomsav"
#endif
extern boolean menuactive, paused;
void D_PageDrawer(void); void D_StartTitle(void);
static int mouse_acc=0, mouse_btn=0;
// depuracion (debug.c): Ctrl+D menu, Ctrl+P captura
void DBG_Init(void (*wide)(void),void (*shot)(void),const char *renderer); void DBG_SetPad(const char*); int DBG_Active(void); int DBG_Paused(void);
static int opt_scale=1;
void DBG_Toggle(void); void DBG_Frame(double ms); int DBG_Key(int sym); void DBG_Draw(uint32_t *out,int w,int h); void DBG_Open(int pg);
const char *DBG_Screenshot(const uint32_t *px,int w,int h);
static int want_shot=0, dbg_arg=0; static void req_shot(void){ want_shot=1; }
static SDL_Window *g_win; static SDL_Renderer *g_ren; static SDL_Texture *g_tex;
static void toggle_wide(void){
    int wide=(outw==OUTW); outw= wide? RV_WIDE : OUTW; RV_SetWide(wide);
    SDL_DestroyTexture(g_tex); g_tex=SDL_CreateTexture(g_ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,outw,OUTH);
    SDL_RenderSetLogicalSize(g_ren,outw,OUTH);
    if(!(SDL_GetWindowFlags(g_win)&(SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP))) SDL_SetWindowSize(g_win,outw*opt_scale,OUTH*opt_scale);
}

#ifndef DP_VERSION
#define DP_VERSION "1.0.4"
#endif
// ---- opciones de ventana y configuracion guardada (doomproject.cfg en la carpeta de datos del usuario) ----
static int opt_full=0, opt_fps=0, opt_wide=0;
static int cfg_gamma=-1, cfg_sens=-1, cfg_blocks=-1, cfg_msgs=-1;
static char cfgpath[700];
static void cfg_init_path(void){
    char *p=SDL_GetPrefPath("COS-Mods","DoomProject");
    if(p){ snprintf(cfgpath,sizeof cfgpath,"%sdoomproject.cfg",p); SDL_free(p); } else snprintf(cfgpath,sizeof cfgpath,"doomproject.cfg");
}
static void cfg_load(void){
    FILE *f=fopen(cfgpath,"r"); if(!f) return; char k[40]; int v;
    while(fscanf(f,"%39s %d",k,&v)==2){
        if(!strcmp(k,"fullscreen")) opt_full=v!=0;
        else if(!strcmp(k,"scale")) { if(v>=1&&v<=6) opt_scale=v; }
        else if(!strcmp(k,"gamma")) { if(v>=0&&v<=4) cfg_gamma=v; }
        else if(!strcmp(k,"sensitivity")) { if(v>=0&&v<=9) cfg_sens=v; }
        else if(!strcmp(k,"screenblocks")) { if(v>=3&&v<=11) cfg_blocks=v; }
        else if(!strcmp(k,"messages")) cfg_msgs=v!=0;
    }
    fclose(f);
}
static void cfg_save(void){
    FILE *f=fopen(cfgpath,"w"); if(!f) return;
    fprintf(f,"fullscreen %d\nscale %d\ngamma %d\nsensitivity %d\nscreenblocks %d\nmessages %d\n",opt_full,opt_scale,usegamma,mouseSensitivity,screenblocks,showMessages);
    fclose(f);
}
static void usage(void){
    printf("Doom Project %s  -  motor hibrido (render propio + codigo de juego original de id Software)\n\n"
           "Uso: doom_game [DOOM.WAD] [-file extra.wad ...] [opciones]\n"
           "  --fullscreen / --windowed   pantalla completa o ventana (Alt+Enter lo cambia en juego)\n"
           "  --scale 4:3 | 16:9          proporcion de pantalla: 4:3 original (por defecto) o 16:9 con mas campo de vision a los lados\n"
           "  Alt+W (en juego)            alterna 4:3 / 16:9\n"
           "  --scale N                   tamano inicial de la ventana: N veces 640x480 (1 a 6); la ventana tambien se puede estirar\n"
           "  Ctrl+P / Ctrl+D (en juego)  captura de pantalla (PNG) / menu de depuracion (render, jugador, sonido, pruebas)\n"
           "  --fps                       muestra los cuadros por segundo en el titulo de la ventana\n"
           "  --map E1M1  --skill 1..5    empezar directo en un mapa\n"
           "  --video-music [carpeta]     musica de videos (ffmpeg)\n"
           "  --version, --help\n"
           "Mando: palanca izq. mover, derecha girar, RT disparar, A usar/aceptar, LT correr, LB/RB arma, Start menu, Back mapa.\n"
           "Las opciones (pantalla, gamma, sensibilidad) se guardan solas al salir.\n",DP_VERSION);
}

// ---- mando (SDL GameController): se traduce a las mismas teclas/eventos del Doom original ----
static SDL_GameController *pad=NULL;
static int pad_fwd=0, pad_str=0, pad_fire=0, pad_run=0, pad_rel_key=0;
static void pad_open(void){
    if(pad) return;
    for(int i=0;i<SDL_NumJoysticks();i++) if(SDL_IsGameController(i)){ pad=SDL_GameControllerOpen(i); if(pad){ printf("[PAD] %s\n",SDL_GameControllerName(pad)); break; } }
}
static void pad_close_if_gone(void){ if(pad && !SDL_GameControllerGetAttached(pad)){ SDL_GameControllerClose(pad); pad=NULL; } }


// ---- cola de eventos (D_PostEvent / D_ProcessEvents originales) ----
static event_t evq[64]; static int evh=0, evt=0;
static void post(int type,int a,int b,int c){ event_t e; e.type=type; e.data1=a; e.data2=b; e.data3=c; evq[evh]=e; evh=(evh+1)&63; }
static void process_events(void){
    for(; evt!=evh; evt=(evt+1)&63){
        if(M_Responder(&evq[evt])) continue;
        G_Responder(&evq[evt]);
    }
}

static int translate(SDL_Keycode k){
    switch(k){
    case SDLK_RIGHT: return KEY_RIGHTARROW; case SDLK_LEFT: return KEY_LEFTARROW;
    case SDLK_UP: return KEY_UPARROW; case SDLK_DOWN: return KEY_DOWNARROW;
    case SDLK_ESCAPE: return KEY_ESCAPE; case SDLK_RETURN: case SDLK_KP_ENTER: return KEY_ENTER; case SDLK_TAB: return KEY_TAB;
    case SDLK_BACKSPACE: return KEY_BACKSPACE; case SDLK_PAUSE: return KEY_PAUSE;
    case SDLK_LCTRL: case SDLK_RCTRL: return KEY_RCTRL;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return KEY_RSHIFT;
    case SDLK_LALT: case SDLK_RALT: return KEY_RALT;
    case SDLK_MINUS: return KEY_MINUS; case SDLK_EQUALS: return KEY_EQUALS;
    case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4: case SDLK_F5: case SDLK_F6: case SDLK_F7: case SDLK_F8: case SDLK_F9: case SDLK_F10:
        return KEY_F1+(k-SDLK_F1);
    case SDLK_F11: return KEY_F11; case SDLK_F12: return KEY_F12;
    }
    if(k>=32 && k<127) return (k>='A'&&k<='Z')? k+32 : k;
    return 0;
}
// WASD y E como alias en juego (no en menus, para poder escribir nombres de partida)
static unsigned char aliased[128];
static int alias_of(int raw,int down){
    if(raw<0||raw>=128) return raw;
    if(!down && aliased[raw]){ aliased[raw]=0; goto map; }
    if(down){
        if(gamestate!=GS_LEVEL || menuactive) return raw;
        if(raw=='w'||raw=='a'||raw=='s'||raw=='d'||raw=='e') aliased[raw]=1; else return raw;
    } else return raw;
map:
    switch(raw){ case 'w': return key_up; case 's': return key_down; case 'a': return key_strafeleft; case 'd': return key_straferight; case 'e': return key_use; }
    return raw;
}
static void key_event(int down,int raw){
    if(!raw) return;
    raw=alias_of(raw,down);
    post(down? ev_keydown : ev_keyup, raw,0,0);
}
static void feed_cheat(const char *s){ for(;*s;s++){ event_t ev={ev_keydown,*s,0,0}; ST_Responder(&ev); } }

static int key_by_name(const char *n){
    if(!strcmp(n,"esc")) return KEY_ESCAPE; if(!strcmp(n,"enter")) return KEY_ENTER;
    if(!strcmp(n,"up")) return KEY_UPARROW; if(!strcmp(n,"down")) return KEY_DOWNARROW;
    if(!strcmp(n,"left")) return KEY_LEFTARROW; if(!strcmp(n,"right")) return KEY_RIGHTARROW;
    if(!strcmp(n,"space")) return ' '; if(!strcmp(n,"tab")) return KEY_TAB; if(!strcmp(n,"bs")) return KEY_BACKSPACE;
    if(n[0]=='f' && n[1]>='1' && n[1]<='9' && !n[2]) return KEY_F1+(n[1]-'1');
    if(!strcmp(n,"f10")) return KEY_F10; if(!strcmp(n,"f11")) return KEY_F11;
    if(n[0]&&!n[1]) return n[0];
    return 0;
}

// teclas simuladas para pruebas:  w s a d (mover) l r (girar) f (fuego) e (usar) S (correr) 1-7 (arma)
static void script_keys(const char *k){
    memset(gamekeydown,0,sizeof(boolean)*256);
    for(;*k;k++){
        switch(*k){
        case 'w': gamekeydown[key_up]=1; break;       case 's': gamekeydown[key_down]=1; break;
        case 'a': gamekeydown[key_strafeleft]=1; break; case 'd': gamekeydown[key_straferight]=1; break;
        case 'l': gamekeydown[key_left]=1; break;     case 'r': gamekeydown[key_right]=1; break;
        case 'f': gamekeydown[key_fire]=1; break;     case 'e': gamekeydown[key_use]=1; break;
        case 'S': gamekeydown[key_speed]=1; break;
        default: if(*k>='1'&&*k<='7') gamekeydown[(int)*k]=1;
        }
    }
}

// un tic completo, igual que TryRunTics: eventos, ticcmd, M_Ticker, G_Ticker
static void run_tic(void){
    process_events();
    G_BuildTiccmd(&netcmds[consoleplayer][maketic%BACKUPTICS]); maketic++;
    M_Ticker();
    G_Ticker();
    gametic++;
}

// D_Display: dibuja segun gamestate
static void render_frame(double frac,double px,double py,double pz,double pa){
    static int oldstate=-1;
    if(palette_serial!=0){ static int last=-1; if(last!=palette_serial){ RV_SetPalette(game_palette); last=palette_serial; } }
    if(gamestate!=oldstate && oldstate!=-1){ I_SetPalette((byte*)W_CacheLumpName("PLAYPAL",PU_CACHE)); RV_SetPalette(game_palette); }
    oldstate=gamestate;
    int show3d=0;
    switch(gamestate){
    case GS_LEVEL: {
        player_t *pl=&players[displayplayer]; mobj_t *mo=pl->mo;
        if(!mo) break;
        double x=mo->x/65536.0, y=mo->y/65536.0, z=pl->viewz/65536.0, a=mo->angle*(2*M_PI/4294967296.0);
        if(frac<1 && leveltime>1 && hypot(x-px,y-py)<96){
            x=px+(x-px)*frac; y=py+(y-py)*frac; z=pz+(z-pz)*frac;
            double da=a-pa; while(da>M_PI) da-=2*M_PI; while(da<-M_PI) da+=2*M_PI;
            a=pa+da*frac;
        }
        RV_Render(pl,x,y,z,a); show3d=1;
        memset(screens[0],255,SCREENWIDTH*SCREENHEIGHT);
        if(automapactive){ AM_Drawer(); show3d=2; }
        ST_Drawer(false,true);
        HU_Drawer();
        break; }
    case GS_INTERMISSION: WI_Drawer(); break;
    case GS_FINALE: F_Drawer(); break;
    case GS_DEMOSCREEN: D_PageDrawer(); break;
    }
    if(paused){ patch_t *pp=(patch_t*)W_CacheLumpName("M_PAUSE",PU_CACHE); V_DrawPatch((SCREENWIDTH-SHORT(pp->width))/2,4,0,pp); }
    M_Drawer();
    RV_Compose(out,outw,OUTH,screens[0],show3d);
    if(g_vm_on){ if(outw==OUTW) vm_game_frame(out); else { static uint32_t crop[OUTW*OUTH]; int ox=(outw-OUTW)/2; for(int r=0;r<OUTH;r++) memcpy(crop+r*OUTW,out+r*outw+ox,OUTW*4); vm_game_frame(crop); } }
}


// PWAD suelto: busca la base DOOM.WAD en $DOOM_WAD, junto al PWAD y en la carpeta actual
static int file_exists(const char *p){ FILE *f=fopen(p,"rb"); if(f){ fclose(f); return 1; } return 0; }
static WAD *find_base_iwad(const char *pwad){
    const char *env=getenv("DOOM_WAD"); char d[700];
    if(env && file_exists(env)) return wad_load(env);
    snprintf(d,sizeof d,"%s",pwad); char *sl=strrchr(d,'/');
#ifdef _WIN32
    { char *bs=strrchr(d,'\\'); if(bs>sl) sl=bs; }
#endif
    if(sl){ snprintf(sl+1,sizeof d-(size_t)(sl+1-d),"DOOM.WAD"); if(file_exists(d)) return wad_load(d); }
    return file_exists("DOOM.WAD")? wad_load("DOOM.WAD") : NULL;
}

int main(int argc,char **argv){
    myargc=argc; myargv=argv; SDL_SetMainReady();
    const char *pwads[16]; int npw=0, in_file=0; int wait_s=0; const char *wadp=NULL,*mapname=NULL,*shot=NULL,*seq=NULL,*cheat=NULL,*postl=NULL; int skill=2, ntics=-1;
    int do_exit=0, do_save=-1, do_load=-1; int cli_full=-1, cli_scale=0, cli_wide=-1;
    double tp[3]; int have_tp=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--map")&&i+1<argc) mapname=argv[++i];
        else if(!strcmp(argv[i],"--skill")&&i+1<argc) skill=atoi(argv[++i])-1;
        else if(!strcmp(argv[i],"--shot")&&i+1<argc) shot=argv[++i];
        else if(!strcmp(argv[i],"--seq")&&i+1<argc) seq=argv[++i];
        else if(!strcmp(argv[i],"--cheat")&&i+1<argc) cheat=argv[++i];
        else if(!strcmp(argv[i],"--post")&&i+1<argc) postl=argv[++i];
        else if(!strcmp(argv[i],"--exit")) do_exit=1;
        else if(!strcmp(argv[i],"--secret")) do_exit=2;
        else if(!strcmp(argv[i],"--save")&&i+1<argc) do_save=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--load")&&i+1<argc) do_load=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--video-music")){ const char *d="Video_Music"; if(i+1<argc && argv[i+1][0]!='-' && !strstr(argv[i+1],".WAD") && !strstr(argv[i+1],".wad")) d=argv[++i]; snd_video_music(d); }
        else if(!strcmp(argv[i],"--wait")&&i+1<argc) wait_s=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--fullscreen")) cli_full=1;
        else if(!strcmp(argv[i],"--windowed")) cli_full=0;
        else if(!strcmp(argv[i],"--scale")&&i+1<argc){ const char *v=argv[++i];
            if(!strcmp(v,"16:9")) cli_wide=1; else if(!strcmp(v,"4:3")) cli_wide=0;
            else { cli_scale=atoi(v); if(cli_scale<1) cli_scale=1; if(cli_scale>6) cli_scale=6; } }
        else if(!strcmp(argv[i],"--fps")) opt_fps=1;
        else if(!strcmp(argv[i],"--dbg")&&i+1<argc) dbg_arg=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--alphabet")){ extern int Clay_Alphabet(const char*); return Clay_Alphabet(i+1<argc&&argv[i+1][0]!='-'? argv[i+1] : "clay"); }
        else if(!strcmp(argv[i],"--version")){ printf("Doom Project %s\n",DP_VERSION); return 0; }
        else if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){ usage(); return 0; }
        else if(!strcmp(argv[i],"--tics")&&i+1<argc) ntics=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--pos")&&i+3<argc){ tp[0]=atof(argv[i+1]); tp[1]=atof(argv[i+2]); tp[2]=atof(argv[i+3]); have_tp=1; i+=3; }
        else if(!strcmp(argv[i],"-file")||!strcmp(argv[i],"--file")) in_file=1;
        else if(argv[i][0]!='-'){ if(!wadp && !in_file) wadp=argv[i]; else if(npw<16) pwads[npw++]=argv[i]; }
    }
    int test_mode = (shot || ntics>=0 || do_save>=0 || do_load>=0);
    cfg_init_path(); if(!test_mode) cfg_load();
    if(cli_full>=0) opt_full=cli_full; if(cli_wide>=0) opt_wide=cli_wide; outw= opt_wide? RV_WIDE : OUTW; RV_SetWide(opt_wide); if(cli_scale) opt_scale=cli_scale;
    if(!wadp) wadp="DOOM.WAD";
    WAD *w=wad_load(wadp); if(!w){ fprintf(stderr,"No puedo abrir %s\nNecesitas un WAD: tu DOOM.WAD, o consigue uno en %s (o Freedoom: https://freedoom.github.io)\n",wadp,"https://archive.org/download/doom-wads"); return 1; }
    if(!strcmp(w->magic,"PWAD")){      // se pasó solo un PWAD: la base es DOOM.WAD
        WAD *base=find_base_iwad(wadp); if(!base){ fprintf(stderr,"%s es un PWAD y no encuentro DOOM.WAD (la base): ponlo en la misma carpeta, en la actual, o usa DOOM_WAD=/ruta/DOOM.WAD\n",wadp); return 1; }
        if(npw<16) pwads[npw++]=wadp;
        wad_free(w); w=base; wadp="DOOM.WAD";
    }
    for(int k=0;k<npw;k++){ WAD *p=wad_load(pwads[k]); if(!p){ fprintf(stderr,"No puedo abrir %s\n",pwads[k]); return 1; } wad_merge(w,p); printf("[WAD] + PWAD %s\n",pwads[k]); }
    if(shot) setenv("SDL_VIDEODRIVER","dummy",0);
    if(shot && !getenv("DOOM_KEEPSOUND")) setenv("DOOM_NOSOUND","1",0);
    tex_init(w); W_Init(w); Z_Init();
    gamemode = W_CheckNumForName("MAP01")>=0? commercial : W_CheckNumForName("E4M1")>=0? retail : W_CheckNumForName("E2M1")>=0? registered : shareware;
    M_ClearRandom();
    key_right=KEY_RIGHTARROW; key_left=KEY_LEFTARROW; key_up=KEY_UPARROW; key_down=KEY_DOWNARROW;
    key_strafeleft=','; key_straferight='.'; key_fire=KEY_RCTRL; key_use=' '; key_strafe=KEY_RALT; key_speed=KEY_RSHIFT;
    mousebfire=0; mousebstrafe=1; mousebforward=2;
    mouseSensitivity=5; showMessages=1; screenblocks=9; detailLevel=0; usegamma=0;
    if(cfg_sens>=0) mouseSensitivity=cfg_sens; if(cfg_msgs>=0) showMessages=cfg_msgs; if(cfg_blocks>=0) screenblocks=cfg_blocks; if(cfg_gamma>=0) usegamma=cfg_gamma;
    R_InitDataShim(); V_Init(); P_Init(); HU_Init(); ST_Init();
    RV_Init(w);
    I_SetPalette((byte*)W_CacheLumpName("PLAYPAL",PU_CACHE));
    RV_SetPalette(game_palette);
    if(SDL_Init(SDL_INIT_VIDEO)!=0){ fprintf(stderr,"SDL %s\n",SDL_GetError()); return 1; }
    if(!test_mode && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0) pad_open();
    extern int numChannels; numChannels=8;
    { const char *md=getenv("DOOM_MUSIC_DIR"); char t[300]; snprintf(t,sizeof t,"%s/D_E1M1.wav",md?md:"music");
      FILE *tf=fopen(t,"rb"); if(tf) fclose(tf); else fprintf(stderr,"[MUSICA] no hay %s: se usa el sintetizador de respaldo (suena mas crudo). Ejecuta 'make music' para la version CTTS 8 bits.\n",md?md:"music/"); }
    I_InitSound(); S_Init(100,100);
    M_Init();
    playeringame[0]=true; consoleplayer=displayplayer=0;
    for(int i=0;i<MAXPLAYERS;i++) memset(&players[i],0,sizeof(player_t)), players[i].playerstate=PST_LIVE;
    gameskill=(skill_t)skill; gameepisode=1; gamemap=1;

    int ep=1,map=1;
    if(mapname){
        if(!strncasecmp(mapname,"MAP",3)) map=atoi(mapname+3);
        else if(toupper(mapname[0])=='E'){ ep=mapname[1]-'0'; map=atoi(mapname+3); }
    }
    int test = (shot || ntics>=0 || do_save>=0 || do_load>=0);
    if(do_save>=0||do_load>=0) setenv("SDL_VIDEODRIVER","dummy",0), setenv("DOOM_NOSOUND","1",0);
    if(mapname || (!test && 0)){ G_DeferedInitNew((skill_t)skill,ep,map); run_tic(); }
    else { D_StartTitle(); }
    if(mapname){
        printf("[JUEGO] %s  sectores=%d segs=%d  jugador (%d,%d)\n",wadp,numsectors,numsegs,players[0].mo->x>>16,players[0].mo->y>>16);
        if(have_tp && players[0].mo){
            mobj_t *mo=players[0].mo; P_UnsetThingPosition(mo);
            mo->x=(fixed_t)(tp[0]*65536); mo->y=(fixed_t)(tp[1]*65536); mo->angle=(angle_t)(tp[2]/360.0*4294967296.0);
            P_SetThingPosition(mo); mo->floorz=mo->subsector->sector->floorheight; mo->ceilingz=mo->subsector->sector->ceilingheight; mo->z=mo->floorz;
        }
        if(cheat) feed_cheat(cheat);
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"nearest");
    Uint32 wflags=SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE; if(opt_full && !test_mode) wflags|=SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window *win=g_win=SDL_CreateWindow("DOOM Project",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,outw*opt_scale,OUTH*opt_scale,wflags);
    SDL_Renderer *ren= shot? NULL : SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!ren) ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE);
    { SDL_RendererInfo ri; static char rn[32]="?"; if(SDL_GetRendererInfo(ren,&ri)==0) snprintf(rn,sizeof rn,"%s",ri.name); DBG_Init(toggle_wide,req_shot,rn); }
    SDL_RenderSetLogicalSize(ren,outw,OUTH);      // la imagen 4:3 se estira con la ventana y deja bandas negras
    SDL_Texture *tex; g_ren=ren; tex=g_tex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,outw,OUTH);

    if(test){      // modo prueba sin reloj: tics exactos
        int total=0; if(getenv("DOOM_KEEPSOUND")) SDL_Delay(1500);   // deja arrancar ffmpeg antes de que suenen los efectos de la prueba
        for(int i=0;i<2;i++){ run_tic(); total++; }
        if(do_load>=0){ char nm[32]; sprintf(nm,SAVEGAMENAME"%d.dsg",do_load); G_LoadGame(nm); for(int i=0;i<2;i++){ run_tic(); total++; } printf("[JUEGO] partida %d cargada: tic de nivel %d\n",do_load,leveltime); }
        if(do_save>=0){ G_SaveGame(do_save,"prueba"); for(int i=0;i<2;i++){ run_tic(); total++; } printf("[JUEGO] partida %d guardada\n",do_save); }
        if(do_exit==1) G_ExitLevel(); else if(do_exit==2) G_SecretExitLevel();
        if(postl){
            char pb[256]; strncpy(pb,postl,255); pb[255]=0;
            for(char *t=strtok(pb,","); t; t=strtok(NULL,",")){
                int k=key_by_name(t); if(!k) continue;
                post(ev_keydown,k,0,0); run_tic(); post(ev_keyup,k,0,0); run_tic(); total+=2;
            }
        }
        char buf[512]; strncpy(buf,seq?seq:"-:0",511); buf[511]=0;
        for(char *tok=strtok(buf,","); tok; tok=strtok(NULL,",")){
            char *c=strchr(tok,':'); int n=c? atoi(c+1):1; if(c) *c=0;
            script_keys(strcmp(tok,"-")?tok:""); for(int i=0;i<n;i++){ run_tic(); total++; }
        }
        script_keys("");
        while(total<ntics){ run_tic(); total++; }
        player_t *pl=&players[0];
        if(pl->mo) printf("[JUEGO] tic %d  estado %d  pos (%d,%d) salud %d armadura %d  balas %d  bajas %d/%d  arma %d\n",gametic,gamestate,pl->mo->x>>16,pl->mo->y>>16,pl->health,pl->armorpoints,pl->ammo[am_clip],pl->killcount,totalkills,pl->readyweapon);
        else printf("[JUEGO] tic %d  estado %d\n",gametic,gamestate);
        render_frame(1,0,0,0,0);
        if(dbg_arg){ DBG_Open(dbg_arg); DBG_Frame(16); DBG_Draw(out,outw,OUTH); }
        if(wait_s) SDL_Delay(wait_s*1000);
        I_ShutdownSound();
        if(shot){
            SDL_Surface *sf=SDL_CreateRGBSurfaceFrom(out,outw,OUTH,32,outw*4,0xFF0000,0xFF00,0xFF,0xFF000000);
            SDL_SaveBMP(sf,shot); SDL_FreeSurface(sf); printf("[SHOT] %s\n",shot);
        }
        return 0;
    }

    int running=1, relmouse=0; Uint32 last=SDL_GetTicks(); double acc=0;
    Uint32 fps_t=last; int fps_n=0;
    double px=0,py=0,pz=0,pa=0;
    while(running && !g_quit_request){
        Uint32 now=SDL_GetTicks(); double dtms=now-last; acc+=dtms; last=now; DBG_Frame(dtms); DBG_SetPad(pad? SDL_GameControllerName(pad) : NULL); if(acc>250) acc=250;
        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT) running=0;
            else if(ev.type==SDL_KEYDOWN && !ev.key.repeat && (ev.key.keysym.sym==SDLK_RETURN||ev.key.keysym.sym==SDLK_KP_ENTER) && (ev.key.keysym.mod&KMOD_ALT)){
                opt_full=!opt_full; SDL_SetWindowFullscreen(win,opt_full? SDL_WINDOW_FULLSCREEN_DESKTOP:0);
            }
            else if(ev.type==SDL_KEYDOWN && !ev.key.repeat && ev.key.keysym.sym==SDLK_w && (ev.key.keysym.mod&KMOD_ALT)) toggle_wide();      // Alt+W: alterna 4:3 / 16:9 (no se guarda)
            else if(ev.type==SDL_KEYDOWN && !ev.key.repeat && (ev.key.keysym.mod&KMOD_CTRL) && ev.key.keysym.sym==SDLK_p) req_shot();         // Ctrl+P: captura
            else if(ev.type==SDL_KEYDOWN && !ev.key.repeat && (ev.key.keysym.mod&KMOD_CTRL) && ev.key.keysym.sym==SDLK_d) DBG_Toggle();      // Ctrl+D: depuracion
            else if(ev.type==SDL_KEYDOWN && DBG_Active()){ if(!ev.key.repeat || ev.key.keysym.sym==SDLK_UP || ev.key.keysym.sym==SDLK_DOWN) DBG_Key(ev.key.keysym.sym); }
            else if(ev.type==SDL_CONTROLLERDEVICEADDED) pad_open();
            else if(ev.type==SDL_CONTROLLERDEVICEREMOVED) pad_close_if_gone();
            else if((ev.type==SDL_CONTROLLERBUTTONDOWN||ev.type==SDL_CONTROLLERBUTTONUP) && !DBG_Active()){
                int d=(ev.type==SDL_CONTROLLERBUTTONDOWN), t=d? ev_keydown:ev_keyup, b=ev.cbutton.button;
                switch(b){
                case SDL_CONTROLLER_BUTTON_A: post(t,KEY_ENTER,0,0); post(t,key_use,0,0); break;
                case SDL_CONTROLLER_BUTTON_B: if(menuactive) post(t,KEY_ESCAPE,0,0); else post(t,key_use,0,0); break;
                case SDL_CONTROLLER_BUTTON_START: post(t,KEY_ESCAPE,0,0); break;
                case SDL_CONTROLLER_BUTTON_BACK: post(t,KEY_TAB,0,0); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: post(t,KEY_UPARROW,0,0); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: post(t,KEY_DOWNARROW,0,0); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: post(t,KEY_LEFTARROW,0,0); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: post(t,KEY_RIGHTARROW,0,0); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    if(d && gamestate==GS_LEVEL && !menuactive && players[0].mo){      // arma anterior/siguiente entre las que tienes
                        int dir=(b==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)?1:-1, cur=players[0].readyweapon; if(cur==7) cur=0; if(cur==8) cur=2;
                        for(int k=1;k<=7;k++){ int w=((cur+dir*k)%7+7)%7; if(players[0].weaponowned[w]){ pad_rel_key='1'+w; post(ev_keydown,pad_rel_key,0,0); break; } }
                    }
                    break;
                }
            }
            else if(ev.type==SDL_KEYDOWN||ev.type==SDL_KEYUP){
                if(ev.key.repeat && menuactive==0) continue;
                if(ev.key.repeat && ev.type==SDL_KEYUP) continue;
                key_event(ev.type==SDL_KEYDOWN,translate(ev.key.keysym.sym));
            }
            else if(ev.type==SDL_MOUSEMOTION) mouse_acc+=ev.motion.xrel;
            else if(ev.type==SDL_MOUSEBUTTONDOWN||ev.type==SDL_MOUSEBUTTONUP){
                int bit= ev.button.button==SDL_BUTTON_LEFT?1: ev.button.button==SDL_BUTTON_RIGHT?2: 4;
                if(ev.type==SDL_MOUSEBUTTONDOWN) mouse_btn|=bit; else mouse_btn&=~bit;
                post(ev_mouse,mouse_btn,0,0);
            }
        }
        int want = (gamestate==GS_LEVEL && !menuactive && !paused && !DBG_Active());
        if(want!=relmouse){ SDL_SetRelativeMouseMode(want? SDL_TRUE:SDL_FALSE); relmouse=want; mouse_acc=0; }
        const double TIC=1000.0/35;
        if(DBG_Paused()) acc=0;      // Ctrl+D -> "Simulacion: pausar"
        while(acc>=TIC){
            mobj_t *mo=players[0].mo;
            if(mo && gamestate==GS_LEVEL){ px=mo->x/65536.0; py=mo->y/65536.0; pz=players[0].viewz/65536.0; pa=mo->angle*(2*M_PI/4294967296.0); }
            if(pad && !DBG_Active()){
                #define AX(a) (SDL_GameControllerGetAxis(pad,a)/32768.0)
                double lx=AX(SDL_CONTROLLER_AXIS_LEFTX), ly=AX(SDL_CONTROLLER_AXIS_LEFTY), rx=AX(SDL_CONTROLLER_AXIS_RIGHTX);
                int fw= ly<-0.35? 1 : ly>0.35? -1 : 0, st= lx>0.45? 1 : lx<-0.45? -1 : 0;
                int fi= AX(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>0.3, ru= AX(SDL_CONTROLLER_AXIS_TRIGGERLEFT)>0.3;
                if(fw!=pad_fwd){ if(pad_fwd) post(ev_keyup, pad_fwd>0? key_up:key_down,0,0); if(fw) post(ev_keydown, fw>0? key_up:key_down,0,0); pad_fwd=fw; }
                if(st!=pad_str){ if(pad_str) post(ev_keyup, pad_str>0? key_straferight:key_strafeleft,0,0); if(st) post(ev_keydown, st>0? key_straferight:key_strafeleft,0,0); pad_str=st; }
                if(fi!=pad_fire){ post(fi? ev_keydown:ev_keyup,key_fire,0,0); pad_fire=fi; }
                if(ru!=pad_run){ post(ru? ev_keydown:ev_keyup,key_speed,0,0); pad_run=ru; }
                if(gamestate==GS_LEVEL && !menuactive && (rx>0.15||rx<-0.15)){ double m=(rx<0?-rx:rx)-0.15; m=m/0.85; m=m*m*(rx<0?-1:1); post(ev_mouse,mouse_btn,(int)(m*100),0); }
            }
            if(relmouse && mouse_acc) post(ev_mouse,mouse_btn,mouse_acc*3,0);
            mouse_acc=0;
            run_tic(); acc-=TIC;
            if(pad_rel_key){ post(ev_keyup,pad_rel_key,0,0); pad_rel_key=0; }
        }
        S_UpdateSounds(players[displayplayer].mo);
        render_frame(gamestate==GS_LEVEL? acc/TIC : 1,px,py,pz,pa);
        if(want_shot){ want_shot=0; const char *sp=DBG_Screenshot(out,outw,OUTH); players[consoleplayer].message= sp? "CAPTURA GUARDADA" : "NO SE PUDO GUARDAR LA CAPTURA"; printf("[CAPTURA] %s\n",sp? sp : "error"); fflush(stdout); }
        DBG_Draw(out,outw,OUTH);
        SDL_UpdateTexture(g_tex,NULL,out,outw*4);
        SDL_RenderClear(ren); SDL_RenderCopy(ren,g_tex,NULL,NULL); SDL_RenderPresent(ren);
        if(opt_fps){ fps_n++; Uint32 t=SDL_GetTicks(); if(t-fps_t>=1000){ char ti[64]; snprintf(ti,sizeof ti,"DOOM Project  -  %d fps",(int)(fps_n*1000.0/(t-fps_t))); SDL_SetWindowTitle(win,ti); fps_t=t; fps_n=0; } }
    }
    cfg_save();
    I_ShutdownSound(); SDL_Quit();
    return 0;
}
