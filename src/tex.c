#include "tex.h"
#include "wad.h"
#include "map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#define WIDTH 640
#define HEIGHT 480

uint8_t doom_palette[256][3];

Texture *textures=NULL;
int num_textures=0;
Flat *flats=NULL;
int num_flats=0;

static WAD *gwad=NULL;

#pragma pack(push,1)
typedef struct { int16_t width, height, leftoffset, topoffset; int32_t columnofs[1]; } PatchHdr;
typedef struct { uint8_t topdelta, length; uint8_t unused; } PostHdr;
typedef struct { int16_t originx, originy; int16_t patch; int16_t stepdir, colormap; } MapPatchRaw;
typedef struct { char name[8]; int32_t masked; int16_t width,height; int32_t columndir; int16_t patchcount; MapPatchRaw patches[1]; } MapTextureRaw;
#pragma pack(pop)

static void name_to8(char out[9], const char *in){
    char tmp[9]; memcpy(tmp,in,8); tmp[8]=0;   // copia primero: in y out pueden ser el mismo buffer
    memset(out,0,9);
    memcpy(out,tmp,strnlen(tmp,8));
    for(int i=0;i<8 && out[i];i++) if(out[i]>='a'&&out[i]<='z') out[i]-=32;
}

int tex_num_for_name(const char *name){
    if(!name || name[0]=='-' || name[0]==0) return -1;
    char n8[9]; name_to8(n8,name);
    for(int i=0;i<num_textures;i++){
        if(strncmp(textures[i].name,n8,8)==0) return i;
    }
    return -1;
}
Texture *tex_find(const char *name){
    int n=tex_num_for_name(name);
    return n>=0? &textures[n]: NULL;
}
Flat *flat_find(const char *name){
    if(!name || name[0]=='-') return NULL;
    char n8[9]; name_to8(n8,name);
    for(int i=0;i<num_flats;i++) if(strncmp(flats[i].name,n8,8)==0) return &flats[i];
    return NULL;
}
int tex_get_pixel(Texture *t,int x,int y){
    if(!t) return 255;
    if(x<0||x>=t->width||y<0||y>=t->height) return 255;
    return t->pixels[y*t->width + x];
}
int flat_get_pixel(Flat *f,int x,int y){
    if(!f) return 0;
    x&=63; y&=63;
    return f->data[y*64+x];
}

static void build_texture(Texture *tex, MapTextureRaw *mtex, int patchcount, int *patch_lumps){
    int w = mtex->width;
    int h = mtex->height;
    int pc = mtex->patchcount;
    // sanity
    if(w>2048 || h>2048 || pc>64 || pc<0){
        w = (w>>8)|((w&0xFF)<<8);
        h = (h>>8)|((h&0xFF)<<8);
        pc= (pc>>8)|((pc&0xFF)<<8);
    }
    tex->width=w; tex->height=h;
    tex->pixels=calloc(w*h,1);
    memset(tex->pixels, 255, w*h);
    for(int p=0;p<pc;p++){
        MapPatchRaw *mp=&mtex->patches[p];
        int lump = patch_lumps[mp->patch];
        if(lump<0) continue;
        uint8_t *pdata=gwad->lumps[lump].data;
        int psz=gwad->lumps[lump].size;
        if(!pdata || psz<8) continue;
        PatchHdr *ph=(PatchHdr*)pdata;
        int pw=ph->width;
        int phh=ph->height;
        if(pw>1024 || phh>1024){
            pw = (pw>>8)|((pw&0xFF)<<8);
            phh=(phh>>8)|((phh&0xFF)<<8);
        }
        int ox=mp->originx, oy=mp->originy;
        for(int x=0;x<pw;x++){
            int tx = ox + x;
            if(tx<0 || tx>=w) continue;
            if(x*4+8 > psz) continue;
            int32_t colofs = ((int32_t*)pdata)[2 + x];
            if(colofs> psz || colofs<0){
                colofs = ((colofs&0xFF)<<24)|((colofs&0xFF00)<<8)|((colofs&0xFF0000)>>8)|((colofs>>24)&0xFF);
                if(colofs>psz || colofs<0) continue;
            }
            uint8_t *col = pdata + colofs;
            int pos=0;
            while(pos+2 < psz - (int)(col - pdata)){
                uint8_t top=col[pos];
                if(top==0xFF) break;
                uint8_t len=col[pos+1];
                if(pos+3+len >= psz-(int)(col-pdata)) break;
                for(int k=0;k<len;k++){
                    int ty = oy + top + k;
                    if(ty<0||ty>=h) continue;
                    uint8_t pix=col[pos+3+k];
                    tex->pixels[ty*w + tx]=pix;
                }
                pos+= 3 + len + 1;
            }
        }
    }
}

int tex_init(WAD *wad){
    gwad=wad;
    // PLAYPAL
    int psz; uint8_t *pal=wad_get_data(wad,"PLAYPAL",&psz);
    if(pal && psz>=768){
        for(int i=0;i<256;i++){ doom_palette[i][0]=pal[i*3]; doom_palette[i][1]=pal[i*3+1]; doom_palette[i][2]=pal[i*3+2]; }
    } else {
        for(int i=0;i<256;i++) doom_palette[i][0]=doom_palette[i][1]=doom_palette[i][2]=(i);
    }
    // PNAMES
    uint8_t *pnames=wad_get_data(wad,"PNAMES",&psz);
    int num_pnames=0;
    int *patch_lumps=NULL;
    if(pnames && psz>=4){
        num_pnames = pnames[0]|pnames[1]<<8|pnames[2]<<16|pnames[3]<<24;
        patch_lumps=calloc(num_pnames,sizeof(int));
        for(int i=0;i<num_pnames;i++){
            char name[9]={0};
            if(4+i*8+8 <= psz) memcpy(name, pnames+4+i*8, 8);
            name_to8(name,name);
            int found=-1;
            for(int li=0; li<wad->numlumps; li++){
                char a[9],b[9];
                strncpy(a, wad->lumps[li].name,8); a[8]=0;
                strncpy(b, name,8); b[8]=0;
                for(int k=0;a[k];k++) if(a[k]>='a'&&a[k]<='z') a[k]-=32;
                for(int k=0;b[k];k++) if(b[k]>='a'&&b[k]<='z') b[k]-=32;
                if(strcmp(a,b)==0){ found=li; break; }
            }
            if(found==-1){
                char up[9]; strncpy(up,name,8); up[8]=0;
                for(int k=0;up[k];k++) if(up[k]>='a'&&up[k]<='z') up[k]-=32;
                Lump *lu=wad_get(wad,up);
                if(lu) found = lu - wad->lumps;
            }
            patch_lumps[i]=found;
        }
    }
    // TEXTURE1 / TEXTURE2
    int t1size, t2size;
    uint8_t *t1=wad_get_data(wad,"TEXTURE1",&t1size);
    uint8_t *t2=wad_get_data(wad,"TEXTURE2",&t2size);
    int num1=0,num2=0;
    if(t1 && t1size>=4) num1 = t1[0]|t1[1]<<8|t1[2]<<16|t1[3]<<24;
    if(t2 && t2size>=4) num2 = t2[0]|t2[1]<<8|t2[2]<<16|t2[3]<<24;
    num_textures=num1+num2;
    if(num_textures>0){
        textures=calloc(num_textures,sizeof(Texture));
        int idx=0;
        for(int pass=0; pass<2; pass++){
            uint8_t *base = pass==0? t1: t2;
            int num = pass==0? num1: num2;
            int bsize = pass==0? t1size: t2size;
            if(!base) continue;
            for(int i=0;i<num;i++){
                int off = base[4+i*4]|base[5+i*4]<<8|base[6+i*4]<<16|base[7+i*4]<<24;
                if(off+22 > bsize) continue;
                MapTextureRaw *mtex=(MapTextureRaw*)(base+off);
                char name[9]={0}; memcpy(name,mtex->name,8);
                for(int k=0;k<8 && name[k];k++) if(name[k]>='a'&&name[k]<='z') name[k]-=32;
                strncpy(textures[idx].name,name,8);
                build_texture(&textures[idx], mtex, mtex->patchcount, patch_lumps);
                idx++;
            }
        }
    }
    if(patch_lumps) free(patch_lumps);
    printf("[TEX] %d textures loaded (T1=%d T2=%d)\n", num_textures, num1,num2);
    // flats: between F_START and F_END
    int f_start=-1,f_end=-1;
    for(int i=0;i<wad->numlumps;i++) if(strcmp(wad->lumps[i].name,"F_START")==0) f_start=i;
    for(int i=0;i<wad->numlumps;i++) if(strcmp(wad->lumps[i].name,"F_END")==0) f_end=i;
    if(f_start>=0 && f_end> f_start+1){
        num_flats = f_end - f_start -1;
        flats=calloc(num_flats,sizeof(Flat));
        for(int i=0;i<num_flats;i++){
            Lump *l=&wad->lumps[f_start+1+i];
            strncpy(flats[i].name,l->name,8);
            flats[i].name[8]=0;
            if(l->size==4096) memcpy(flats[i].data,l->data,4096);
            else memset(flats[i].data,0,4096);
        }
        printf("[TEX] %d flats loaded\n", num_flats);
    } else {
        printf("[TEX] no F_START/F_END found\n");
    }
    return 0;
}
void tex_free(void){
    if(textures){ for(int i=0;i<num_textures;i++) free(textures[i].pixels); free(textures); textures=NULL; num_textures=0; }
    if(flats){ free(flats); flats=NULL; num_flats=0; }
}

void patch_draw(SDL_Renderer *ren, uint8_t *data, int size, int x, int y, int scale){
    if(!data || size<8) return;
    int16_t w = data[0]|data[1]<<8;
    int16_t h = data[2]|data[3]<<8;
    if(w<=0||w>2000||h<=0||h>500) return;
    for(int cx=0; cx<w; cx++){
        int off = data[8+cx*4]|data[9+cx*4]<<8|data[10+cx*4]<<16|data[11+cx*4]<<24;
        if(off<0||off>=size) continue;
        uint8_t *col=data+off;
        int p=0;
        while(p+3 < size - off){
            uint8_t top=col[p];
            if(top==0xFF) break;
            uint8_t len=col[p+1];
            if(p+3+len >= size-off) break;
            for(int k=0;k<len;k++){
                int py = y + top + k;
                uint8_t idx=col[p+3+k];
                if(idx==255) continue;
                uint8_t r=doom_palette[idx][0], g=doom_palette[idx][1], b=doom_palette[idx][2];
                SDL_SetRenderDrawColor(ren,r,g,b,255);
                if(scale==1) SDL_RenderDrawPoint(ren, x+cx, py);
                else {
                    SDL_Rect rc={x+cx*scale, py*scale, scale, scale};
                    rc.x = x + cx*scale;
                    rc.y = (y + top + k)*scale;
                    SDL_RenderFillRect(ren,&rc);
                }
            }
            p+= 4 + len;
        }
    }
}
void patch_draw_to_texture(SDL_Texture *tex, uint8_t *data, int size){ (void)tex;(void)data;(void)size; }
