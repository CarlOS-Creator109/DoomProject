// video_music.c - --video-music: genera un video (osciloscopio + espectro) de la MÚSICA que suena durante la partida.
// El mezclador entrega aquí cada muestra de música (vm_push); un hilo dibuja cuadros de 1280x720 a 30 fps y los manda,
// junto con el audio, a ffmpeg (video por stdin, audio por el descriptor 3) -> ./Video_Music/doom_music_FECHA.mp4
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "compat.h"
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif
#include "vfont.h"
#define VW 1280
#define VH 720
#define FPS 30
#define SR 44100
#define SPF (SR/FPS)            // 1470 muestras por cuadro
#define RING (1<<22)           // ~95 s de margen: el audio nunca debe perder muestras aunque el codificador de video vaya lento
#define WIN 2048
#define NB 56
extern char g_music_name[16];
#define PW 280            // recuadro con la ventana del juego (esquina superior derecha, opuesta al texto)
#define PH 210
#define PX (VW-PW-24)
#define PY 16
static unsigned int gbuf[640*480]; static int ghave=0; static SDL_mutex *gmx;
void vm_game_frame(const unsigned int *argb){ if(!gmx) return; SDL_LockMutex(gmx); memcpy(gbuf,argb,sizeof gbuf); ghave=1; SDL_UnlockMutex(gmx); }
int g_vm_on = 0;
static float ringl[RING], ringr[RING]; static volatile long rhead=0, rtail=0, atail=0;
static volatile int run=0; static SDL_Thread *th=NULL, *tha=NULL; static char outpath[512];
#ifdef _WIN32
static FILE *vfp=NULL, *afp=NULL; static char tmpv[560], tmpa[560];
#else
static int pid=-1, vfd=-1, afd=-1;
#endif
static unsigned char *img; static float hist[WIN*4]; static double bars[NB];

void vm_push(float l,float r){ long h=rhead; long mt=rtail<atail?rtail:atail; if(h-mt>=RING) return; ringl[h&(RING-1)]=l; ringr[h&(RING-1)]=r; __sync_synchronize(); rhead=h+1; }

static void px(int x,int y,int r,int g,int b,int a){      // mezcla aditiva con saturación (brillo de fósforo)
    if(x<0||y<0||x>=VW||y>=VH) return; unsigned char *p=img+(y*VW+x)*3;
    int v;
    v=p[0]+r*a/255; p[0]=v>255?255:v; v=p[1]+g*a/255; p[1]=v>255?255:v; v=p[2]+b*a/255; p[2]=v>255?255:v;
}
static void pip(void){
    static unsigned int loc[640*480]; if(!gmx) return; int have; SDL_LockMutex(gmx); have=ghave; if(have) memcpy(loc,gbuf,sizeof loc); SDL_UnlockMutex(gmx);
    for(int y=-3;y<PH+3;y++) for(int x=-3;x<PW+3;x++){ if(x>=0&&x<PW&&y>=0&&y<PH) continue; px(PX+x,PY+y,255,90,60,255); }      // marco
    for(int y=0;y<PH;y++) for(int x=0;x<PW;x++){
        unsigned char *d=img+((PY+y)*VW+PX+x)*3; if(!have){ d[0]=d[1]=d[2]=0; continue; }
        int sx0=x*640/PW, sx1=(x+1)*640/PW, sy0=y*480/PH, sy1=(y+1)*480/PH, r=0,g=0,b=0,n=0;     // promedio de caja
        for(int yy=sy0;yy<sy1;yy++) for(int xx=sx0;xx<sx1;xx++){ unsigned int c=loc[yy*640+xx]; r+=(c>>16)&255; g+=(c>>8)&255; b+=c&255; n++; }
        if(n){ d[0]=r/n; d[1]=g/n; d[2]=b/n; }
    }
}
static void line(int x0,int y0,int x1,int y1,int r,int g,int b,int a,int th){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
    for(;;){ for(int k=-th;k<=th;k++){ px(x0,y0+k,r,g,b,a); if(th&&dx<abs(dy)) px(x0+k,y0,r,g,b,a); }
        if(x0==x1&&y0==y1) break; int e2=2*e; if(e2>=dy){ e+=dy; x0+=sx; } if(e2<=dx){ e+=dx; y0+=sy; } }
}
static void text(int x,int y,const char *s,int sc,int r,int g,int b){
    for(;*s;s++,x+=8*sc){ int c=(unsigned char)*s; if(c<32||c>126) continue; const unsigned char *gl=VFONT+(c-32)*14;
        for(int yy=0;yy<14;yy++) for(int xx=0;xx<8;xx++) if(gl[yy]&(1<<(7-xx))) for(int a=0;a<sc;a++) for(int d=0;d<sc;d++) px(x+xx*sc+a,y+yy*sc+d,r,g,b,255); }
}
static void frame(long sec_x100){
    // fondo + rejilla
    for(int y=0;y<VH;y++){ int g=10+y*8/VH; for(int x=0;x<VW;x++){ unsigned char *p=img+(y*VW+x)*3; p[0]=6; p[1]=g; p[2]=14+y*10/VH; } }
    for(int x=0;x<=VW;x+=80) line(x>=VW?VW-1:x,225,x>=VW?VW-1:x,465,18,40,30,255,0);
    for(int y=225;y<=465;y+=40) line(0,y,VW-1,y,18,40,30,255,0);
    line(0,345,VW-1,345,30,70,50,255,0);
    // osciloscopio: ventana disparada en un cruce por cero ascendente
    const float *h=hist+WIN*4-WIN*2; int t0=0; for(int i=1;i<WIN;i++) if(h[i-1]<0&&h[i]>=0){ t0=i; break; }
    int n=WIN-t0; if(n>1100) n=1100; int px0=-1,py0=0;
    for(int i=0;i<n;i++){ int x=40+i*1200/n, y=345-(int)(fmaxf(-1,fminf(1,h[t0+i]*2.2f))*120);
        if(px0>=0){ line(px0,py0,x,y,20,160,80,70,3); line(px0,py0,x,y,120,255,160,255,0); } px0=x; py0=y; }
    // espectro (Goertzel, 56 bandas logarítmicas 40 Hz..12 kHz)
    const float *w=hist+WIN*4-WIN;
    for(int b=0;b<NB;b++){
        double f=40*pow(12000.0/40,(double)b/(NB-1)), k=2*cos(2*M_PI*f/SR), s1=0,s2=0;
        for(int i=0;i<WIN;i+=2){ double win=0.5-0.5*cos(2*M_PI*i/WIN), x=w[i]*win, s=x+k*s1-s2; s2=s1; s1=s; }
        double mag=sqrt(s1*s1+s2*s2-k*s1*s2)/(WIN/4.0); double db=20*log10(mag+1e-6); double v=(db+70)/60; if(v<0)v=0; if(v>1)v=1;
        bars[b]= v>bars[b]? v : bars[b]*0.86+v*0.14;
        int bh=(int)(bars[b]*190), x0=40+b*1200/NB, bw=1200/NB-4;
        for(int j=0;j<bh;j++){ double t=(double)j/190; int r=(int)(255*fmin(1,t*2)), g=(int)(255*fmin(1,2-t*2)), bl=60;
            for(int x=0;x<bw;x++) px(x0+x,700-j,r/2+30,g/2+30,bl,255); }
    }
    pip();
    // textos
    text(40,24,"DOOM PROJECT",3,255,90,60);
    char nm[64]; snprintf(nm,sizeof nm,"%s",g_music_name[0]? g_music_name : "-"); char t[96]; snprintf(t,sizeof t,"MUSICA  %s   %02ld:%02ld",nm,sec_x100/6000,(sec_x100/100)%60);
    for(char *c=t;*c;c++) if(*c>='a'&&*c<='z') *c-=32;
    text(40,80,t,2,200,230,210);
    text(40,118,"OSCILOSCOPIO / ESPECTRO  -  CTTS 8 BITS",1,110,150,130);
}
#ifdef _WIN32
static int wr_v(const void *b,size_t n){ return vfp && fwrite(b,1,n,vfp)==n; }
static int wr_a(const void *b,size_t n){ return afp && fwrite(b,1,n,afp)==n; }
static void close_a(void){ if(afp){ fclose(afp); afp=NULL; } }
static void close_v(void){ if(vfp){ fflush(vfp); DOOM_PCLOSE(vfp); vfp=NULL; } }
#else
static int writeall(int fd,const void *b,size_t n){ const char *p=b; while(n){ ssize_t w=write(fd,p,n); if(w<=0) return 0; p+=w; n-=w; } return 1; }
static int wr_v(const void *b,size_t n){ return writeall(vfd,b,n); }
static int wr_a(const void *b,size_t n){ return writeall(afd,b,n); }
static void close_a(void){ if(afd>=0){ close(afd); afd=-1; } }
static void close_v(void){ if(vfd>=0){ close(vfd); vfd=-1; } }
#endif
// Audio y video se escriben en hilos distintos y cada uno cierra su entrada al terminar: ffmpeg lee cada entrada
// cuando le conviene (al sondear una puede esperar a la otra) y con un solo escritor se bloqueaba.
static int athread(void *u){
    (void)u; short *ab=malloc(SPF*4);
    for(;;){
        long avail=rhead-atail;
        if(avail<SPF && run){ SDL_Delay(3); continue; }
        if(avail<=0) break;
        int n= avail<SPF? (int)avail : SPF;
        for(int i=0;i<n;i++){ long k=(atail+i)&(RING-1); ab[2*i]=(short)(fmaxf(-1,fminf(1,ringl[k]))*32767); ab[2*i+1]=(short)(fmaxf(-1,fminf(1,ringr[k]))*32767); }
        atail+=n; if(!wr_a(ab,n*4)) break;
    }
    free(ab); close_a(); return 0;
}
static int vthread(void *u){
    (void)u; long frames=0;
    for(;;){
        long avail=rhead-rtail;
        if(avail<SPF && run){ SDL_Delay(3); continue; }
        if(avail<=0) break;
        int n= avail<SPF? (int)avail : SPF;
        memmove(hist,hist+SPF,(WIN*4-SPF)*sizeof(float));
        for(int i=0;i<SPF;i++){ float m=0; if(i<n){ long k=(rtail+i)&(RING-1); m=(ringl[k]+ringr[k])*0.5f; } hist[WIN*4-SPF+i]=m; }
        rtail+=n;
        // si el codificador va atrasado (>0.5 s), repetir el cuadro anterior en vez de dibujar: el audio sigue intacto y el video no se desfasa
        if(rhead-rtail<SR/2 || frames==0) frame(frames*100/FPS);
        if(!wr_v(img,VW*VH*3)) break;
        frames++;
    }
    close_v(); return 0;
}
static int ff_encoder_ok(void){ return doom_ff_has("-encoders","libx264"); }
int vm_start(const char *dir){
    DOOM_MKDIR(dir); time_t tt=time(NULL); struct tm *tm=localtime(&tt); char ts[40]; strftime(ts,sizeof ts,"%Y%m%d_%H%M%S",tm);
    snprintf(outpath,sizeof outpath,"%s/doom_music_%s.mp4",dir,ts);
    int x264=ff_encoder_ok();
#ifdef _WIN32
    // Windows no tiene fork ni descriptor 3: el video va por stdin de ffmpeg y el audio a un archivo temporal; al terminar se unen
    snprintf(tmpv,sizeof tmpv,"%s/.video_tmp.mp4",dir); snprintf(tmpa,sizeof tmpa,"%s/.audio_tmp.raw",dir);
    char cmd[1024]; snprintf(cmd,sizeof cmd,"%s -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 1280x720 -r 30 -i - -c:v %s %s -pix_fmt yuv420p \"%s\"",
        doom_ffbin(),x264?"libx264":"mpeg4",x264?"-preset ultrafast -crf 26":"-q:v 3",tmpv);
    vfp=DOOM_POPEN_W(cmd); afp=fopen(tmpa,"wb"); if(!vfp||!afp) return 0;
#else
    int vp[2],ap[2]; if(pipe(vp)||pipe(ap)) return 0;
    signal(SIGPIPE,SIG_IGN);
    pid=fork(); if(pid<0) return 0;
    if(pid==0){
        dup2(vp[0],0); dup2(ap[0],3); for(int i=4;i<256;i++) close(i);
        execlp(doom_ffbin(),"ffmpeg","-hide_banner","-loglevel","error","-y","-f","rawvideo","-pix_fmt","rgb24","-s","1280x720","-r","30","-i","pipe:0",
               "-f","s16le","-ar","44100","-ac","2","-i","pipe:3","-c:v",x264?"libx264":"mpeg4",x264?"-preset":"-q:v",x264?"ultrafast":"3",
               "-pix_fmt","yuv420p",x264?"-crf":"-b:v",x264?"26":"4M","-c:a","aac","-b:a","192k","-movflags","frag_keyframe+empty_moov",outpath,(char*)NULL);
        _exit(127);
    }
    close(vp[0]); close(ap[0]); vfd=vp[1]; afd=ap[1];
#endif
    img=malloc(VW*VH*3); gmx=SDL_CreateMutex(); ghave=0; memset(hist,0,sizeof hist); rhead=rtail=atail=0; run=1; g_vm_on=1;
    tha=SDL_CreateThread(athread,"audio-music",NULL); th=SDL_CreateThread(vthread,"video-music",NULL); printf("[VIDEO] grabando la música en %s\n",outpath); return th!=NULL;
}
void vm_stop(void){
    if(!g_vm_on) return; g_vm_on=0; run=0; SDL_WaitThread(th,NULL); SDL_WaitThread(tha,NULL); th=tha=NULL;
#ifdef _WIN32
    { char cmd[1400]; snprintf(cmd,sizeof cmd,"%s -hide_banner -loglevel error -y -i \"%s\" -f s16le -ar 44100 -ac 2 -i \"%s\" -c:v copy -c:a aac -b:a 192k -shortest \"%s\"",doom_ffbin(),tmpv,tmpa,outpath);
      if(system(cmd)==0){ remove(tmpv); remove(tmpa); } else fprintf(stderr,"[VIDEO] no pude unir audio y video (quedan %s y %s)\n",tmpv,tmpa); }
#else
    int st; waitpid(pid,&st,0);
#endif
    free(img); img=NULL; printf("[VIDEO] guardado: %s\n",outpath);
}
