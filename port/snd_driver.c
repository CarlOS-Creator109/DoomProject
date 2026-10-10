// snd_driver.c - i_sound.c de Doom Project.
// Mezcla en un solo flujo PCM (44.1 kHz, estéreo, 16 bits):  música pre-renderizada (chip.c)  +  efectos DMX del WAD
// (voces de monstruos, armas, puertas...) encima. El flujo sale por un pequeño driver que lo manda a FFmpeg, y FFmpeg
// lo entrega al audio del sistema (PulseAudio/PipeWire o ALSA).  Si no hay ffmpeg, usa SDL como respaldo.
//   DOOM_AUDIO=ffmpeg|sdl|none      (por defecto: ffmpeg si existe, si no sdl)
//   DOOM_FFMPEG_OUT="-f alsa default"   salida de ffmpeg a medida (p. ej. "-f wav salida.wav" para grabar)
#include <SDL2/SDL.h>
#include "compat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#include "doomdef.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"
#define NCH 8
#define OUTRATE 44100
typedef struct { const unsigned char *s; int n; double pos, step; int lv, rv, id; int active; } Chan;
static Chan ch[NCH];
static int nextid = 1;
void chip_next(float *l, float *r);
extern int g_vm_on; void vm_push(float,float); int vm_start(const char*); void vm_stop(void);
static const char *video_dir=NULL;
void snd_video_music(const char *dir){ video_dir=dir; }
static SDL_mutex *mx;
void snd_lock(void){ if(mx) SDL_LockMutex(mx); }
void snd_unlock(void){ if(mx) SDL_UnlockMutex(mx); }

static volatile int vu_l=0, vu_r=0; static volatile long mixed_frames=0; static const char *drv_name="ninguno";
int snd_stats(const char **drv,int *active,int *pl,int *pr,long *frames){
    int n=0; snd_lock(); for(int c=0;c<NCH;c++) if(ch[c].active) n++; snd_unlock();
    *drv=drv_name; *active=n; *pl=vu_l; *pr=vu_r; *frames=mixed_frames; return n; }
static void mix(short *o, int frames){           // el mismo flujo para cualquiera de los dos destinos
    snd_lock(); int pkl=0,pkr=0;
    for(int i=0;i<frames;i++){
        int l=0,r=0;
        for(int c=0;c<NCH;c++) if(ch[c].active){
            Chan *k=&ch[c]; int ip=(int)k->pos;
            if(ip>=k->n){ k->active=0; continue; }
            int v=((int)k->s[ip]-128)*256;
            l+=v*k->lv/127; r+=v*k->rv/127;
            k->pos+=k->step;
        }
        l/=3; r/=3;
        float ml,mr; chip_next(&ml,&mr); if(g_vm_on) vm_push(ml,mr); l+=(int)(ml*32767); r+=(int)(mr*32767);
        if(l>32767)l=32767; if(l<-32768)l=-32768; if(r>32767)r=32767; if(r<-32768)r=-32768;
        o[2*i]=(short)l; o[2*i+1]=(short)r; if(l>pkl)pkl=l; if(-l>pkl)pkl=-l; if(r>pkr)pkr=r; if(-r>pkr)pkr=-r;
    }
    vu_l=pkl; vu_r=pkr; mixed_frames+=frames;
    snd_unlock();
}

// ---- backend SDL (respaldo) ----
static SDL_AudioDeviceID dev = 0;
static void sdl_cb(void *u, Uint8 *stream, int len){ (void)u; mix((short*)stream, len/4); }

// ---- backend FFmpeg: hilo que escribe PCM crudo en la tubería de ffmpeg ----
static FILE *ff = NULL; static SDL_Thread *thr = NULL; static volatile int running = 0;
static int ff_has(const char *name){ return doom_ff_has("-devices",name); }
static int ff_thread(void *u){
    (void)u; short buf[441*2]; long written=0; Uint64 t0=SDL_GetPerformanceCounter(), fq=SDL_GetPerformanceFrequency();
    while(running){
        double el=(double)(SDL_GetPerformanceCounter()-t0)/fq;
        if(written > el*OUTRATE + OUTRATE*0.06){ SDL_Delay(2); continue; }   // no adelantarse más de 60 ms (latencia baja)
        mix(buf,441);
        if(!ff){ written+=441; continue; }
        if(fwrite(buf,sizeof buf,1,ff)!=1){ fprintf(stderr,"[AUDIO] ffmpeg cerró la tubería; sin sonido\n"); running=0; break; }
        fflush(ff); written+=441;
    }
    return 0;
}
static int ff_start(void){
    const char *out=getenv("DOOM_FFMPEG_OUT"); char outbuf[160];
    if(!out){
        if(ff_has("pulse")) out="-f pulse -buffer_duration 50 -name DoomProject DoomProject";
        else if(ff_has("alsa")) out="-f alsa -buffer_size 2048 default";
        else return 0;
    }
    char cmd[640]; snprintf(cmd,sizeof cmd,"%s -hide_banner -loglevel error -y -f s16le -ar %d -ac 2 -fflags nobuffer -flags low_delay -i - %s",doom_ffbin(),OUTRATE,out); (void)outbuf;
    signal(SIGPIPE,SIG_IGN);
    ff=DOOM_POPEN_W(cmd); if(!ff) return 0;
    running=1; thr=SDL_CreateThread(ff_thread,"audio-ffmpeg",NULL); return thr!=NULL;
}

void I_InitSound(void){
    const char *m=getenv("DOOM_AUDIO"); int quiet=getenv("DOOM_NOSOUND")!=NULL;
    if(quiet && !video_dir) return;
    mx=SDL_CreateMutex();
    if(video_dir) vm_start(video_dir);
    if(quiet || (m && !strcmp(m,"none") && video_dir)){ drv_name="silencio (solo mezcla, sin salida)"; running=1; thr=SDL_CreateThread(ff_thread,"audio-null",NULL); return; }   // sin salida de audio: solo mezcla (para el video)
    if(m && !strcmp(m,"none")) return;
    #if defined(_WIN32) || defined(__APPLE__)
    int ffdef=0;      // ffmpeg no tiene salida directa a la tarjeta de sonido aqui: mismo mezclador, salida SDL
#else
    int ffdef=1;
#endif
    if((!m && ffdef) || (m && !strcmp(m,"ffmpeg"))){ if(ff_start()){ printf("[AUDIO] FFmpeg -> audio del sistema\n"); drv_name="ffmpeg -> audio del sistema"; return; } if(m) fprintf(stderr,"[AUDIO] no pude iniciar ffmpeg\n"); }
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) return;
    SDL_AudioSpec w={0},h; w.freq=OUTRATE; w.format=AUDIO_S16SYS; w.channels=2; w.samples=1024; w.callback=sdl_cb;
    dev=SDL_OpenAudioDevice(NULL,0,&w,&h,0);
    if(dev){ SDL_PauseAudioDevice(dev,0); printf("[AUDIO] SDL\n"); drv_name="SDL"; }
}
void I_ShutdownSound(void){
    if(running){ running=0; SDL_WaitThread(thr,NULL); thr=NULL; }
    vm_stop();
    if(ff){ DOOM_PCLOSE(ff); ff=NULL; }
    if(dev){ SDL_CloseAudioDevice(dev); dev=0; }
}
int snd_active(void){ return running || dev; }
void I_UpdateSound(void){} void I_SubmitSound(void){}
void I_SetChannels(){}
int  I_GetSfxLumpNum(sfxinfo_t *s){          // ademas carga el sonido en memoria (el original DMX lo hacia aqui)
    char name[20]; sprintf(name,"ds%s",s->name); int l=W_CheckNumForName(name);
    if(l>=0 && !s->data) s->data=W_CacheLumpNum(l,PU_STATIC);
    return l;
}
static void setvol(Chan *k,int vol,int sep){
    k->lv=vol*(255-sep)/255*2; k->rv=vol*sep/255*2;
    if(k->lv>127)k->lv=127; if(k->rv>127)k->rv=127;
}
int  I_StartSound(int id,int vol,int sep,int pitch,int priority){
    if(getenv("DOOM_SNDDEBUG")) fprintf(stderr,"[SFX] id=%d vol=%d sep=%d data=%p\n",id,vol,sep,S_sfx[id].data);
    (void)pitch;(void)priority;
    const unsigned char *d=(const unsigned char*)S_sfx[id].data; if(!d||!snd_active()) return 0;
    int rate=d[2]|(d[3]<<8), n=d[4]|(d[5]<<8)|(d[6]<<16)|(d[7]<<24); if(n>8+16) n-=32;
    if(n<=0) return 0;
    snd_lock();
    int c=-1; for(int i=0;i<NCH;i++) if(!ch[i].active){ c=i; break; }
    if(c<0) c=0;
    Chan *k=&ch[c]; k->s=d+8+16; k->n=n; k->pos=0; k->step=(double)(rate?rate:11025)/OUTRATE; setvol(k,vol,sep);
    k->id=nextid++; k->active=1; int h=k->id;
    if(getenv("DOOM_SNDDEBUG")) fprintf(stderr,"[SFX] canal %d n=%d rate=%d lv=%d rv=%d\n",c,n,rate,k->lv,k->rv);
    snd_unlock();
    return h;
}
void I_StopSound(int h){ snd_lock(); for(int i=0;i<NCH;i++) if(ch[i].active&&ch[i].id==h) ch[i].active=0; snd_unlock(); }
int  I_SoundIsPlaying(int h){ int r=0; snd_lock(); for(int i=0;i<NCH;i++) if(ch[i].active&&ch[i].id==h) r=1; snd_unlock(); return r; }
void I_UpdateSoundParams(int h,int vol,int sep,int pitch){ (void)pitch; snd_lock(); for(int i=0;i<NCH;i++) if(ch[i].active&&ch[i].id==h) setvol(&ch[i],vol,sep); snd_unlock(); }
