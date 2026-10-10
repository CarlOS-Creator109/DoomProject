#include "map.h"
#include <string.h>
#include <stdint.h>

void tex_name(char out[9], const char in[8]){
    memcpy(out,in,8); out[8]=0;
    // trim at first \0 or space padding
    for(int i=0;i<8;i++) if(out[i]=='\0') {out[i]=0; break;}
    // rtrim spaces
    int len=strlen(out);
    while(len>0 && (out[len-1]==' '||out[len-1]=='\0')) out[--len]=0;
}
void pic_name(char out[9], const char in[8]){ tex_name(out,in); }

uint32_t hash_str(const char *s){
    uint32_t h=5381;
    while(*s) h = ((h<<5)+h) ^ (uint8_t)(*s++);
    return h;
}
