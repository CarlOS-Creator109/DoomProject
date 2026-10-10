// mus2wav: renderiza una pieza MUS de un WAD con el sintetizador de 8 bits -> WAV 16-bit mono 44.1 kHz
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "wad.h"
void chip_next(float*,float*); int I_RegisterSong(void*); void I_PlaySong(int,int); void I_SetMusicVolume(int);
int main(int argc,char**argv){
    if(argc<4){ fprintf(stderr,"uso: mus2wav DOOM.WAD D_E1M1 salida.wav [segundos]\n"); return 1; }
    WAD *w=wad_load(argv[1]); if(!w) return 1; int sz; uint8_t *d=wad_get_data(w,argv[2],&sz); if(!d){ fprintf(stderr,"no hay %s\n",argv[2]); return 1; }
    double secs= argc>4? atof(argv[4]) : 60; long n=(long)(secs*44100);
    I_RegisterSong(d); I_SetMusicVolume(100); I_PlaySong(0,0);
    int16_t *o=malloc(n*2); long used=n; double peak=0;
    for(long i=0;i<n;i++){ float l,r; chip_next(&l,&r); float s=l; if(s>peak) peak=s; if(-s>peak) peak=-s; o[i]=(int16_t)(s*32000); }
    FILE *f=fopen(argv[3],"wb"); uint32_t dl=used*2, r=44100, br=88200, ck=36+dl; uint16_t fm=1,ch=1,ba=2,bt=16; uint32_t sixteen=16;
    fwrite("RIFF",1,4,f); fwrite(&ck,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&sixteen,4,1,f); fwrite(&fm,2,1,f); fwrite(&ch,2,1,f); fwrite(&r,4,1,f); fwrite(&br,4,1,f); fwrite(&ba,2,1,f); fwrite(&bt,2,1,f);
    fwrite("data",1,4,f); fwrite(&dl,4,1,f); fwrite(o,2,used,f); fclose(f); printf("pico %.2f, %.1f s\n",peak,secs); return 0;
}
