// compat.h - diferencias minimas entre Linux / macOS / Windows (MinGW) para Doom Project
#ifndef COMPAT_H
#define COMPAT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
  extern int _mkdir(const char *);
  #include <strings.h>
  static inline int setenv(const char *n,const char *v,int o){ if(!o && getenv(n)) return 0; return _putenv_s(n,v); }
  #define DOOM_MKDIR(d) _mkdir(d)
  #define DOOM_POPEN_R(c) _popen((c),"r")
  #define DOOM_POPEN_W(c) _popen((c),"wb")
  #define DOOM_PCLOSE(f) _pclose(f)
  #define DOOM_NULLDEV "NUL"
#else
  #include <sys/stat.h>
  #define DOOM_MKDIR(d) mkdir((d),0755)
  #define DOOM_POPEN_R(c) popen((c),"r")
  #define DOOM_POPEN_W(c) popen((c),"w")
  #define DOOM_PCLOSE(f) pclose(f)
  #define DOOM_NULLDEV "/dev/null"
#endif
// ffmpeg: DOOM_FFMPEG_BIN, si no, "ffmpeg" del PATH (los builders lo dejan en ./ffmpeg y el lanzador lo pone en el PATH)
static inline const char *doom_ffbin(void){ const char *b=getenv("DOOM_FFMPEG_BIN"); return b&&*b? b : "ffmpeg"; }
// true si ffmpeg lista el texto 'what' en 'opt' (-encoders / -devices / -muxers)
static inline int doom_ff_has(const char *opt,const char *what){
    char cmd[512]; snprintf(cmd,sizeof cmd,"%s -hide_banner %s 2>%s",doom_ffbin(),opt,DOOM_NULLDEV);
    FILE *p=DOOM_POPEN_R(cmd); if(!p) return 0;
    char line[512]; int found=0;
    while(fgets(line,sizeof line,p)) if(strstr(line,what)) found=1;
    DOOM_PCLOSE(p); return found;
}
#endif
