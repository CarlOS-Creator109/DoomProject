// chip.c - música de Doom (formato MUS) con sintetizador de instrumentos de 8 bits.
// Los osciladores, el LFSR de ruido de la NES, la envolvente ADSR (5 ms / 30 ms / 0.8 / 30 ms) y la batería
// (kick, snare, hihat, tom, crash, ride, clap) son los del motor CTTS/GemTunes (ctts.c) de Carlos,
// pasados a tiempo real. Añadidos de estilo 8 bits: ciclo de trabajo 12.5/25/50 % y triángulo de 4 bits como la NES.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#define SR 44100.0
#define PI 3.14159265358979323846
#define MAXV 28
char g_music_name[16];
__attribute__((weak)) void snd_lock(void){}
__attribute__((weak)) void snd_unlock(void){}

typedef enum { W_SQ, W_TRI, W_SAW, W_HEX, W_NOISE, W_NOISES } Wave;
typedef struct { Wave w; double duty, tau, atk, rel, vib; int bits; } Patch;   // tau>0: caída exponencial mientras suena

static Patch patch_of(int p){
    Patch d={W_SQ,0.5,0,0.005,0.03,0,0};
    int f=p>>3;
    switch(f){
    case 0: d.duty=0.25; d.tau=0.45; break;                    // pianos
    case 1: d.w=W_TRI; d.tau=0.5; d.bits=4; break;             // percusión cromática
    case 2: d.duty=0.5; break;                                 // órganos
    case 3: d.w=W_SQ; d.duty=0.125; d.tau=0.6; break;          // guitarras
    case 4: d.w=W_TRI; d.bits=4; d.tau=1.2; break;             // bajos (triángulo NES)
    case 5: d.w=W_SAW; d.atk=0.05; d.rel=0.08; d.vib=0.15; break; // cuerdas
    case 6: d.duty=0.5; d.atk=0.02; break;                     // metales
    case 7: d.w=W_SAW; d.atk=0.015; d.vib=0.1; break;          // lengüetas
    case 8: d.w=W_TRI; d.bits=4; d.vib=0.1; break;             // flautas
    case 9: d.duty=0.25; d.vib=0.2; break;                     // lead cuadrado
    case 10: d.w=W_SAW; d.vib=0.2; break;                      // lead sierra
    case 11: d.w=W_TRI; d.bits=4; d.atk=0.08; d.rel=0.12; break; // pads
    case 12: d.w=W_HEX; d.tau=0.7; break;                      // fx
    case 13: d.w=W_HEX; d.tau=0.5; break;                      // étnicos
    case 14: d.w=W_NOISES; d.tau=0.12; break;                  // percusivos
    default: d.w=W_NOISE; d.tau=0.3; break;                    // sfx
    }
    if(p>=29 && p<=31){ d.w=W_SQ; d.duty=0.125; d.tau=0; }     // guitarra distorsionada: sostenida (E1M1...)
    if(p>=32 && p<=39){ d.w=W_TRI; d.bits=4; d.tau=(p==33||p==34)?0:1.2; }
    if(p>=38 && p<=39){ d.w=W_SQ; d.duty=0.5; d.tau=0; }       // bajos sintéticos
    return d;
}
static double lfsr_step(unsigned *st,int tap){ unsigned fb=(*st&1)^((*st>>tap)&1); *st=(*st>>1)|(fb<<14); return (*st&1)?-1.0:1.0; }
static double clamp1(double s){ return s<-1?-1:(s>1?1:s); }
typedef struct { double a,b,c,y1,y2; } GRes;
static void gres_init(GRes *r,double f,double bw){ double rr=exp(-PI*bw/SR),th=2*PI*f/SR; r->b=2*rr*cos(th); r->c=-rr*rr; r->a=1-r->b-r->c; r->y1=r->y2=0; }
static double gres_step(GRes *r,double x){ double y=r->a*x+r->b*r->y1+r->c*r->y2; r->y2=r->y1; r->y1=y; return y; }

// ---- batería (DrumKit de CTTS) ----
typedef enum { D_KICK,D_SNARE,D_HAT,D_TOM,D_CRASH,D_RIDE,D_CLAP } DK;
typedef struct { int on; DK k; double t,dur,vol,freq,ph; unsigned sa,sb; GRes r[6]; int nr; double tau; } Drum;
static void drum_init(Drum *d,int note,double vol){
    memset(d,0,sizeof(*d)); d->on=1; d->vol=vol; d->sa=d->sb=1;
    switch(note){
    case 35: case 36: d->k=D_KICK; d->freq=60; d->dur=0.25; break;
    case 38: case 40: case 37: d->k=D_SNARE; d->freq=190; d->dur=0.2; break;
    case 39: d->k=D_CLAP; d->dur=0.15; gres_init(&d->r[0],1200,800); break;
    case 42: case 44: d->k=D_HAT; d->dur=0.06; d->tau=0.02; break;
    case 46: d->k=D_HAT; d->dur=0.3; d->tau=0.1; break;
    case 41: case 43: case 45: case 47: case 48: case 50: { static const double F[]={80,100,120,140,170,200}; d->k=D_TOM; d->dur=0.35;
        d->freq=F[(note==41)?0:(note==43)?1:(note==45)?2:(note==47)?3:(note==48)?4:5]; break; }
    case 49: case 52: case 55: case 57: { static const double R[6]={1,1.44,1.86,2.29,2.66,3.32}; d->k=D_CRASH; d->freq=300; d->dur=1.2; d->tau=0.7; d->nr=6;
        for(int i=0;i<6;i++) gres_init(&d->r[i],d->freq*R[i],400); break; }
    case 51: case 53: case 59: { static const double R[4]={1.4,2.6,3.3,4.8}; d->k=D_RIDE; d->freq=400; d->dur=0.8; d->tau=0.4; d->nr=5;
        gres_init(&d->r[0],d->freq*2,40); for(int i=0;i<4;i++) gres_init(&d->r[1+i],d->freq*R[i],300); break; }
    default: d->k=D_HAT; d->dur=0.08; d->tau=0.03; break;
    }
}
static double drum_next(Drum *d){
    double t=d->t; d->t+=1.0/SR; if(t>d->dur){ d->on=0; return 0; }
    switch(d->k){
    case D_KICK: { double dur=d->dur, at=fmax(dur/3,0.12), f=d->freq*0.55+(d->freq*3-d->freq*0.55)*exp(-t/0.045); d->ph+=f/SR; if(d->ph>=1) d->ph-=1;
        double nz=lfsr_step(&d->sa,1); return clamp1((sin(2*PI*d->ph)*exp(-t/at)+nz*exp(-t/0.004)*0.35)*d->vol); }
    case D_SNARE: { double nt=fmax(d->dur/2.5,0.09); d->ph+=d->freq/SR; if(d->ph>=1) d->ph-=1;
        return clamp1((sin(2*PI*d->ph)*exp(-t/0.07)*0.4+lfsr_step(&d->sa,1)*exp(-t/nt)*0.85)*d->vol); }
    case D_HAT: { double xa=lfsr_step(&d->sa,1),xb=lfsr_step(&d->sb,6); return clamp1((xa+xb)*0.5*exp(-t/d->tau)*d->vol); }
    case D_TOM: { double f=d->freq*0.85+(d->freq*1.8-d->freq*0.85)*exp(-t/0.06); d->ph+=f/SR; if(d->ph>=1) d->ph-=1;
        return clamp1(sin(2*PI*d->ph)*exp(-t/fmax(d->dur/2.5,0.18))*d->vol); }
    case D_CRASH: { double nz=(lfsr_step(&d->sa,1)+lfsr_step(&d->sb,6))*0.5,s=0; for(int i=0;i<6;i++) s+=gres_step(&d->r[i],nz);
        return clamp1((nz*0.45+s/6*1.6)*exp(-t/d->tau)*d->vol); }
    case D_RIDE: { double nz=lfsr_step(&d->sa,1),by=gres_step(&d->r[0],nz),s=0; for(int i=0;i<4;i++) s+=gres_step(&d->r[1+i],nz);
        return clamp1((by*0.9*exp(-t/(d->tau*1.3))+s/4*0.55*exp(-t/d->tau)+nz*0.12*exp(-t/d->tau))*d->vol); }
    case D_CLAP: { double nz=lfsr_step(&d->sa,1),y=gres_step(&d->r[0],nz),env=0; static const double BS[4]={0,0.012,0.024,0.038};
        for(int k=0;k<4;k++){ double dt=t-BS[k]; if(dt>=0) env=fmax(env,exp(-dt/0.008)); }
        if(t>=0.045) env=fmax(env,0.6*exp(-(t-0.045)/0.15)); return clamp1(y*env*d->vol*0.8); }
    }
    return 0;
}

// ---- voces melódicas ----
typedef struct { int on,ch,note,held; Patch p; double ph,inc,t,relt,vel; unsigned ns; double nacc; double last; } Voice;
static Voice V[MAXV]; static Drum D[8];
static struct { int prog,vol,bend; } CH[16];
static uint8_t *song=NULL; static int song_len=0, pos=0, playing=0, looping=0, paused=0;
static long wait_samples=0; static double tick_acc=0; static double gain=0.5;
static int start_ofs=0;

static void ch_reset(void){ for(int i=0;i<16;i++){ CH[i].prog=0; CH[i].vol=100; CH[i].bend=128; } }
static void all_off(void){ memset(V,0,sizeof(V)); memset(D,0,sizeof(D)); }
static double note_hz(int n,int bend){ return 440.0*pow(2.0,(n-69+((bend-128)/128.0)*2.0)/12.0); }
static void note_on(int ch,int note,int vel){
    if(ch==15){ for(int i=0;i<8;i++) if(!D[i].on){ drum_init(&D[i],note,vel/127.0*0.9); return; } drum_init(&D[0],note,vel/127.0*0.9); return; }
    int s=-1; for(int i=0;i<MAXV;i++) if(!V[i].on){ s=i; break; }
    if(s<0){ double mt=-1; for(int i=0;i<MAXV;i++) if(V[i].t>mt){ mt=V[i].t; s=i; } }
    Voice *v=&V[s]; memset(v,0,sizeof(*v)); v->on=1; v->ch=ch; v->note=note; v->held=1; v->p=patch_of(CH[ch].prog); v->vel=vel/127.0; v->ns=1;
}
static void note_off(int ch,int note){ for(int i=0;i<MAXV;i++) if(V[i].on&&V[i].held&&V[i].ch==ch&&V[i].note==note){ V[i].held=0; } }
static double voice_next(Voice *v){
    Patch *p=&v->p; double f=note_hz(v->note,CH[v->ch].bend);
    if(p->vib>0 && v->t>0.15) f*=pow(2.0,p->vib*sin(2*PI*5.5*(v->t-0.15))/12.0);
    v->ph+=f/SR; int wrap=0; if(v->ph>=1){ v->ph-=1; wrap=1; }
    double s;
    switch(p->w){
    case W_SQ: s=v->ph<p->duty? 1:-1; break;
    case W_TRI: s=4*(v->ph<0.5?v->ph:1-v->ph)-1; if(p->bits){ double q=(1<<p->bits)-1; s=floor((s*0.5+0.5)*q+0.5)/q*2-1; } break;
    case W_SAW: s=2*v->ph-1; break;
    case W_HEX: { double th=v->ph*2*PI,seg=PI/3,rel=fmod(th,seg),half=seg/2; s=cos(half)/cos(rel-half)*cos(th); break; }
    case W_NOISE: if(wrap||v->t==0) v->last=lfsr_step(&v->ns,1); s=v->last; break;
    default: if(wrap||v->t==0) v->last=lfsr_step(&v->ns,6); s=v->last; break;
    }
    double t=v->t, A=p->atk, Dd=0.03, S=0.8, e;
    if(t<A) e=t/A; else if(t<A+Dd) e=1-(1-S)*((t-A)/Dd); else e=S;
    if(p->tau>0) e*=exp(-t/p->tau);
    v->t+=1.0/SR;
    if(!v->held){ v->relt+=1.0/SR; if(v->relt>=p->rel){ v->on=0; return 0; } e*=1-v->relt/p->rel; }
    return s*e*v->vel*(CH[v->ch].vol/127.0);
}
static int rd(void){ return pos<song_len? song[pos++] : 0; }
static void seq_events(void){          // procesa eventos hasta el siguiente retardo
    for(;;){
        if(pos>=song_len){ playing=0; return; }
        int b=rd(), last=b&0x80, type=(b>>4)&7, ch=b&15;
        switch(type){
        case 0: note_off(ch,rd()&127); break;
        case 1: { int n=rd(); int vel=-1; if(n&0x80) vel=rd()&127; n&=127; static int lastvel[16]; if(vel<0) vel=lastvel[ch]; else lastvel[ch]=vel; note_on(ch,n,vel); break; }
        case 2: CH[ch].bend=rd(); break;
        case 3: rd(); break;
        case 4: { int c=rd(), val=rd(); if(c==0) CH[ch].prog=val&127; else if(c==3) CH[ch].vol=val&127; break; }
        case 5: break;
        case 6: if(looping){ pos=start_ofs; ch_reset(); all_off(); } else { playing=0; return; } break;
        default: rd(); break;
        }
        if(last){ long t=0; int x; do{ x=rd(); t=t*128+(x&127); }while(x&0x80); wait_samples=(long)(t*SR/140.0); if(wait_samples>0) return; }
    }
}
static float chip_mono(void){
    if(!song||!playing||paused) return 0;
    while(wait_samples<=0 && playing){ seq_events(); if(wait_samples<=0 && !playing) return 0; }
    wait_samples--;
    double m=0;
    for(int i=0;i<MAXV;i++) if(V[i].on) m+=voice_next(&V[i]);
    for(int i=0;i<8;i++) if(D[i].on) m+=drum_next(&D[i])*1.0;
    m*=0.12*gain;
    return (float)(m>1?1:(m<-1?-1:m));
}
void pcm_stop(void); void pcm_free_all(void);
// ---- interfaz i_sound.h ----
void I_InitMusic(void){} void I_ShutdownMusic(void){}
void I_SetMusicVolume(int v){ snd_lock(); gain=v/127.0*1.6; extern double *pcm_gain_p; *pcm_gain_p=v/127.0*0.9; snd_unlock(); }
void I_PauseSong(int h){ (void)h; paused=1; } void I_ResumeSong(int h){ (void)h; paused=0; }
int I_RegisterSong(void *d){
    const uint8_t *b=(const uint8_t*)d; if(!b||memcmp(b,"MUS\x1a",4)) return 0;
    int len=b[4]|(b[5]<<8), ofs=b[6]|(b[7]<<8); uint8_t *c=malloc(len+ofs+8); if(!c) return 0; memcpy(c,b,len+ofs); 
    snd_lock(); free(song); song=c; song_len=len+ofs; start_ofs=ofs; playing=0; snd_unlock(); return 1;
}
void I_PlaySong(int h,int l){ (void)h; if(!song) return; snd_lock(); extern int pcm_try(const char*); if(pcm_try(g_music_name)){ looping=l; paused=0; playing=1; snd_unlock(); return; } ch_reset(); all_off(); pos=start_ofs; looping=l; wait_samples=0; paused=0; playing=1; snd_unlock(); }
void I_StopSong(int h){ (void)h; snd_lock(); playing=0; all_off(); pcm_stop(); snd_unlock(); }
void I_UnRegisterSong(int h){ (void)h; snd_lock(); playing=0; all_off(); free(song); song=NULL; pcm_free_all(); snd_unlock(); }

// ---- música pre-renderizada: MUS -> MIDI -> .rbsong -> WAV con el motor CTTS (ver tools/build_music.py) ----
#include <stdio.h>
#include <ctype.h>
static int16_t *pcm=NULL; static long pcm_n=0, pcm_pos=0; static int pcm_ch=2; static double pcm_gain=0.8;
static void pcm_free(void){ free(pcm); pcm=NULL; pcm_n=0; }
static int pcm_load(const char *name){
    const char *dir=getenv("DOOM_MUSIC_DIR"); if(!dir) dir="music";
    char path[512], up[16]; int i=0; for(;name[i]&&i<15;i++) up[i]=toupper((unsigned char)name[i]); up[i]=0;
    snprintf(path,sizeof path,"%s/D_%s.wav",dir,up);
    FILE *f=fopen(path,"rb"); if(!f) return 0;
    uint8_t h[12]; if(fread(h,1,12,f)!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)){ fclose(f); return 0; }
    int ch=0,bits=0; uint32_t rate=0; uint8_t ck[8];
    while(fread(ck,1,8,f)==8){
        uint32_t sz=ck[4]|(ck[5]<<8)|(ck[6]<<16)|((uint32_t)ck[7]<<24);
        if(!memcmp(ck,"fmt ",4)){ uint8_t b[16]; if(sz<16||fread(b,1,16,f)!=16) break; ch=b[2]|(b[3]<<8); rate=b[4]|(b[5]<<8)|(b[6]<<16)|((uint32_t)b[7]<<24); bits=b[14]|(b[15]<<8); if(sz>16) fseek(f,sz-16,SEEK_CUR); }
        else if(!memcmp(ck,"data",4)){
            if(bits!=16||(ch!=1&&ch!=2)||rate!=44100){ fclose(f); return 0; }
            long n=sz/2; int16_t *b=malloc(n*2); if(!b){ fclose(f); return 0; } n=fread(b,2,n,f); fclose(f);
            pcm_free(); pcm=b; pcm_n=n/ch; pcm_pos=0; pcm_ch=ch; return 1;
        } else fseek(f,sz+(sz&1),SEEK_CUR);
    }
    fclose(f); return 0;
}
void chip_next(float *l,float *r){
    if(!song&&!pcm){ *l=*r=0; return; }
    if(pcm&&playing&&!paused){
        if(pcm_pos>=pcm_n){ if(looping) pcm_pos=0; else { playing=0; *l=*r=0; return; } }
        int16_t *p=pcm+pcm_pos*pcm_ch; pcm_pos++;
        *l=p[0]/32768.0f*(float)pcm_gain; *r=p[pcm_ch-1]/32768.0f*(float)pcm_gain; return;
    }
    if(pcm){ *l=*r=0; return; }
    float m=chip_mono(); *l=*r=m;
}

double *pcm_gain_p=&pcm_gain;
// para el menu de depuracion: modo (0 nada, 1 CTTS pre-renderizada, 2 sintetizador de respaldo), tocando, pausada, posicion y largo en segundos
void chip_stats(int *mode,int *pl,int *pa,double *pos,double *len){
    *mode= pcm? 1 : song? 2 : 0; *pl=playing; *pa=paused; *pos= pcm? (double)pcm_pos/44100.0 : 0; *len= pcm? (double)pcm_n/44100.0 : 0; }
int pcm_try(const char *name){ pcm_free(); if(getenv("DOOM_SYNTH")) return 0; return pcm_load(name); }
void pcm_stop(void){ pcm_free(); }
void pcm_free_all(void){ pcm_free(); }
