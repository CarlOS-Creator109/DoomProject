#include "wad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

static int is_zip(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f) return 0;
    uint8_t m[4]; size_t n=fread(m,1,4,f); fclose(f);
    return n==4 && m[0]==0x50 && m[1]==0x4B && m[2]==0x03 && m[3]==0x04;
}

static int strcase_ends_with(const char *s, const char *suf){
    size_t ls=strlen(s), lf=strlen(suf);
    if(lf>ls) return 0;
    const char *a=s+ls-lf;
    for(size_t i=0;i<lf;i++){
        char ca=a[i], cb=suf[i];
        if(ca>='A'&&ca<='Z') ca|=0x20;
        if(cb>='A'&&cb<='Z') cb|=0x20;
        if(ca!=cb) return 0;
    }
    return 1;
}

// minimal ZIP local file parser, only store (method 0) directly, deflate via system unzip -p fallback
static char *zip_extract_wad(const char *zip_path){
    FILE *f=fopen(zip_path,"rb");
    if(!f) return NULL;
    fseek(f,0,SEEK_END);
    long fsize=ftell(f);
    fseek(f,0,SEEK_SET);
    // Collect candidates
    typedef struct { char name[512]; long data_ofs; uint32_t comp_size; uint32_t uncomp_size; uint16_t method; } Entry;
    Entry *entries=NULL; int nentries=0, cap=0;
    long pos=0;
    while(pos+30 <= fsize){
        fseek(f,pos,SEEK_SET);
        uint8_t hdr[30];
        if(fread(hdr,1,30,f)!=30) break;
        uint32_t sig = hdr[0]|hdr[1]<<8|hdr[2]<<16|hdr[3]<<24;
        if(sig!=0x04034b50) break; // local file header
        uint16_t method = hdr[8]|hdr[9]<<8;
        uint32_t comp = hdr[18]|hdr[19]<<8|hdr[20]<<16|hdr[21]<<24;
        uint32_t uncomp = hdr[22]|hdr[23]<<8|hdr[24]<<16|hdr[25]<<24;
        uint16_t fname_len = hdr[26]|hdr[27]<<8;
        uint16_t extra_len = hdr[28]|hdr[29]<<8;
        if(pos+30+fname_len+extra_len > fsize) break;
        char fname[512]={0};
        if(fname_len>=sizeof(fname)) fname_len=sizeof(fname)-1;
        fread(fname,1,fname_len,f);
        // extra
        fseek(f, extra_len, SEEK_CUR);
        long data_ofs = pos+30+fname_len+extra_len;
        if(nentries>=cap){
            cap= cap? cap*2:16;
            entries=realloc(entries, cap*sizeof(Entry));
        }
        strncpy(entries[nentries].name, fname, 511);
        entries[nentries].data_ofs=data_ofs;
        entries[nentries].comp_size=comp;
        entries[nentries].uncomp_size=uncomp;
        entries[nentries].method=method;
        nentries++;
        pos = data_ofs + comp;
    }
    fclose(f);
    if(nentries==0){ free(entries); return NULL; }
    // choose target: prefer freedoom2.wad > freedoom1.wad > doom2.wad > doom.wad > any .wad
    const char *prefs[]={"freedoom2.wad","freedoom1.wad","doom2.wad","doom.wad", NULL};
    int chosen=-1;
    for(int p=0;prefs[p];p++){
        for(int i=0;i<nentries;i++){
            if(strcase_ends_with(entries[i].name, prefs[p])){ chosen=i; break; }
        }
        if(chosen!=-1) break;
    }
    if(chosen==-1){
        for(int i=0;i<nentries;i++) if(strcase_ends_with(entries[i].name,".wad")){ chosen=i; break; }
    }
    if(chosen==-1){ free(entries); return NULL; }
    Entry e=entries[chosen];
    free(entries);
    // if store, extract directly
    char tmp[512];
    const char *base=strrchr(e.name,'/');
    base= base? base+1: e.name;
    snprintf(tmp,sizeof(tmp),"/tmp/doom_engine_%s", base);
    if(e.method==0){
        FILE *zf=fopen(zip_path,"rb");
        FILE *out=fopen(tmp,"wb");
        if(!zf||!out){ if(zf) fclose(zf); if(out) fclose(out); return NULL; }
        fseek(zf,e.data_ofs,SEEK_SET);
        uint8_t buf[8192];
        uint32_t remain=e.comp_size;
        while(remain){
            size_t r= remain>sizeof(buf)? sizeof(buf):remain;
            size_t n=fread(buf,1,r,zf);
            if(n==0) break;
            fwrite(buf,1,n,out);
            remain-=n;
        }
        fclose(zf); fclose(out);
        printf("[WAD] ZIP %s -> extraído %s (%u KB) a %s\n", zip_path, e.name, e.comp_size/1024, tmp);
        char *ret=strdup(tmp);
        return ret;
    } else {
        // deflate -> fallback to system unzip -p
        printf("[WAD] ZIP %s %s método %d (deflate), usando unzip -p ...\n", zip_path, e.name, e.method);
        char cmd[1024];
        snprintf(cmd,sizeof(cmd),"unzip -p \"%s\" \"%s\" > \"%s\" 2>/dev/null", zip_path, e.name, tmp);
        int rc=system(cmd);
        if(rc!=0){
            fprintf(stderr,"[WAD] unzip fallback falló rc=%d\n",rc);
            return NULL;
        }
        struct stat st;
        if(stat(tmp,&st)!=0) return NULL;
        printf("[WAD] ZIP %s -> extraído via unzip %s (%ld KB) a %s\n", zip_path, e.name, st.st_size/1024, tmp);
        return strdup(tmp);
    }
}

WAD *wad_load(const char *path){
    char *actual=NULL;
    int need_free=0;
    if(is_zip(path)){
        actual=zip_extract_wad(path);
        if(!actual){
            fprintf(stderr,"[WAD] no se pudo extraer WAD de ZIP %s\n",path);
            return NULL;
        }
        need_free=1;
    } else {
        actual=strdup(path);
        need_free=1;
    }
    FILE *f=fopen(actual,"rb");
    if(!f){ perror(actual); free(actual); return NULL; }
    fseek(f,0,SEEK_END);
    long fsize=ftell(f);
    fseek(f,0,SEEK_SET);
    uint8_t hdr[12];
    if(fread(hdr,1,12,f)!=12){ fclose(f); free(actual); return NULL; }
    char magic[5]={0};
    memcpy(magic,hdr,4);
    int32_t numlumps = hdr[4]|hdr[5]<<8|hdr[6]<<16|hdr[7]<<24;
    int32_t dir_ofs = hdr[8]|hdr[9]<<8|hdr[10]<<16|hdr[11]<<24;
    if(strcmp(magic,"IWAD")!=0 && strcmp(magic,"PWAD")!=0){
        fprintf(stderr,"[WAD] Magic inválido %s en %s\n",magic,actual);
        fclose(f); free(actual); return NULL;
    }
    if(dir_ofs<0 || dir_ofs+numlumps*16 > fsize){
        fprintf(stderr,"[WAD] dir_ofs corrupto %d\n",dir_ofs);
        fclose(f); free(actual); return NULL;
    }
    fseek(f,dir_ofs,SEEK_SET);
    uint8_t *dir = malloc(numlumps*16);
    if(fread(dir,1,numlumps*16,f)!=(size_t)(numlumps*16)){ free(dir); fclose(f); free(actual); return NULL; }
    WAD *wad=calloc(1,sizeof(WAD));
    strncpy(wad->magic,magic,5);
    wad->numlumps=numlumps;
    wad->dir_ofs=dir_ofs;
    wad->lumps=calloc(numlumps,sizeof(Lump));
    wad->lumps_cap=numlumps;
    strncpy(wad->path,actual,511);
    strncpy(wad->orig_path,path,511);
    for(int i=0;i<numlumps;i++){
        uint8_t *e=dir+i*16;
        int32_t filepos = e[0]|e[1]<<8|e[2]<<16|e[3]<<24;
        int32_t size    = e[4]|e[5]<<8|e[6]<<16|e[7]<<24;
        char name[9]={0};
        memcpy(name,e+8,8);
        // rstrip \0
        for(int k=7;k>=0;k--) if(name[k]=='\0'||name[k]==' ') name[k]='\0'; else break;
        // ensure null
        name[8]='\0';
        // trim after first \0 already
        int len=strlen(name);
        // remove trailing spaces (already)
        Lump *l=&wad->lumps[i];
        strncpy(l->name,name,8);
        l->name[8]='\0';
        l->filepos=filepos;
        l->size=size;
        if(size>0){
            if(filepos<0 || filepos+size > fsize){
                l->data=NULL; l->size=0;
            } else {
                l->data=malloc(size);
                fseek(f,filepos,SEEK_SET);
                fread(l->data,1,size,f);
            }
        } else l->data=NULL;
    }
    free(dir);
    fclose(f);
    printf("[WAD] %s %s: %d lumps, dir @ %d\n", wad->magic, path, numlumps, dir_ofs);
    free(actual);
    return wad;
}
void wad_free(WAD *wad){
    if(!wad) return;
    for(int i=0;i<wad->numlumps;i++) free(wad->lumps[i].data);
    free(wad->lumps);
    free(wad);
}
Lump *wad_get(WAD *wad, const char *name){
    // último con ese nombre
    Lump *found=NULL;
    for(int i=0;i<wad->numlumps;i++) if(strcmp(wad->lumps[i].name,name)==0) found=&wad->lumps[i];
    return found;
}
uint8_t *wad_get_data(WAD *wad, const char *name, int *out_size){
    Lump *l=wad_get(wad,name);
    if(!l) return NULL;
    if(out_size) *out_size=l->size;
    return l->data;
}
char **wad_find_maps(WAD *wad, int *out_count){
    char **maps=NULL; int cap=0, cnt=0;
    for(int i=0;i<wad->numlumps;i++){
        char *name=wad->lumps[i].name;
        int len=strlen(name);
        int is_exmy = len==4 && name[0]=='E' && name[2]=='M' && name[1]>='0'&&name[1]<='9' && name[3]>='0'&&name[3]<='9';
        int is_mapxx= len==5 && strncmp(name,"MAP",3)==0 && name[3]>='0'&&name[3]<='9' && name[4]>='0'&&name[4]<='9';
        if(!is_exmy && !is_mapxx) continue;
        // verificar THINGS/LINEDEFS en siguientes
        int has_things=0, has_lines=0;
        for(int j=i+1;j < i+11 && j<wad->numlumps;j++){
            if(strcmp(wad->lumps[j].name,"THINGS")==0) has_things=1;
            if(strcmp(wad->lumps[j].name,"LINEDEFS")==0) has_lines=1;
            if(strlen(wad->lumps[j].name)==4 && wad->lumps[j].name[0]=='E' && wad->lumps[j].name[2]=='M') break;
            if(strlen(wad->lumps[j].name)==5 && strncmp(wad->lumps[j].name,"MAP",3)==0) break;
        }
        int dup=0; for(int k=0;k<cnt;k++) if(!strcmp(maps[k],name)) dup=1;
        if(has_things && has_lines && !dup){
            if(cnt>=cap){ cap=cap?cap*2:16; maps=realloc(maps,cap*sizeof(char*)); }
            maps[cnt++]=strdup(name);
        }
    }
    *out_count=cnt;
    return maps;
}
void wad_find_maps_free(char **maps,int count){
    for(int i=0;i<count;i++) free(maps[i]);
    free(maps);
}
// PWAD: anade los lumps de 'add' al final de 'base' (los del PWAD ganan al buscar por nombre desde el final)
int wad_merge(WAD *base, WAD *add){
    if(!base||!add) return -1;
    int n=base->numlumps+add->numlumps;
    Lump *nl=realloc(base->lumps,n*sizeof(Lump));
    if(!nl) return -1;
    memcpy(nl+base->numlumps,add->lumps,add->numlumps*sizeof(Lump));
    base->lumps=nl; base->lumps_cap=n; base->numlumps=n;
    free(add->lumps); free(add);      // los datos pasan a 'base'
    return 0;
}
int wad_read_map(WAD *wad, const char *mapname, MapLumps *out){
    memset(out,0,sizeof(*out));
    int idx=-1;
    for(int i=wad->numlumps-1;i>=0;i--) if(strcmp(wad->lumps[i].name,mapname)==0){ idx=i; break; }
    if(idx==-1) return -1;
    for(int j=idx+1;j < idx+12 && j<wad->numlumps;j++){
        char *n=wad->lumps[j].name;
        if(strlen(n)==4 && n[0]=='E' && n[2]=='M') break;
        if(strlen(n)==5 && strncmp(n,"MAP",3)==0) break;
        Lump *l=&wad->lumps[j];
        if(strcmp(n,"THINGS")==0){ out->things=l->data; out->things_len=l->size; }
        else if(strcmp(n,"LINEDEFS")==0){ out->linedefs=l->data; out->linedefs_len=l->size; }
        else if(strcmp(n,"SIDEDEFS")==0){ out->sidedefs=l->data; out->sidedefs_len=l->size; }
        else if(strcmp(n,"VERTEXES")==0){ out->vertexes=l->data; out->vertexes_len=l->size; }
        else if(strcmp(n,"SEGS")==0){ out->segs=l->data; out->segs_len=l->size; }
        else if(strcmp(n,"SSECTORS")==0){ out->ssectors=l->data; out->ssectors_len=l->size; }
        else if(strcmp(n,"NODES")==0){ out->nodes=l->data; out->nodes_len=l->size; }
        else if(strcmp(n,"SECTORS")==0){ out->sectors=l->data; out->sectors_len=l->size; }
        else if(strcmp(n,"REJECT")==0){ out->reject=l->data; out->reject_len=l->size; }
        else if(strcmp(n,"BLOCKMAP")==0){ out->blockmap=l->data; out->blockmap_len=l->size; }
    }
    return 0;
}
PalColor *wad_get_palette(WAD *wad,int palnum){
    int sz; uint8_t *data=wad_get_data(wad,"PLAYPAL",&sz);
    if(!data || sz < (palnum+1)*768) return NULL;
    PalColor *pal=malloc(256*sizeof(PalColor));
    uint8_t *src=data+palnum*768;
    for(int i=0;i<256;i++){ pal[i].r=src[i*3]; pal[i].g=src[i*3+1]; pal[i].b=src[i*3+2]; }
    return pal;
}
