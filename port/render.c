// render.c - renderizador propio (columnas + BSP) que dibuja DIRECTAMENTE sobre las estructuras
// originales de Doom (sector_t, seg_t, node_t, mobj_t, pspdef_t...) que mantiene el código de juego original.
// Salida: buffer de índices de paleta 640x416; la barra/HUD/automapa originales (screens[0]) se componen encima.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "wad.h"
#include "tex.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "p_local.h"
#include "p_pspr.h"
#include "r_state.h"
#include "r_main.h"
#include "r_data.h"
#include "r_sky.h"
#include "info.h"
#include "render.h"

#define WIDTH RV_WIDTH
#define VIEWH RV_VIEWH
#define HALF_W (WIDTH/2)
#define HALF_H (VIEWH/2)
#define PROJ  320.0                 // FOV horizontal 90 grados
#define PROJY (PROJ*1.2)            // los píxeles de Doom (320x200 en 4:3) son 1.2 veces más altos que anchos
#define MAXREC 48
#define MAXMASK 6

static uint8_t vbuf[VIEWH][WIDTH];
static uint8_t colormap[34*256];
static uint32_t pal32[256];
static WAD *rw;
static double vx, vy, vz, vang;
static int extralight_v, fixedcm;

// ---------- caches por nivel (doubles) ----------
typedef struct { double x1,y1,x2,y2,len,offset; seg_t *s; side_t *sd; sector_t *front,*back; int hv; } RSeg;
typedef struct { double x,y,dx,dy,bbox[2][4]; unsigned short child[2]; } RNode;
static RSeg *rsegs; static RNode *rnodes; static int rnn;
static Texture **sky_tex_p; static Texture *sky_tex;

void RV_Level(void){
    free(rsegs); free(rnodes);
    rsegs=calloc(numsegs?numsegs:1,sizeof(RSeg));
    for(int i=0;i<numsegs;i++){
        seg_t *s=&segs[i]; RSeg *r=&rsegs[i];
        r->x1=s->v1->x/65536.0; r->y1=s->v1->y/65536.0; r->x2=s->v2->x/65536.0; r->y2=s->v2->y/65536.0;
        r->len=hypot(r->x2-r->x1,r->y2-r->y1); r->offset=s->offset/65536.0;
        r->s=s; r->sd=s->sidedef; r->front=s->frontsector; r->back=s->backsector;
        r->hv = (s->v1->y==s->v2->y)? -1 : (s->v1->x==s->v2->x)? 1 : 0;   // contraste falso de R_StoreWallRange
    }
    rnn=numnodes; rnodes=calloc(numnodes?numnodes:1,sizeof(RNode));
    for(int i=0;i<numnodes;i++){
        node_t *n=&nodes[i]; RNode *r=&rnodes[i];
        r->x=n->x/65536.0; r->y=n->y/65536.0; r->dx=n->dx/65536.0; r->dy=n->dy/65536.0;
        for(int a=0;a<2;a++){ for(int b=0;b<4;b++) r->bbox[a][b]=n->bbox[a][b]/65536.0; r->child[a]=n->children[a]; }
    }
    sky_tex=tex_find("SKY1");
    if(skytexture>=0 && skytexture<num_textures) sky_tex=&textures[skytexture];
}

// ---------- sprites (R_InitSpriteDefs) ----------
typedef struct { int16_t lump[8]; uint8_t flip[8]; int8_t rotate; } SprFrame;
typedef struct { int n; SprFrame f[29]; } SprDef;
static SprDef sprdefs[NUMSPRITES];
#pragma pack(push,1)
typedef struct { int16_t width,height,leftoffset,topoffset; int32_t columnofs[1]; } SprPatch;
#pragma pack(pop)

void RV_Init(WAD *w){
    rw=w;
    int sz; uint8_t *cm=wad_get_data(w,"COLORMAP",&sz);
    if(cm && sz>=34*256) memcpy(colormap,cm,34*256);
    memset(sprdefs,0,sizeof(sprdefs));
    int start=-1,end=-1;
    for(int i=0;i<w->numlumps;i++){
        if(!strcmp(w->lumps[i].name,"S_START")||!strcmp(w->lumps[i].name,"SS_START")) start=i;
        if(!strcmp(w->lumps[i].name,"S_END")||!strcmp(w->lumps[i].name,"SS_END")) end=i;
    }
    for(int i=start+1;i<end && start>=0;i++){
        const char *nm=w->lumps[i].name; int len=strlen(nm);
        if(len<6||!w->lumps[i].data) continue;
        int sp=-1; for(int k=0;k<NUMSPRITES;k++) if(!strncmp(nm,sprnames[k],4)){ sp=k; break; }
        if(sp<0) continue;
        for(int pass=0;pass<2;pass++){
            if(pass==1&&len<8) break;
            int frame=nm[4+pass*2]-'A', rot=nm[5+pass*2]-'0';
            if(frame<0||frame>=29||rot<0||rot>8) continue;
            SprDef *sd=&sprdefs[sp]; SprFrame *sf=&sd->f[frame];
            if(frame+1>sd->n) sd->n=frame+1;
            if(rot==0){ sf->rotate=0; for(int r=0;r<8;r++){ sf->lump[r]=i; sf->flip[r]=pass; } }
            else { sf->rotate=1; sf->lump[rot-1]=i; sf->flip[rot-1]=pass; }
        }
    }
}

void RV_SetPalette(const uint8_t *p768){
    for(int i=0;i<256;i++) pal32[i]=0xFF000000u|(p768[i*3]<<16)|(p768[i*3+1]<<8)|p768[i*3+2];
}

// ---------- luz (R_InitLightTables / scalelight) ----------
static inline int light_level(int light,double depth,int hv){
    int ln=(light>>4)+extralight_v+hv; if(ln<0) ln=0; if(ln>15) ln=15;
    int startmap=(15-ln)*4;
    int j=(int)(2560.0/(depth<1?1:depth)); if(j>47) j=47;
    int lv=startmap-j/2; if(lv<0) lv=0; if(lv>31) lv=31;
    return lv;
}
#define CM(lv,idx) colormap[(lv)*256+(idx)]
static inline int pmod(int a,int m){ int r=a%m; return r<0? r+m: r; }
static inline int clampi(double v,int lo,int hi){ int i=(int)floor(v); return i<lo?lo:(i>hi?hi:i); }
static inline double w2s(double wz,double perp){ return HALF_H-(wz-vz)*PROJY/perp; }

static Texture *wall_tex(int n){ if(n<=0||n>=num_textures) return NULL; return &textures[texturetranslation[n]]; }
static Flat *flat_of(int pic){ if(pic<0||pic>=num_flats) return NULL; return &flats[flattranslation[pic]]; }

// ---------- columnas ----------
typedef struct { int col; double ox,oy,dx,dy,cosd,angle; int top,bot,closed; } ColState;
typedef struct { float perp; short top,bot; Texture *t; int tex_x,yofs,lv; double tz; } MaskCol;
typedef struct { int n; float perp[MAXREC]; short top[MAXREC], bot[MAXREC]; int nm; MaskCol m[MAXMASK]; } ColClip;
static ColClip colclip[WIDTH];

static void span_wall(int col,int y0,int y1,Texture *t,int tex_x,double top_z,int yofs,double perp,int lv){
    if(y0<0) y0=0; if(y1>VIEWH) y1=VIEWH;
    for(int y=y0;y<y1;y++){
        int c;
        if(t && t->pixels){
            double z=vz-(y+0.5-HALF_H)*perp/PROJY;
            int v=pmod((int)floor(top_z-z)+yofs, t->height);
            int idx=t->pixels[v*t->width+pmod(tex_x,t->width)];
            c=CM(lv,idx==255?0:idx);
        } else c=CM(lv,100);
        vbuf[y][col]=c;
    }
}
static void span_flat(int col,int y0,int y1,Flat *fl,double plane_z,int light,double ox,double oy,double dx,double dy,double cosd){
    if(y0<0) y0=0; if(y1>VIEWH) y1=VIEWH;
    for(int y=y0;y<y1;y++){
        double den=(y+0.5-HALF_H);
        if(fabs(den)<1e-3) continue;
        double depth=(vz-plane_z)*PROJY/den;
        if(depth<=0) continue;
        double dist=depth/cosd;
        int fx=(int)floor(ox+dx*dist), fy=(int)floor(oy+dy*dist);
        int idx= fl? fl->data[((-fy)&63)*64+(fx&63)] : 100;
        vbuf[y][col]=fixedcm? CM(fixedcm,idx) : CM(light_level(light,depth,0),idx);
    }
}
static void span_sky(int col,int y0,int y1,double angle){
    if(y0<0) y0=0; if(y1>VIEWH) y1=VIEWH;
    int tx=(int)(fmod(angle/(2*M_PI)+1.0,1.0)*1024.0);
    for(int y=y0;y<y1;y++){
        if(sky_tex && sky_tex->pixels){
            int v=(int)(100+(y-HALF_H)/(PROJY/160.0)); if(v<0)v=0; if(v>sky_tex->height-1)v=sky_tex->height-1;
            int idx=sky_tex->pixels[v*sky_tex->width+pmod(tx,sky_tex->width)];
            vbuf[y][col]=fixedcm? CM(fixedcm,idx): idx;
        } else vbuf[y][col]=100;
    }
}

static void clip_record(ColState *cs,double perp){
    ColClip *cc=&colclip[cs->col];
    if(cc->n<MAXREC){ cc->perp[cc->n]=(float)perp; cc->top[cc->n]=(short)cs->top; cc->bot[cc->n]=(short)cs->bot; cc->n++; }
}

static void process_seg(ColState *cs,const RSeg *sg){
    double vxx=sg->x2-sg->x1, vyy=sg->y2-sg->y1;
    double den=cs->dx*vyy-cs->dy*vxx;
    if(den>=0) return;
    double t=((sg->x1-cs->ox)*vyy-(sg->y1-cs->oy)*vxx)/den;
    double u=((sg->x1-cs->ox)*cs->dy-(sg->y1-cs->oy)*cs->dx)/den;
    if(t<=0.01 || u<-1e-6 || u>1+1e-6) return;
    sector_t *N=sg->front, *F=sg->back; side_t *sd=sg->sd;
    if(!sd||!N) return;
    int col=cs->col, top=cs->top, bot=cs->bot;
    double perp=t*cs->cosd; if(perp<1) perp=1;
    int tex_x=(int)(sg->offset+u*sg->len)+(sd->textureoffset>>16);
    int yofs=sd->rowoffset>>16;
    int lv= fixedcm? fixedcm : light_level(N->lightlevel,perp,sg->hv);
    double ox=cs->ox, oy=cs->oy, dx=cs->dx, dy=cs->dy, cosd=cs->cosd;
    double nceil=N->ceilingheight/65536.0, nfloor=N->floorheight/65536.0;
    int nsky=(N->ceilingpic==skyflatnum);

    int yc=clampi(w2s(nceil,perp),top,bot);
    int yf=clampi(w2s(nfloor,perp),top,bot);
    if(nsky) span_sky(col,top,yc,cs->angle);
    else span_flat(col,top,yc,flat_of(N->ceilingpic),nceil,N->lightlevel,ox,oy,dx,dy,cosd);
    span_flat(col,yf,bot,flat_of(N->floorpic),nfloor,N->lightlevel,ox,oy,dx,dy,cosd);

    int lflags=sg->s->linedef->flags;
    if(!F){
        Texture *tx=wall_tex(sd->midtexture);
        double tz=(lflags&ML_DONTPEGBOTTOM)? nfloor+(tx?tx->height:0) : nceil;
        span_wall(col,yc,yf,tx,tex_x,tz,yofs,perp,lv);
        cs->top=cs->bot=0; cs->closed=1; clip_record(cs,perp);
        return;
    }
    double fceil=F->ceilingheight/65536.0, ffloor=F->floorheight/65536.0;
    if(nsky && F->ceilingpic==skyflatnum) fceil=nceil;                 // truco del cielo
    int ntop=yc, nbot=yf;
    if(fceil<nceil){
        int yb=clampi(w2s(fceil,perp),yc,yf);
        Texture *tx=wall_tex(sd->toptexture);
        double tz=(lflags&ML_DONTPEGTOP)? nceil : fceil+(tx?tx->height:0);
        span_wall(col,yc,yb,tx,tex_x,tz,yofs,perp,lv);
        ntop=yb;
    }
    if(ffloor>nfloor){
        int yb=clampi(w2s(ffloor,perp),ntop,yf);
        Texture *tx=wall_tex(sd->bottomtexture);
        double tz=(lflags&ML_DONTPEGBOTTOM)? nceil : ffloor;
        span_wall(col,yb,yf,tx,tex_x,tz,yofs,perp,lv);
        nbot=yb;
    }
    // textura intermedia con transparencia (rejas, etc.): se guarda y se dibuja junto con los sprites
    if(sd->midtexture){
        Texture *tx=wall_tex(sd->midtexture); ColClip *cc=&colclip[col];
        if(tx && cc->nm<MAXMASK){
            MaskCol *m=&cc->m[cc->nm++];
            double lo=nfloor>ffloor?nfloor:ffloor, hi=nceil<fceil?nceil:fceil;
            if(nsky && F->ceilingpic==skyflatnum) hi=nceil<fceil?nceil:fceil;
            m->perp=(float)perp; m->top=(short)ntop; m->bot=(short)nbot; m->t=tx; m->tex_x=tex_x; m->yofs=yofs; m->lv=lv;
            m->tz=(lflags&ML_DONTPEGBOTTOM)? lo+tx->height : hi;
        }
    }
    cs->top=ntop; cs->bot=nbot;
    if(ntop>=nbot) cs->closed=1;
    clip_record(cs,perp);
}

static int point_on_side(double x,double y,const RNode *n){
    if(n->dx==0){ if(x<=n->x) return n->dy>0; return n->dy<0; }
    if(n->dy==0){ if(y<=n->y) return n->dx<0; return n->dx>0; }
    double dx=x-n->x, dy=y-n->y;
    return (dy*n->dx < n->dy*dx)? 0 : 1;
}
static void bsp_subsector(ColState *cs,int ss){
    if(ss<0||ss>=numsubsectors) return;
    subsector_t *s=&subsectors[ss];
    for(int i=0;i<s->numlines && !cs->closed;i++) process_seg(cs,&rsegs[s->firstline+i]);
}
static int ray_hits_bbox(const ColState *cs,const double *bb){   // bb = top,bottom,left,right
    double ymax=bb[0]+1, ymin=bb[1]-1, xmin=bb[2]-1, xmax=bb[3]+1;
    double tmin=0, tmax=1e30;
    if(fabs(cs->dx)<1e-12){ if(cs->ox<xmin||cs->ox>xmax) return 0; }
    else { double a=(xmin-cs->ox)/cs->dx, b=(xmax-cs->ox)/cs->dx; if(a>b){double q=a;a=b;b=q;} if(a>tmin)tmin=a; if(b<tmax)tmax=b; }
    if(fabs(cs->dy)<1e-12){ if(cs->oy<ymin||cs->oy>ymax) return 0; }
    else { double a=(ymin-cs->oy)/cs->dy, b=(ymax-cs->oy)/cs->dy; if(a>b){double q=a;a=b;b=q;} if(a>tmin)tmin=a; if(b<tmax)tmax=b; }
    return tmin<=tmax;
}
static void bsp_node(ColState *cs,int nodenum){
    if(cs->closed) return;
    if(nodenum&NF_SUBSECTOR){ bsp_subsector(cs,nodenum&~NF_SUBSECTOR); return; }
    const RNode *n=&rnodes[nodenum];
    int side=point_on_side(vx,vy,n);
    bsp_node(cs,n->child[side]);
    if(cs->closed) return;
    if(ray_hits_bbox(cs,n->bbox[side^1])) bsp_node(cs,n->child[side^1]);
}

// ---------- sprites ----------
typedef struct { mobj_t *mo; double tz; } VisSpr;
static int visspr_cmp(const void *a,const void *b){ double d=((const VisSpr*)b)->tz-((const VisSpr*)a)->tz; return d<0?-1:d>0?1:0; }
static int mask_next[WIDTH];          // siguiente textura intermedia pendiente por columna (de lejos a cerca)

static void draw_mask_col(int cx,MaskCol *m){
    int wtop=m->top, wbot=m->bot; if(wtop>=wbot) return;
    Texture *t=m->t; if(!t||!t->pixels) return;
    int tx=pmod(m->tex_x,t->width);
    for(int y=wtop;y<wbot;y++){
        double z=vz-(y+0.5-HALF_H)*m->perp/PROJY;
        double v=m->tz-z+m->yofs;                  // fila de textura (sin repetir en vertical)
        int vi=(int)floor(v); if(vi<0||vi>=t->height) continue;
        int idx=t->pixels[vi*t->width+tx]; if(idx==255) continue;
        vbuf[y][cx]=CM(m->lv,idx);
    }
}
static void flush_masks(int cx,double tz){   // dibuja las texturas intermedias más lejanas que tz
    ColClip *cc=&colclip[cx];
    for(int k=cc->nm-1-mask_next[cx];k>=0;k--){
        if(cc->m[k].perp<=tz) break;
        draw_mask_col(cx,&cc->m[k]); mask_next[cx]++;
    }
}

static void draw_patch_cols(const SprPatch *p,int flip,double x1,double sx,double ytop,double sy,int lv,int shadow,double tz,int usetz){
    int cx0=(int)floor(x1); if(cx0<0) cx0=0; int cx1=(int)ceil(x1+p->width*sx); if(cx1>WIDTH) cx1=WIDTH;
    for(int cx=cx0;cx<cx1;cx++){
        int wtop=0, wbot=VIEWH;
        if(usetz){
            ColClip *cc=&colclip[cx];
            flush_masks(cx,tz);
            for(int k=0;k<cc->n && cc->perp[k]<tz;k++){ wtop=cc->top[k]; wbot=cc->bot[k]; }
        }
        if(wtop>=wbot) continue;
        int tc=(int)floor((cx+0.5-x1)/sx); if(tc<0||tc>=p->width) continue;
        if(flip) tc=p->width-1-tc;
        const uint8_t *col=(const uint8_t*)p+p->columnofs[tc];
        while(col[0]!=0xff){
            int td=col[0], len=col[1];
            int y0=(int)ceil(ytop+td*sy-0.5), y1=(int)ceil(ytop+(td+len)*sy-0.5);
            if(y0<wtop) y0=wtop; if(y1>wbot) y1=wbot;
            for(int y=y0;y<y1;y++){
                int r=(int)floor((y+0.5-ytop)/sy)-td; if(r<0) r=0; if(r>=len) r=len-1;
                if(shadow){
                    int yy=y+(((cx*7+y*13)>>2)%3)-1; if(yy<0) yy=0; if(yy>=VIEWH) yy=VIEWH-1;
                    vbuf[y][cx]=CM(6,vbuf[yy][cx]);
                } else vbuf[y][cx]=CM(lv,col[3+r]);
            }
            col+=len+4;
        }
    }
}

static void draw_sprites(player_t *pl){
    static VisSpr *vs=NULL; static int vcap=0; int nv=0;
    double cs=cos(vang), sn=sin(vang);
    for(thinker_t *th=thinkercap.next; th!=&thinkercap; th=th->next){
        if(th->function.acp1!=(actionf_p1)P_MobjThinker) continue;
        mobj_t *m=(mobj_t*)th;
        if(m==pl->mo || (m->flags&MF_NOSECTOR) || !m->subsector) continue;
        double dx=m->x/65536.0-vx, dy=m->y/65536.0-vy, tz=dx*cs+dy*sn;
        if(tz<4) continue;
        if(nv>=vcap){ vcap=vcap?vcap*2:256; vs=realloc(vs,vcap*sizeof(VisSpr)); }
        vs[nv].mo=m; vs[nv].tz=tz; nv++;
    }
    qsort(vs,nv,sizeof(VisSpr),visspr_cmp);
    for(int v=0;v<nv;v++){
        mobj_t *m=vs[v].mo; double tz=vs[v].tz;
        SprDef *sd=&sprdefs[m->sprite]; int frame=m->frame&FF_FRAMEMASK;
        if(frame>=sd->n) continue;
        SprFrame *sf=&sd->f[frame]; int rot=0;
        if(sf->rotate){
            viewx=(fixed_t)(vx*65536.0); viewy=(fixed_t)(vy*65536.0);
            angle_t ang=R_PointToAngle(m->x,m->y);
            rot=(ang-m->angle+(unsigned)(ANG45/2)*9)>>29;
        }
        int lump=sf->lump[rot], flip=sf->flip[rot];
        if(lump<=0||lump>=rw->numlumps) continue;
        const SprPatch *p=(const SprPatch*)rw->lumps[lump].data; if(!p) continue;
        double mx=m->x/65536.0, my=m->y/65536.0, mz=m->z/65536.0;
        double sx=PROJ/tz, sy=PROJY/tz;
        double tr=(mx-vx)*sn-(my-vy)*cs;
        double lo=flip? p->width-p->leftoffset : p->leftoffset;
        double x1=HALF_W+(tr-lo)*sx;
        double ytop=HALF_H-((mz+p->topoffset)-vz)*sy;
        int fullbright=(m->frame&FF_FULLBRIGHT)!=0, shadow=(m->flags&MF_SHADOW)!=0;
        int lv= fixedcm? fixedcm : fullbright? 0 : light_level(m->subsector->sector->lightlevel,tz,0);
        draw_patch_cols(p,flip,x1,sx,ytop,sy,lv,shadow,tz,1);
    }
    // texturas intermedias restantes (más cercanas que todos los sprites)
    for(int cx=0;cx<WIDTH;cx++) flush_masks(cx,-1);
}

static void draw_weapon(player_t *pl){
    for(int i=0;i<NUMPSPRITES;i++){
        pspdef_t *psp=&pl->psprites[i]; if(!psp->state) continue;
        state_t *st=psp->state;
        SprDef *sd=&sprdefs[st->sprite]; int frame=st->frame&FF_FRAMEMASK;
        if(frame>=sd->n) continue;
        SprFrame *sf=&sd->f[frame];
        int lump=sf->lump[0], flip=sf->flip[0];
        if(lump<=0||lump>=rw->numlumps) continue;
        const SprPatch *p=(const SprPatch*)rw->lumps[lump].data; if(!p) continue;
        double kx=WIDTH/320.0, ky=PROJY/160.0;
        double tx=psp->sx/65536.0-160-p->leftoffset;
        if(flip) tx=psp->sx/65536.0-160-(p->width-p->leftoffset);
        double x1=HALF_W+tx*kx;
        double texturemid=100.5-(psp->sy/65536.0-p->topoffset);
        double ytop=HALF_H-texturemid*ky;
        int shadow=(pl->powers[pw_invisibility]>4*32) || (pl->powers[pw_invisibility]&8);
        int lv;
        if(pl->fixedcolormap) lv=pl->fixedcolormap;
        else if(st->frame&FF_FULLBRIGHT) lv=0;
        else lv=light_level(pl->mo->subsector->sector->lightlevel,1,0);
        draw_patch_cols(p,flip,x1,kx,ytop,ky,lv,shadow,0,0);
    }
}

void RV_Render(player_t *pl,double x,double y,double z,double ang){
    vx=x; vy=y; vz=z; vang=ang;
    extralight_v=pl->extralight; fixedcm=pl->fixedcolormap;
    memset(vbuf,0,sizeof(vbuf));
    for(int c=0;c<WIDTH;c++){ colclip[c].n=0; colclip[c].nm=0; mask_next[c]=0; }
    for(int col=0;col<WIDTH;col++){
        double a=ang+atan2(HALF_W-col-0.5,PROJ);
        ColState cs={col,x,y,cos(a),sin(a),cos(a-ang),a,0,VIEWH,0};
        if(rnn==0) bsp_subsector(&cs,0); else bsp_node(&cs,rnn-1);
    }
    draw_sprites(pl);
    draw_weapon(pl);
}

// compone: pantalla 320x200 del juego (255 = transparente) a 640x480 con escala uniforme (x2, y x2.4 = píxel de Doom 1.2:1);
// con show3d, lo transparente de las primeras 168 líneas deja ver la vista 3D.
void RV_Compose(uint32_t *out,int outw,int outh,const uint8_t *scr,int show3d){
    for(int yy=0;yy<outh;yy++){
        const uint8_t *src=scr+(yy*200/outh)*320; uint32_t *o=out+yy*outw; const uint8_t *vr= (show3d&&yy<VIEWH)? vbuf[yy] : NULL;
        for(int xx=0;xx<outw;xx++){ int p=src[xx>>1]; if(p==255) p= vr? vr[xx] : 0; o[xx]=pal32[p]; }
    }
}
