// debug.c - Ctrl+D: menu de depuracion (render, jugador, juego, sonido, pruebas)  y  Ctrl+P: captura de pantalla (PNG).
// Todo se dibuja encima de la imagen final (ya compuesta), asi que no toca el render ni el codigo original.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include "compat.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "p_local.h"
#include "p_mobj.h"
#include "r_state.h"
#include "w_wad.h"
#include "s_sound.h"
#include "sounds.h"
#include "tex.h"
#include "render.h"
#include "vfont.h"

extern int rv_st_segs, rv_st_subs, rv_st_spr; extern double rv_st_ms; extern int rv_w; int RV_NonBlank(void);
int snd_stats(const char **drv,int *active,int *pl,int *pr,long *frames); int snd_active(void);
void chip_stats(int *mode,int *pl,int *pa,double *pos,double *len); extern char g_music_name[16];
int I_StartSound(int id,int vol,int sep,int pitch,int priority); int I_GetSfxLumpNum(sfxinfo_t *s);
static int dbg_sfx(int id,int sep){ if(!S_sfx[id].data) S_sfx[id].lumpnum=I_GetSfxLumpNum(&S_sfx[id]); return I_StartSound(id,127,sep,128,0); }
extern int usegamma, screenblocks, showMessages;

// ---------------------------------------------------------------- estado del menu
static int dbg_on=0, page=0, sel=0, sim_paused=0;
static void (*cb_wide)(void)=NULL; static void (*cb_shot)(void)=NULL;
static const char *rend_name="?", *pad_name=NULL; static int g_outw=640;
static char result[96]="";
void DBG_Init(void (*wide)(void),void (*shot)(void),const char *renderer){ cb_wide=wide; cb_shot=shot; rend_name=renderer? renderer:"?"; }
void DBG_SetPad(const char *n){ pad_name=n; }
int DBG_Active(void){ return dbg_on; }
int DBG_Paused(void){ return sim_paused; }
void DBG_Toggle(void){ dbg_on=!dbg_on; }

// ---------------------------------------------------------------- medidas continuas
static double fps=0, loop_ms=0, tps=0, mix_rate=0; static int vu_lp=0, vu_rp=0;
static Uint32 t_mark=0; static int n_frames=0; static int tic_mark=0; static long mix_mark=0; static double ms_acc=0;
void DBG_Frame(double frame_ms){
    n_frames++; ms_acc+=frame_ms; Uint32 t=SDL_GetTicks();
    const char *d; int a,pl,pr; long fr; snd_stats(&d,&a,&pl,&pr,&fr);
    vu_lp= pl>vu_lp? pl : vu_lp*9/10; vu_rp= pr>vu_rp? pr : vu_rp*9/10;
    if(t-t_mark>=1000){
        double s=(t-t_mark)/1000.0; fps=n_frames/s; loop_ms=ms_acc/(n_frames?n_frames:1);
        tps=(gametic-tic_mark)/s; mix_rate=(fr-mix_mark)/s;
        n_frames=0; ms_acc=0; t_mark=t; tic_mark=gametic; mix_mark=fr;
    }
}

// ---------------------------------------------------------------- dibujo
static uint32_t *O; static int W,H;
static void blend(int x,int y,uint32_t c,int a){ if(x<0||y<0||x>=W||y>=H) return; uint32_t p=O[y*W+x];
    int r=((p>>16&255)*(255-a)+(c>>16&255)*a)/255, g=((p>>8&255)*(255-a)+(c>>8&255)*a)/255, b=((p&255)*(255-a)+(c&255)*a)/255; O[y*W+x]=0xFF000000u|r<<16|g<<8|b; }
static void rect(int x,int y,int w,int h,uint32_t c,int a){ for(int j=0;j<h;j++) for(int i=0;i<w;i++) blend(x+i,y+j,c,a); }
static void text(int x,int y,const char *s,uint32_t c){
    for(;*s;s++,x+=8){ int ch=(unsigned char)*s; if(ch<32||ch>126) continue; const unsigned char *gl=VFONT+(ch-32)*14;
        for(int yy=0;yy<14;yy++) for(int xx=0;xx<8;xx++) if(gl[yy]&(1<<(7-xx))) blend(x+xx,y+yy,c,255); }
}
#define COL_T 0xFFE8E8E8u
#define COL_H 0xFFFFD34Du
#define COL_OK 0xFF59E36Eu
#define COL_WARN 0xFFFFC233u
#define COL_BAD 0xFFFF5A5Au
#define COL_DIM 0xFF9AA4B2u
static int ly; static int lx;
static void ln(uint32_t c,const char *f,...){ char b[120]; va_list v; va_start(v,f); vsnprintf(b,sizeof b,f,v); va_end(v); text(lx,ly,b,c); ly+=15; }
static void bar(int x,int y,int w,int v,int max,uint32_t c){ rect(x,y,w,9,0xFF000000u,160); int f=max? (int)((long)w*v/max) : 0; if(f>w) f=w; if(f<0) f=0; rect(x,y,f,9,c,255); }

static const char *WPN[]={"punos","pistola","escopeta","chaingun","lanzacohetes","plasma","BFG","motosierra","superescopeta"};
static const char *AMM[]={"balas","cartuchos","celdas","cohetes"};
static const char *STATE[]={"nivel","intermision","final","pantalla titulo"};

static void page_render(void){
    ln(COL_T,"FPS %.1f   cuadro %.2f ms   render 3D %.2f ms",fps,loop_ms,rv_st_ms);
    ln(COL_T,"salida %dx480   vista 3D %dx403   %s",g_outw,rv_w,g_outw>640?"16:9":"4:3");
    ln(COL_T,"campo de vision horizontal %.0f grados",2*atan2(rv_w/2.0,320.0)*180/M_PI);
    ln(COL_T,"renderer SDL: %s",rend_name);
    ln(COL_T,"segmentos visitados %d   subsectores %d",rv_st_segs,rv_st_subs);
    ln(COL_T,"sprites visibles %d",rv_st_spr);
    ln(COL_T,"mapa: %d sectores  %d lineas  %d segs  %d subsect.",numsectors,numlines,numsegs,numsubsectors);
    ln(COL_T,"texturas %d   flats %d   lumps %d",num_textures,num_flats,numlumps);
    ln(COL_T,"gamma %d   tamano de vista %d   mensajes %s",usegamma,screenblocks,showMessages?"si":"no");
    ln(COL_DIM,"vista 3D con algo dibujado: %d%%",gamestate==GS_LEVEL? RV_NonBlank():0);
}
static void page_player(void){
    player_t *p=&players[consoleplayer]; mobj_t *m=p->mo;
    if(!m){ ln(COL_WARN,"no hay jugador (no estas en un nivel)"); return; }
    ln(COL_T,"posicion x %.1f  y %.1f  z %.1f",m->x/65536.0,m->y/65536.0,m->z/65536.0);
    ln(COL_T,"angulo %.1f grados   altura de ojos %.1f",m->angle*(360.0/4294967296.0),p->viewz/65536.0);
    ln(COL_T,"velocidad x %.2f  y %.2f  (%.2f)",m->momx/65536.0,m->momy/65536.0,hypot(m->momx,m->momy)/65536.0);
    ln(p->health>25?COL_OK:COL_BAD,"salud %d   armadura %d (tipo %d)",p->health,p->armorpoints,p->armortype);
    ln(COL_T,"arma %s",p->readyweapon<9?WPN[p->readyweapon]:"?");
    ln(COL_T,"municion: %s %d  %s %d  %s %d  %s %d",AMM[0],p->ammo[0],AMM[1],p->ammo[1],AMM[2],p->ammo[2],AMM[3],p->ammo[3]);
    ln(COL_T,"llaves: azul %s%s  amarilla %s%s  roja %s%s",p->cards[0]?"T":"-",p->cards[3]?"c":"",p->cards[1]?"T":"-",p->cards[4]?"c":"",p->cards[2]?"T":"-",p->cards[5]?"c":"");
    ln(COL_T,"bajas %d/%d   objetos %d/%d   secretos %d/%d",p->killcount,totalkills,p->itemcount,totalitems,p->secretcount,totalsecret);
    if(m->subsector){ sector_t *s=m->subsector->sector;
        ln(COL_T,"sector %d  suelo %d  techo %d  luz %d  especial %d",(int)(s-sectors),s->floorheight>>16,s->ceilingheight>>16,s->lightlevel,s->special); }
    ln(COL_T,"en el suelo: %s    dios: %s   noclip: %s",m->z<=m->floorz?"si":"no",(p->cheats&CF_GODMODE)?"SI":"no",(p->cheats&CF_NOCLIP)?"SI":"no");
}
static void page_game(void){
    int mons=0,alive=0,items=0,all=0;
    for(thinker_t *th=thinkercap.next; th!=&thinkercap; th=th->next){ all++;
        if(th->function.acp1==(actionf_p1)P_MobjThinker){ mobj_t *o=(mobj_t*)th; if(o->flags&MF_COUNTKILL){ mons++; if(o->health>0) alive++; } if(o->flags&MF_SPECIAL) items++; } }
    ln(COL_T,"estado: %s    nivel E%dM%d   dificultad %d",gamestate<4?STATE[gamestate]:"?",gameepisode,gamemap,(int)gameskill+1);
    ln(COL_T,"tics de juego %d   tics del nivel %d",gametic,leveltime);
    ln(tps>33&&tps<37?COL_OK:COL_WARN,"simulacion %.1f tics/s (debe ser 35)%s",tps,sim_paused?"   PAUSADA":"");
    ln(COL_T,"thinkers %d   monstruos %d (vivos %d)   objetos %d",all,mons,alive,items);
    ln(COL_T,"menu del juego %s   pausa %s",menuactive?"abierto":"cerrado",paused?"si":"no");
    ln(COL_T,"modo de juego: %s",gamemode==commercial?"Doom II":gamemode==retail?"Ultimate Doom":gamemode==registered?"Doom registrado":"shareware");
    ln(COL_T,"mando: %s",pad_name?pad_name:"ninguno");
}
static void page_sound(void){
    const char *d; int a,pl,pr; long fr; snd_stats(&d,&a,&pl,&pr,&fr);
    int mode,mp,mpa; double pos,len; chip_stats(&mode,&mp,&mpa,&pos,&len);
    ln(snd_active()?COL_OK:COL_WARN,"salida de audio: %s%s",d,snd_active()?"":"  (inactiva)");
    ln(mix_rate>37000&&mix_rate<51000?COL_OK:COL_WARN,"mezclador %.0f muestras/s (44100)",mix_rate);
    ln(COL_T,"canales de efectos activos %d / 8",a);
    text(lx,ly,"nivel izq.",COL_DIM); bar(lx+88,ly+2,200,vu_lp,22000,COL_OK); ly+=15;
    text(lx,ly,"nivel der.",COL_DIM); bar(lx+88,ly+2,200,vu_rp,22000,COL_OK); ly+=15;
    ln(COL_T,"volumen efectos %d/15   musica %d/15",snd_SfxVolume,snd_MusicVolume);
    ln(mode==1?COL_OK:COL_WARN,"musica: %s  %s",g_music_name[0]?g_music_name:"-",mode==1?"CTTS 8 bits (pre-renderizada)":mode==2?"sintetizador de respaldo (falta music/)":"nada");
    ln(COL_T,"estado: %s   %02d:%02d / %02d:%02d",mp?(mpa?"en pausa":"sonando"):"parada",(int)pos/60,(int)pos%60,(int)len/60,(int)len%60);
}

// ---------------------------------------------------------------- pruebas
typedef struct { const char *name; int st; char info[90]; } Test;     // st: 0 falla, 1 aviso, 2 bien
static Test T[16]; static int nT=0, tests_run=0;
static void tadd(const char *n,int st,const char *f,...){ if(nT>=16) return; T[nT].name=n; T[nT].st=st; va_list v; va_start(v,f); vsnprintf(T[nT].info,sizeof T[nT].info,f,v); va_end(v); nT++; }
static void run_selftest(void){
    nT=0; tests_run=1; player_t *p=&players[consoleplayer];
    tadd("WAD",(W_CheckNumForName("PLAYPAL")>=0&&W_CheckNumForName("COLORMAP")>=0)?2:0,"%d lumps, PLAYPAL/COLORMAP %s",numlumps,(W_CheckNumForName("PLAYPAL")>=0)?"ok":"faltan");
    tadd("Texturas",(num_textures>100&&num_flats>50)?2:0,"%d texturas, %d flats",num_textures,num_flats);
    if(gamestate==GS_LEVEL){
        tadd("Mapa",(numsectors>0&&numsegs>0&&numsubsectors>0)?2:0,"%d sectores, %d segs, %d subsectores",numsectors,numsegs,numsubsectors);
        int nb=RV_NonBlank(); tadd("Render 3D",nb>=20?2:(nb>0?1:0),"%d%% de la vista dibujada, %.2f ms, %d segs",nb,rv_st_ms,rv_st_segs);
        tadd("Jugador",(p->mo&&p->health>0&&p->mo->subsector)?2:0,"%s, salud %d",p->mo?"existe":"no existe",p->health);
    } else { tadd("Mapa",1,"no estas en un nivel"); tadd("Render 3D",1,"entra a un nivel para probarlo"); tadd("Jugador",1,"entra a un nivel para probarlo"); }
    tadd("Simulacion",sim_paused?1:((tps>33&&tps<37)?2:(tps>0?1:0)),"%.1f tics/s",tps);
    tadd("Audio",snd_active()?2:1,"%s",snd_active()?"salida activa":"sin salida (DOOM_NOSOUND o sin dispositivo)");
    tadd("Mezclador",(mix_rate>37000&&mix_rate<51000)?2:(snd_active()?0:1),"%.0f muestras/s",mix_rate);
    int h=dbg_sfx(sfx_pistol,128); const char *d; int a,pl,pr; long fr; snd_stats(&d,&a,&pl,&pr,&fr);
    tadd("Efecto de sonido",h>0&&a>0?2:(snd_active()?0:1),"%s (canales %d)",h>0?"disparo reproducido":"no se pudo",a);
    int mode,mp,mpa; double pos,len; chip_stats(&mode,&mp,&mpa,&pos,&len);
    tadd("Musica",mode==1?2:1,"%s",mode==1?"CTTS 8 bits":mode==2?"sintetizador de respaldo":"sin musica");
    tadd("Mando",1,"%s",pad_name?pad_name:"no hay (opcional)");
}
#define NITEMS 10
static const char *ITEM[NITEMS]={"Autodiagnostico","Sonido: disparo (centro)","Sonido: izquierda","Sonido: derecha","Musica: pausar / seguir","Simulacion: pausar / seguir","Jugador: dios (G)","Jugador: noclip (atravesar)","Jugador: curar y recargar","Pantalla: alternar 4:3 / 16:9"};
static void run_item(int i){
    player_t *p=&players[consoleplayer]; result[0]=0;
    switch(i){
    case 0: run_selftest(); snprintf(result,sizeof result,"diagnostico hecho"); break;
    case 1: dbg_sfx(sfx_pistol,128); snprintf(result,sizeof result,"disparo al centro"); break;
    case 2: dbg_sfx(sfx_pistol,0); snprintf(result,sizeof result,"disparo a la izquierda"); break;
    case 3: dbg_sfx(sfx_pistol,255); snprintf(result,sizeof result,"disparo a la derecha"); break;
    case 4: { static int mp=0; mp=!mp; if(mp) S_PauseSound(); else S_ResumeSound(); snprintf(result,sizeof result,"musica %s",mp?"en pausa":"sonando"); } break;
    case 5: sim_paused=!sim_paused; snprintf(result,sizeof result,"simulacion %s",sim_paused?"pausada":"corriendo"); break;
    case 6: p->cheats^=CF_GODMODE; snprintf(result,sizeof result,"dios %s",(p->cheats&CF_GODMODE)?"activado":"desactivado"); break;
    case 7: p->cheats^=CF_NOCLIP; snprintf(result,sizeof result,"noclip %s",(p->cheats&CF_NOCLIP)?"activado":"desactivado"); break;
    case 8: if(p->mo){ p->health=p->mo->health=100; p->armorpoints=100; p->armortype=1; for(int k=0;k<NUMAMMO;k++) p->ammo[k]=p->maxammo[k]; snprintf(result,sizeof result,"salud, armadura y municion al maximo"); } else snprintf(result,sizeof result,"no hay jugador"); break;
    case 9: if(cb_wide){ cb_wide(); snprintf(result,sizeof result,"proporcion cambiada"); } break;
    }
}
static void page_tests(void){
    for(int i=0;i<NITEMS;i++){ char b[80]; snprintf(b,sizeof b,"%s %s",i==sel?">":" ",ITEM[i]); text(lx,ly,b,i==sel?COL_H:COL_T); ly+=15; }
    ly+=4; if(result[0]) { ln(COL_H,"%s",result); }
    if(tests_run){ int ok=0,aw=0,bad=0; for(int i=0;i<nT;i++){ if(T[i].st==2) ok++; else if(T[i].st==1) aw++; else bad++; }
        ln(bad?COL_BAD:aw?COL_WARN:COL_OK,"RESUMEN: %d bien, %d avisos, %d fallos",ok,aw,bad);
        for(int i=0;i<nT;i++) ln(T[i].st==2?COL_OK:T[i].st==1?COL_WARN:COL_BAD,"%-5s %-17s %s",T[i].st==2?"BIEN":T[i].st==1?"AVISO":"FALLA",T[i].name,T[i].info); }
}

// ---------------------------------------------------------------- teclas del menu (sym = tecla de SDL). Devuelve 1 si la consumio.
int DBG_Key(int sym){
    if(!dbg_on) return 0;
    switch(sym){
    case SDLK_ESCAPE: dbg_on=0; break;
    case SDLK_LEFT: page=(page+4)%5; break;
    case SDLK_RIGHT: page=(page+1)%5; break;
    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: page=sym-SDLK_1; break;
    case SDLK_UP: if(page==4) sel=(sel+NITEMS-1)%NITEMS; break;
    case SDLK_DOWN: if(page==4) sel=(sel+1)%NITEMS; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if(page==4) run_item(sel); break;
    case SDLK_g: if(players[consoleplayer].mo) run_item(6); break;
    default: break;
    }
    return 1;
}

void DBG_Draw(uint32_t *out,int w,int h){
    if(!dbg_on) return;
    O=out; W=w; H=h; g_outw=w;
    int px=(w-640)/2+8, pw=620;
    rect(px-4,6,pw,h-12,0xFF05080Cu,205);
    static const char *TAB[5]={"1 RENDER","2 JUGADOR","3 JUEGO","4 SONIDO","5 PRUEBAS"};
    int x=px;
    for(int i=0;i<5;i++){ text(x,10,TAB[i],i==page?COL_H:COL_DIM); if(i==page) rect(x,26,(int)strlen(TAB[i])*8,2,COL_H,255); x+=(int)strlen(TAB[i])*8+16; }
    lx=px; ly=38;
    switch(page){ case 0: page_render(); break; case 1: page_player(); break; case 2: page_game(); break; case 3: page_sound(); break; default: page_tests(); }
    text(px,h-26,"Izq/Der u 1-5: paginas   Arriba/Abajo + Enter: acciones (5)   Ctrl+D o Esc: cerrar",COL_DIM);
}

// ---------------------------------------------------------------- captura de pantalla (PNG sin compresion)
static unsigned crc_t[256]; static int crc_ok=0;
static unsigned crc32u(unsigned c,const unsigned char *b,size_t n){
    if(!crc_ok){ for(unsigned i=0;i<256;i++){ unsigned r=i; for(int k=0;k<8;k++) r=r&1? 0xEDB88320u^(r>>1) : r>>1; crc_t[i]=r; } crc_ok=1; }
    c=~c; while(n--) c=crc_t[(c^*b++)&255]^(c>>8); return ~c;
}
static void be32(unsigned char *p,unsigned v){ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void chunk(FILE *f,const char *type,const unsigned char *d,unsigned n){
    unsigned char h[8]; be32(h,n); memcpy(h+4,type,4); fwrite(h,1,8,f);
    unsigned c=crc32u(0,(unsigned char*)type,4); if(n){ fwrite(d,1,n,f); c=crc32u(c,d,n); } unsigned char t[4]; be32(t,c); fwrite(t,1,4,f);
}
// guarda ARGB8888 como PNG RGB (zlib con bloques "stored")
int DBG_SavePNG(const uint32_t *px,int w,int h,const char *path){
    FILE *f=fopen(path,"wb"); if(!f) return 0;
    size_t row=1+(size_t)w*3, raw=row*h; unsigned char *buf=malloc(raw); if(!buf){ fclose(f); return 0; }
    for(int y=0;y<h;y++){ unsigned char *r=buf+y*row; *r++=0; for(int x=0;x<w;x++){ uint32_t c=px[y*w+x]; *r++=c>>16; *r++=c>>8; *r++=c; } }
    size_t nb=(raw+65534)/65535, zl=2+raw+nb*5+4; unsigned char *z=malloc(zl); if(!z){ free(buf); fclose(f); return 0; }
    size_t o=0; z[o++]=0x78; z[o++]=0x01; unsigned a=1,b=0;
    for(size_t i=0;i<raw;i++){ a=(a+buf[i])%65521; b=(b+a)%65521; }
    for(size_t i=0;i<raw;){ size_t n=raw-i>65535?65535:raw-i; z[o++]= (i+n>=raw); z[o++]=n&255; z[o++]=n>>8; z[o++]=~n&255; z[o++]=(~n>>8)&255; memcpy(z+o,buf+i,n); o+=n; i+=n; }
    be32(z+o,(b<<16)|a); o+=4;
    unsigned char sig[8]={0x89,'P','N','G',13,10,0x1a,10}; fwrite(sig,1,8,f);
    unsigned char ih[13]; be32(ih,w); be32(ih+4,h); ih[8]=8; ih[9]=2; ih[10]=0; ih[11]=0; ih[12]=0; chunk(f,"IHDR",ih,13);
    chunk(f,"IDAT",z,(unsigned)o); chunk(f,"IEND",NULL,0);
    free(buf); free(z); fclose(f); return 1;
}
// Ctrl+P: guarda en <carpeta de datos>/screenshots/doom_AAAAMMDD_HHMMSS.png ; devuelve la ruta (o NULL)
const char *DBG_Screenshot(const uint32_t *px,int w,int h){
    static char path[900]; char dir[800]; char *pref=SDL_GetPrefPath("COS-Mods","DoomProject");
    snprintf(dir,sizeof dir,"%sscreenshots",pref?pref:""); if(pref) SDL_free(pref);
    DOOM_MKDIR(dir);
    time_t t=time(NULL); struct tm *tm=localtime(&t); char ts[32]; strftime(ts,sizeof ts,"%Y%m%d_%H%M%S",tm);
    snprintf(path,sizeof path,"%s/doom_%s.png",dir,ts);
    return DBG_SavePNG(px,w,h,path)? path : NULL;
}
// para pruebas sin ventana (--dbg N): abre el menu en la pagina N (1-5); en la 5 corre el autodiagnostico
void DBG_Open(int pg){ dbg_on=1; page=(pg>=1&&pg<=5)? pg-1 : 0; if(page==4) run_selftest(); }
