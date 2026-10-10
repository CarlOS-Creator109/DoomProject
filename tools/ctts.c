/* ctts.c - CTTS (Carlos TTS): sintetizador de formantes (estilo Klatt) para espanol, punto flotante (double).
 * Valores de vocales tomados de espeak-ng (phsource/vowel). Reglas de letras basadas en
 * dictsource/es_rules (espanol latinoamericano: z,ce,ci -> s; ll,y -> J).
 * Compilar: gcc -O2 -o ctts ctts.c -lm
 * Uso:      ./ctts "hola mundo" salida.wav [velocidad=1.0] [f0=120]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(a,b) _mkdir(a)
#endif

static double SR = 22050.0;
#define PI 3.14159265358979323846
#define NP 16

/* valores leidos de un archivo .ctt (clave interna en mayusculas) */
static struct { char k[32]; double v; } cfg[256]; static int ncfg;
static double tun(const char *n, double d) {
    const char *e = getenv(n); if (e) return atof(e);
    for (int i = 0; i < ncfg; i++) if (!strcmp(cfg[i].k, n)) return cfg[i].v;
    return d;
}
static double T_TR, VDUR, CDUR, BURST, VOTS, CLOS, ASP, FRICG, FSCALE, NASAV, NASB1, APXAV, STRESSD, F0DECL, TILT, NOISEV, TAIL, LEAD, NF1, NB2, NB3, NZA, NZG, NF2S, LF1, LF2S, LAV, LB1, TAPAV, TAPD, TRD, TRN, TRF, NVN, GLD, GLAV, ACCF, UNSF, QRISE, FALL, G_F0, G_SPEED, PCOMA, PPUNTO, JITTER, VOL, VIBD, VIBR, VOZG, OSCG, DRUMG, HAMP, RBASE, SRATE, VOZM, SININT, IPAP, IPAY;
static int SUNG = 0; static double VSTRETCH = 1.0;
static void load_tun(void) {
    T_TR=tun("T_TR",0.020); VDUR=tun("VDUR",1.0); CDUR=tun("CDUR",1.0); BURST=tun("BURST",0.6); VOTS=tun("VOTS",1.0); CLOS=tun("CLOS",1.0);
    ASP=tun("ASP",1.0); FRICG=tun("FRICG",1.0); FSCALE=tun("FSCALE",1.0); NASAV=tun("NASAV",0.75); NASB1=tun("NASB1",160); APXAV=tun("APXAV",0.60);
    STRESSD=tun("STRESSD",1.50); F0DECL=tun("F0DECL",0.14); TILT=tun("TILT",1.0); NOISEV=tun("NOISEV",0.0008); TAIL=tun("TAIL",0.10); LEAD=tun("LEAD",0.0); NF1=tun("NF1",250); NB2=tun("NB2",150); NB3=tun("NB3",200); NZA=tun("NZA",1.0); NZG=tun("NZG",0.8); NF2S=tun("NF2S",1.0); LF1=tun("LF1",1.0); LF2S=tun("LF2S",1.0); LAV=tun("LAV",0.75); LB1=tun("LB1",80); TAPAV=tun("TAPAV",0.15); TAPD=tun("TAPD",1.0); TRD=tun("TRD",1.0); TRN=tun("TRN",3); TRF=tun("TRF",0.12); NVN=tun("NVN",0.7); GLD=tun("GLD",1.0); GLAV=tun("GLAV",0.8); ACCF=tun("ACCF",1.10); UNSF=tun("UNSF",0.97); QRISE=tun("QRISE",0.30); FALL=tun("FALL",0.10); G_F0=tun("F0",118); G_SPEED=tun("SPEED",1.05); PCOMA=tun("PCOMA",0.22); PPUNTO=tun("PPUNTO",0.40); JITTER=tun("JITTER",0.004); VOL=tun("VOL",0.85); VIBD=tun("VIBD",0.006); VIBR=tun("VIBR",5.5); VOZG=tun("VOZG",1.0); OSCG=tun("OSCG",0.30); DRUMG=tun("DRUMG",0.8); HAMP=tun("HAMP",2.0); RBASE=tun("BASE",0.25); SRATE=tun("SRATE",0); VOZM=tun("VOZM",0); SININT=tun("SININT",0); IPAP=tun("IPAP",1); IPAY=tun("IPAY",1);
}
/* ---------- parametros de sintesis (se interpolan linealmente entre keyframes) ---------- */
enum { F1, F2, F3, F4, F5, B1, B2, B3, B4, B5, AV, AH, AF, FFC, FBW, NZ, NZF, F0M };
typedef struct { double t; double p[NP + 2]; } Key;
static Key *keys; static int nkeys, capkeys;
static double cur[NP + 2];           /* estado "actual" al construir keyframes */
static double T;                     /* tiempo de construccion en segundos */

static void defaults(double *p) {
    p[F1]=500; p[F2]=1500; p[F3]=2500; p[F4]=3400; p[F5]=3900;
    p[B1]=60;  p[B2]=90;   p[B3]=150;  p[B4]=250;  p[B5]=300;
    p[AV]=0; p[AH]=0; p[AF]=0; p[FFC]=4000; p[FBW]=2000; p[NZ]=0; p[NZF]=1000; p[F0M]=1.0;
}
static void key(double t, const double *p) {
    if (nkeys == capkeys) { capkeys = capkeys ? capkeys * 2 : 4096; keys = realloc(keys, capkeys * sizeof(Key)); }
    if (nkeys && t < keys[nkeys-1].t) t = keys[nkeys-1].t;   /* monotono */
    keys[nkeys].t = t; memcpy(keys[nkeys].p, p, sizeof(double) * (NP + 2)); nkeys++;
}

/* ---------- tabla de fonemas ---------- */
typedef struct {
    char c; char cls;  /* V vocal, G glide, N nasal, L lateral, T tap, R trino, F fricativa,
                          S oclusiva, A aproximante, C africada */
    int voiced; double f1, f2, f3, dur, nfc, nbw, namp, zero;
} Ph;

static Ph PH[] = {
 /* vocales (espeak-ng: a_4, e_mid2, i, oo, u_bck2) */
 {'a','V',1, 840,1410,2550, 0.105, 0,0,0, 0},
 {'e','V',1, 504,1900,2540, 0.100, 0,0,0, 0},
 {'i','V',1, 290,2270,2840, 0.095, 0,0,0, 0},
 {'o','V',1, 552, 870,2660, 0.105, 0,0,0, 0},
 {'u','V',1, 330, 830,2350, 0.095, 0,0,0, 0},
 {'j','G',1, 290,2270,2840, 0.050, 0,0,0, 0},   /* semiconsonante i */
 {'w','G',1, 330, 830,2350, 0.050, 0,0,0, 0},   /* semiconsonante u */
 /* nasales */
 {'m','N',1, 250,1100,2200, 0.075, 0,0,0, 1000},
 {'n','N',1, 250,1500,2500, 0.070, 0,0,0, 1450},
 {'N','N',1, 250,2100,2800, 0.085, 0,0,0, 2000},   /* n~ */
 /* liquidas */
 {'l','L',1, 360,1150,2750, 0.070, 0,0,0, 0},
 {'r','T',1, 400,1400,2400, 0.030, 0,0,0, 0},      /* vibrante simple */
 {'R','R',1, 400,1400,2400, 0.095, 0,0,0, 0},      /* vibrante multiple */
 /* fricativas */
 {'s','F',0, 500,1500,2500, 0.105, 6000,2800, 0.38, 0},
 {'f','F',0, 500,1500,2500, 0.085, 4500,6000, 0.27, 0},
 {'x','F',0, 500,1500,2500, 0.100, 2800,2200, 0.80, 0},   /* j velar */
 {'J','F',1, 280,2200,2900, 0.075, 3600,1800, 0.25, 0},   /* y, ll */
 {'h','F',0, 500,1500,2500, 0.000, 0,0,0, 0},
 /* oclusivas: f1,f2,f3 = locus */
 {'p','S',0, 200, 900,2200, 0.085, 1500,2500, 0.55, 0},
 {'t','S',0, 200,1800,2700, 0.085, 4500,2800, 0.75, 0},
 {'k','S',0, 200,1600,2300, 0.090, 2300,1400, 0.65, 0},
 {'b','S',1, 200, 900,2200, 0.070, 1500,2500, 0.30, 0},
 {'d','S',1, 200,1800,2700, 0.070, 4500,2800, 0.40, 0},
 {'g','S',1, 200,1600,2300, 0.070, 2300,1400, 0.35, 0},
 /* aproximantes (b,d,g entre vocales) */
 {'B','A',1, 300, 950,2200, 0.055, 1500,3000, 0.10, 0},
 {'D','A',1, 300,1600,2700, 0.055, 4500,3000, 0.10, 0},
 {'G','A',1, 300,1500,2400, 0.055, 2300,1800, 0.10, 0},
 /* africada ch */
 {'C','C',0, 250,2000,2800, 0.130, 3400,1800, 0.90, 0},
 /* --- extension IPA: vocales --- */
       {'I','V',1, 390,1990,2550, 0.080, 0,0,0, 0},
 {'E','V',1, 530,1840,2480, 0.095, 0,0,0, 0},
 {'A','V',1, 660,1720,2410, 0.115, 0,0,0, 0},
 {'P','V',1, 730,1090,2440, 0.125, 0,0,0, 0},
 {'Q','V',1, 590, 880,2540, 0.105, 0,0,0, 0},
 {'O','V',1, 570, 840,2410, 0.115, 0,0,0, 0},
 {'U','V',1, 440,1020,2240, 0.085, 0,0,0, 0},
 {'V','V',1, 640,1190,2390, 0.100, 0,0,0, 0},
 {'@','V',1, 500,1500,2500, 0.070, 0,0,0, 0},
 {'3','V',1, 490,1350,1690, 0.120, 0,0,0, 0},
 {'1','V',1, 330,1550,2350, 0.090, 0,0,0, 0},
 /* consonantes */
 {'v','F',1, 300,1100,2200, 0.075, 4000,4000, 0.15, 0},
 {'z','F',1, 300,1500,2500, 0.085, 6000,2800, 0.30, 0},
 {'T','F',0, 500,1500,2500, 0.085, 5000,5000, 0.14, 0},
 {'S','F',0, 500,1500,2500, 0.110, 2700,2200, 0.42, 0},
 {'Y','F',1, 300,1800,2400, 0.090, 2700,2200, 0.22, 0},
 {'M','N',1, 250,1900,2500, 0.085, 0,0,0, 1600},
 {'L','L',1, 350,1300,1650, 0.075, 0,0,0, 0},
 {'H','H',0, 500,1500,2500, 0.075, 0,0,0, 0},
 {'Z','C',1, 250,2000,2800, 0.110, 3200,2200, 0.55, 0},
 {0}
};
static Ph *phf(char c) { for (Ph *p = PH; p->c; p++) if (p->c == c) return p; return NULL; }
static int isvow(char c) { return c=='a'||c=='e'||c=='i'||c=='o'||c=='u'; }

/* ---------- texto -> fonemas ---------- */
typedef struct { char c; int stress; int wend; double pause; int qend; int cend; int off; } Seg;
static Seg seg[20000]; static int nseg; static double segT[20000];

static void put(char c, int stress, int wend) { seg[nseg].c=c; seg[nseg].stress=stress; seg[nseg].wend=wend; seg[nseg].pause=0; seg[nseg].off=0; seg[nseg].qend=0; seg[nseg].cend=0; nseg++; }
static void pause(double s, int q, int comma) {
    if (nseg && seg[nseg-1].c==' ') { if (s > seg[nseg-1].pause) seg[nseg-1].pause = s; seg[nseg-1].qend |= q; seg[nseg-1].cend |= comma; return; }
    seg[nseg].c=' '; seg[nseg].stress=0; seg[nseg].wend=0; seg[nseg].off=0; seg[nseg].pause=s; seg[nseg].qend=q; seg[nseg].cend=comma; nseg++;
}

/* codigos internos: 1=a' 2=e' 3=i' 4=o' 5=u' 6=u" 7=n~ */
static void word(const char *w) {
    int n = strlen(w); if (!n) return;
    /* 1) letras -> fonemas con marca de letra acentuada */
    char ph[256]; int acc[256]; int np = 0; int accpos = -1;
    #define EMIT(ch,a) do{ ph[np]=(ch); acc[np]=(a); np++; }while(0)
    for (int i = 0; i < n; i++) {
        unsigned char c = w[i], nx = (i+1<n)? w[i+1]:0, pv = i? w[i-1]:0;
        int nxv = strchr("aeiou\1\2\3\4\5\6", nx) != NULL && nx;
        int nxf = (nx=='e'||nx=='i'||nx==2||nx==3);
        int a = 0; char b = c;
        if (c>=1 && c<=5) { a = 1; b = "aeiou"[c-1]; }
        switch (b) {
        case 'a': case 'e': case 'i': case 'o': case 'u': EMIT(b, a); break;
        case 6: EMIT('w', 0); break;
        case 'b': case 'v': EMIT(i==0||pv=='n'||pv=='m'?'b':'B', 0); break;
        case 'c':
            if (nx=='h') { EMIT('C',0); i++; }
            else if (nxf) EMIT('s',0); else EMIT('k',0); break;
        case 'd': EMIT((i==0||pv=='n'||pv=='l')?'d':'D', 0); break;
        case 'f': EMIT('f',0); break;
        case 'g':
            if (nxf) { EMIT('x',0); }
            else if (nx=='u' && i+2<n && (w[i+2]=='e'||w[i+2]=='i'||w[i+2]==2||w[i+2]==3)) { EMIT(i==0||pv=='n'?'g':'G',0); i++; }
            else EMIT((i==0||pv=='n')?'g':'G', 0);
            break;
        case 'h': break;
        case 'j': EMIT('x',0); break;
        case 'k': EMIT('k',0); break;
        case 'l': if (nx=='l') { EMIT('J',0); i++; } else EMIT('l',0); break;
        case 'm': EMIT('m',0); break;
        case 'n': EMIT(nx=='p'||nx=='b'||nx=='v'||nx=='m'? 'm':'n',0); break;
        case 7:   EMIT('N',0); break;
        case 'p': EMIT('p',0); break;
        case 'q': EMIT('k',0); if (nx=='u') i++; break;
        case 'r': if (nx=='r') { EMIT('R',0); i++; }
                  else if (i==0 || pv=='n'||pv=='l'||pv=='s') EMIT('R',0); else EMIT('r',0); break;
        case 's': EMIT('s',0); break;
        case 't': EMIT('t',0); break;
        case 'w': EMIT('w',0); break;
        case 'x': if (i==0) EMIT('s',0); else { EMIT('k',0); EMIT('s',0); } break;
        case 'y': if (i==n-1) EMIT('i',0); else if (n==1) EMIT('i',0); else EMIT('J',0); break;
        case 'z': EMIT('s',0); break;
        default: break;
        }
    }
    /* 2) diptongos: i/u atonas junto a vocal -> semiconsonante. */
    for (int i = 0; i < np; i++) {
        if ((ph[i]=='i'||ph[i]=='u') && !acc[i]) {
            int nv = (i+1<np && isvow(ph[i+1])), pvw = (i>0 && isvow(ph[i-1]));
            if (nv && (i==0 || !pvw || !(ph[i-1]=='i'||ph[i-1]=='u'))) ph[i] = ph[i]=='i'?'j':'w';
            else if (pvw && !nv) ph[i] = ph[i]=='i'?'j':'w';
            else if (nv && pvw) ph[i] = ph[i]=='i'?'j':'w';
        }
    }
    /* 3) posicion del acento: nucleos = vocales (no glides) */
    int nuc[64], nn = 0, stressnuc = -1;
    for (int i = 0; i < np; i++) if (isvow(ph[i])) { nuc[nn++] = i; if (acc[i]) stressnuc = nn-1; }
    if (stressnuc < 0 && nn) {
        char last = ph[np-1];
        int grave = isvow(last) || last=='n' || last=='s' || last=='j' || last=='w';
        /* 'j'/'w' finales (y, ay..) cuentan como vocal final -> llana si terminan en glide tras vocal */
        if (nn == 1) stressnuc = 0;
        else stressnuc = (grave && !(last=='j'||last=='w') ) ? nn-2 : nn-1;
    }
    for (int i = 0; i < np; i++) {
        int st = 0; if (stressnuc >= 0 && i == nuc[stressnuc]) st = 1;
        put(ph[i], st, 0);
    }
    if (nseg) seg[nseg-1].wend = 1;
    (void)accpos;
}


/* ---------- normalizacion de texto: numeros, simbolos, abreviaturas ---------- */
static const char *U[] = {"cero","uno","dos","tres","cuatro","cinco","seis","siete","ocho","nueve","diez","once","doce","trece","catorce","quince",
  "diecis\xc3\xa9is","diecisiete","dieciocho","diecinueve","veinte","veintiuno","veintid\xc3\xb3s","veintitr\xc3\xa9s","veinticuatro","veinticinco","veintis\xc3\xa9is","veintisiete","veintiocho","veintinueve"};
static const char *DEC[] = {"","","","treinta","cuarenta","cincuenta","sesenta","setenta","ochenta","noventa"};
static const char *CEN[] = {"","ciento","doscientos","trescientos","cuatrocientos","quinientos","seiscientos","setecientos","ochocientos","novecientos"};
static void cat(char *o, const char *a) { strcat(o, a); strcat(o, " "); }
static void below1000(char *o, int n, int apoc) {           /* apoc: "uno" -> "un" delante de mil/millon */
    if (n == 100) { cat(o, "cien"); return; }
    if (n >= 100) { cat(o, CEN[n/100]); n %= 100; }
    if (n == 0) return;
    if (n < 30) { cat(o, (apoc && (n==1)) ? "un" : (apoc && n==21) ? "veintiun" : U[n]); return; }
    cat(o, DEC[n/10]); if (n%10) { cat(o, "y"); cat(o, (apoc && n%10==1) ? "un" : U[n%10]); }
}
static void num2words(char *o, unsigned long n) {
    if (n == 0) { cat(o, "cero"); return; }
    if (n >= 1000000000UL) { cat(o, "un n\xc3\xbamero enorme"); return; }
    if (n >= 1000000) { unsigned long m = n/1000000; if (m == 1) cat(o, "un mill\xc3\xb3n"); else { below1000(o, m, 1); cat(o, "millones"); } n %= 1000000; }
    if (n >= 1000) { unsigned long m = n/1000; if (m == 1) cat(o, "mil"); else { below1000(o, m, 1); cat(o, "mil"); } n %= 1000; }
    if (n) below1000(o, n, 0);
}
static const char *ABR[][2] = {{"sr","se\xc3\xb1or"},{"sra","se\xc3\xb1ora"},{"srta","se\xc3\xb1orita"},{"dr","doctor"},{"dra","doctora"},{"etc","etc\xc3\xa9tera"},{"ud","usted"},{"uds","ustedes"},{"av","avenida"},{"tel","tel\xc3\xa9" "fono"},{"pag","p\xc3\xa1gina"},{"km","kil\xc3\xb3metros"},{"kg","kilos"},{"cm","cent\xc3\xadmetros"},{"mb","megabytes"},{"gb","gigabytes"},{NULL,NULL}};
static void expand(const char *in, char *out) {
    int n = strlen(in); out[0] = 0; char *o = out;
    for (int i = 0; i < n; ) {
        unsigned char c = in[i];
        if (isdigit(c)) {
            unsigned long v = 0; int j = i;
            while (j < n && (isdigit((unsigned char)in[j]) || (in[j]=='.' && j+3<n+0 && isdigit((unsigned char)in[j+1]) && isdigit((unsigned char)in[j+2]) && isdigit((unsigned char)in[j+3]) && !isdigit((unsigned char)in[j+4<n?j+4:n])))) {
                if (in[j] != '.') v = v*10 + (in[j]-'0'); j++;
            }
            char tmp[512] = ""; num2words(tmp, v); strcat(o, tmp);
            if (j+1 < n && (in[j]==',' || in[j]=='.') && isdigit((unsigned char)in[j+1])) {     /* decimales */
                strcat(o, "coma "); j++;
                while (j < n && isdigit((unsigned char)in[j])) { char t[32] = ""; cat(t, U[in[j]-'0']); strcat(o, t); j++; }
            }
            i = j; continue;
        }
        if (isalpha(c) && (i==0 || !isalpha((unsigned char)in[i-1]))) {       /* abreviatura: palabra + punto */
            int j = i; char w[16]; int k = 0;
            while (j < n && isalpha((unsigned char)in[j]) && k < 15) w[k++] = tolower(in[j++]); w[k] = 0;
            if (j < n && in[j]=='.' && (j+1>=n || in[j+1]==' ')) for (int a = 0; ABR[a][0]; a++) if (!strcmp(w, ABR[a][0])) { strcat(o, ABR[a][1]); strcat(o, " "); i = j+1; goto next; }
        }
        if (c=='%') { strcat(o, " por ciento "); i++; continue; }
        if (c=='+') { strcat(o, " m\xc3\xa1s "); i++; continue; }
        if (c=='=') { strcat(o, " igual a "); i++; continue; }
        if (c=='&') { strcat(o, " y "); i++; continue; }
        if (c=='@') { strcat(o, " arroba "); i++; continue; }
        if (c=='$') { strcat(o, " pesos "); i++; continue; }
        { size_t L = strlen(o); o[L] = c; o[L+1] = 0; i++; }
        next: ;
    }
}

/* lee UTF-8 -> codigos internos, separa palabras/puntuacion */
static void parse(const char *s0) {
    static char sx[200000]; expand(s0, sx); const char *s = sx; if (getenv("CTTS_SHOW")) fprintf(stderr, "[texto] %s\n", sx);
    char buf[256]; int bn = 0; int question = 0; nseg = 0;
    for (const char *q = s; *q; q++) if (*q=='?') question = 1;
    int n = strlen(s);
    for (int i = 0; i <= n; i++) {
        unsigned char c = i<n ? s[i] : '.';
        char out = 0; int flush = 0; double pz = 0; int qe = 0, ce = 0;
        if (c == 0xC3 && i+1<n) {
            unsigned char d = s[++i]; if (d>=0x80 && d<=0x9F) d += 0x20;
            switch (d) { case 0xA1: out=1; break; case 0xA9: out=2; break; case 0xAD: out=3; break;
                         case 0xB3: out=4; break; case 0xBA: out=5; break; case 0xBC: out=6; break;
                         case 0xB1: out=7; break; default: flush = 1; }
        } else if (c == 0xC2) { if (i+1<n) i++; continue; }   /* ¿ ¡ */
        else if (isalpha(c)) out = tolower(c);
        else if (isdigit(c)) {
            static const char *dg[] = {"cero","uno","dos","tres","cuatro","cinco","seis","siete","ocho","nueve"};
            if (bn) { buf[bn]=0; word(buf); bn=0; pause(0.0,0,0); }
            word(dg[c-'0']); pause(0.0,0,0); continue;
        }
        else { flush = 1;
            if (c=='.'||c=='!'||c=='?'||c=='\n'||c=='\0') { pz = (i==n) ? TAIL : PPUNTO; qe = question; }
            else if (c==','||c==';'||c==':') { pz = PCOMA; ce = 1; }
        }
        if (out) { if (bn < 250) buf[bn++] = out; }
        if (flush) {
            if (bn) { buf[bn]=0; word(buf); bn=0; }
            if (pz > 0) pause(pz, qe, ce); else if (nseg && seg[nseg-1].c!=' ') pause(0.0,0,0);
        }
    }
}

/* ---------- planificador: fonemas -> keyframes ---------- */
static double frand(void) { static unsigned s = 12345; s = s*1664525u + 1013904223u; return (s>>8)/16777216.0; }

static void vowel_target(int idx, double *f1, double *f2, double *f3) {
    for (int k = idx; k < nseg; k++) { if (seg[k].c==' ') break;
        Ph *p = phf(seg[k].c); if (p && (p->cls=='V' || p->cls=='G')) { *f1=p->f1; *f2=p->f2; *f3=p->f3; return; } }
    *f1=500; *f2=1500; *f3=2500;
}

static void plan(double speed) {
    double p[NP+2]; defaults(p); memcpy(cur, p, sizeof p);
    T = 0; nkeys = 0;
    p[AV]=0; key(0, p); if (LEAD > 0) { T = LEAD; key(T, p); }
    /* longitud total aproximada para declinacion */
    int nwords = 0; for (int i = 0; i < nseg; i++) if (seg[i].c==' ') nwords++;
    int done_final_stress = 0; (void)done_final_stress;
    int last_stress_idx = -1; for (int i = 0; i < nseg; i++) if (seg[i].stress) last_stress_idx = i;

    for (int i = 0; i < nseg; i++) {
        Seg *s = &seg[i]; segT[i] = T;
        if (s->c == ' ') {
            double d = (s->pause > 0 ? s->pause : 0.020) / speed;
            memcpy(p, cur, sizeof p); p[AV]=0; p[AH]=0; p[AF]=0; p[NZ]=0;
            key(T + 0.012, p); T += d; key(T, p); memcpy(cur, p, sizeof p);
            continue;
        }
        const Ph *ph = phf(s->c); if (!ph) continue;
        double d = ph->dur / speed * ((ph->cls=='V'||ph->cls=='G')?VDUR:CDUR); if (ph->cls=='G') d *= GLD;
        if (SUNG && ph->cls=='V' && !s->off) d *= VSTRETCH;
        double fm = 1.0;   /* multiplicador de F0 segun acento */
        if (ph->cls=='V' || ph->cls=='G') {
            if (s->stress) { d *= STRESSD; fm = ACCF; } else fm = UNSF;
            if (s->wend && !s->stress) d *= 1.10;
            /* alargamiento de la ultima vocal antes de pausa */
            if (i+1<nseg && seg[i+1].c==' ' && seg[i+1].pause > 0.1) d *= 1.25;
        }
        double F1_=ph->f1, F2_=ph->f2, F3_=ph->f3;
        int prevcons = 0; (void)prevcons;

        if (ph->cls=='V' || ph->cls=='G') {
            double tr = fminf(T_TR/speed, d*0.4);
            memcpy(p, cur, sizeof p);
            p[F1]=F1_; p[F2]=F2_; p[F3]=F3_; p[B1]=(ph->cls=='G')?80:60; p[B2]=90; p[B3]=150;
            p[AV]=(ph->cls=='G')?GLAV:1.0; p[AH]=0.01; p[AF]=0; p[NZ]=0; p[F0M]=fm;
            if (ph->cls=='V' && NVN > 0) {
                const Ph *pn = (i+1<nseg && seg[i+1].c!=' ') ? phf(seg[i+1].c) : NULL;
                const Ph *pp = (i>0 && seg[i-1].c!=' ') ? phf(seg[i-1].c) : NULL;
                if ((pn && pn->cls=='N') || (pp && pp->cls=='N')) { p[NZ]=NVN; p[NZF]=(pn&&pn->cls=='N')?pn->zero:pp->zero; p[B1]=90; }
            }
            /* formantes ya se mueven desde el keyframe anterior; llegada en T+tr */
            key(T+tr, p);
            key(T+d-tr, p);
            T += d;
        }
        else if (ph->cls=='N') {
            double tr = fminf(0.020/speed, d*0.3);
            memcpy(p, cur, sizeof p);
            p[F1]=NF1; p[F2]=F2_*NF2S; p[F3]=F3_; p[B1]=NASB1; p[B2]=NB2; p[B3]=NB3;
            p[AV]=NASAV; p[AH]=0; p[AF]=0; p[NZ]=NZA; p[NZF]=ph->zero; p[F0M]=0.97;
            key(T+tr, p); key(T+d-tr, p); T += d;
        }
        else if (ph->cls=='L') {
            double tr = fminf(0.020/speed, d*0.3);
            memcpy(p, cur, sizeof p);
            p[F1]=F1_*LF1; p[F2]=F2_*LF2S; p[F3]=F3_; p[B1]=LB1; p[B2]=120; p[B3]=200;
            p[AV]=LAV; p[AH]=0; p[AF]=0; p[NZ]=0; p[F0M]=0.97;
            key(T+tr, p); key(T+d-tr, p); T += d;
        }
        else if (ph->cls=='T') {       /* tap: cierre breve */
            memcpy(p, cur, sizeof p);
            p[F1]=400; p[F2]=1400; p[F3]=2400; p[AV]=TAPAV; p[NZ]=0; p[AF]=0; d*=TAPD;
            key(T+d*0.2, p); key(T+d*0.8, p);
            double q[NP+2]; memcpy(q, p, sizeof q); q[AV]=0.85; key(T+d, q); T += d;
        }
        else if (ph->cls=='R') {       /* trino: 3 cierres */
            memcpy(p, cur, sizeof p);
            p[F1]=400; p[F2]=1400; p[F3]=2400; p[NZ]=0; p[AF]=0;
            d*=TRD; int ntr=(int)(TRN+0.5); double step = d/ntr;
            for (int k = 0; k < ntr; k++) {
                p[AV]=0.85; key(T+step*0.15, p);
                p[AV]=TRF; key(T+step*0.50, p);
                p[AV]=0.85; key(T+step*0.90, p);
                T += step;
            }
        }
        else if (ph->cls=='H') {       /* h aspirada: ruido por el tracto vocal */
            double tr = fminf(0.012/speed, d*0.3);
            double v1,v2,v3; vowel_target(i+1, &v1,&v2,&v3);
            memcpy(p, cur, sizeof p);
            p[F1]=v1; p[F2]=v2; p[F3]=v3; p[B1]=120; p[B2]=150; p[B3]=200;
            p[AV]=0; p[AH]=HAMP; p[AF]=0; p[NZ]=0; p[F0M]=0.97;
            key(T+tr, p); key(T+d-tr, p); T += d;
        }
        else if (ph->cls=='F') {
            double tr = fminf(0.020/speed, d*0.25);
            double v1,v2,v3; vowel_target(i+1, &v1,&v2,&v3);
            memcpy(p, cur, sizeof p);
            if (ph->c=='s'||ph->c=='f'||ph->c=='x') { p[F1]=v1*0.8; p[F2]=v2; p[F3]=v3; }
            else { p[F1]=F1_; p[F2]=F2_; p[F3]=F3_; }
            p[NZ]=0; p[B1]=60; p[B2]=90; p[B3]=150;
            p[AV]=ph->voiced? 0.35:0; p[AH]=(ph->c=='x')?0.12:0.0; p[AF]=ph->namp*0.5;
            p[FFC]=ph->nfc; p[FBW]=ph->nbw; p[F0M]=0.97;
            if (ph->c=='x') { p[FFC]=fmax(2200, fmin(3200, v2+600)); }
            key(T+tr, p); key(T+d-tr, p); T += d;
        }
        else if (ph->cls=='A') {       /* aproximante/fricativa suave */
            double tr = fminf(0.015/speed, d*0.3);
            double v1,v2,v3; vowel_target(i+1, &v1,&v2,&v3);
            memcpy(p, cur, sizeof p);
            p[F1]=F1_; p[F2]=(ph->c=='G')? (0.5*F2_+0.5*v2) : F2_; p[F3]=F3_;
            p[AV]=APXAV; p[AF]=ph->namp*0.5; p[FFC]=ph->nfc; p[FBW]=ph->nbw; p[NZ]=0; p[F0M]=0.97;
            p[B1]=100; p[B2]=150; p[B3]=200;
            key(T+tr, p); key(T+d-tr, p); T += d;
        }
        else if (ph->cls=='S' || ph->cls=='C') {
            double v1,v2,v3; vowel_target(i+1, &v1,&v2,&v3);
            /* locus dependiente de la vocal siguiente */
            double L1=ph->f1, L2=ph->f2, L3=ph->f3;
            if (ph->c=='k'||ph->c=='g') { L2 = 0.55*v2 + 0.45*1700; L3 = fmin(v3, 2500); if (v2>1700) L2 = 2000; }
            else if (ph->c=='t'||ph->c=='d') { L2 = 0.45*v2 + 0.55*1800; }
            else { L2 = 0.4*v2 + 0.6*900; }
            double clo = (ph->voiced ? 0.050 : 0.060)*CLOS/speed;
            /* preceded by nasal/pause: keep closure */
            memcpy(p, cur, sizeof p);
            p[F1]=L1; p[F2]=L2; p[F3]=L3; p[NZ]=0; p[AF]=0; p[AH]=0; p[B1]=100; p[B2]=150; p[B3]=200;
            p[AV]=ph->voiced? 0.25:0.0; p[F0M]=0.97;
            key(T+0.012/speed, p);             /* las transiciones de entrada a locus */
            /* hold closure (sin pico extra) */
            key(T+clo, p);
            T += clo;
            /* burst */
            double bamp = ph->namp*BURST;
            double fc = ph->nfc;
            if (ph->c=='k'||ph->c=='g') { fc = fmax(1000, fmin(2700, v2)); p[FBW]=700; bamp*=1.6; }
            double bd = (ph->cls=='C') ? 0.090/speed : ((ph->c=='k'||ph->c=='g')?0.014:0.009)/speed;
            p[AF]=bamp; p[FFC]=fc; if (ph->c!='k'&&ph->c!='g') p[FBW]=ph->nbw; p[AV]=ph->voiced? 0.35:0.0;
            if (ph->cls=='C') { p[F1]=300; p[F2]=2000; p[F3]=2800; }
            key(T+0.0005, p);
            p[AF]=bamp*(ph->cls=='C'?0.85:0.25); key(T+bd, p); T += bd;
            /* aspiracion (VOT) */
            if (!ph->voiced) {
                double vot = (ph->c=='p'?0.012: ph->c=='t'?0.018: ph->c=='k'?0.026: 0.022)*VOTS/speed;
                p[AF]=0; p[AH]=ASP; p[AV]=0; p[B1]=150; key(T+vot*0.5, p);
                p[AH]=0.05; p[AV]=0.9; p[B1]=90; p[F1]=fmin(v1, 600); p[F2]=0.5*L2+0.5*v2; p[F3]=v3; key(T+vot, p);
                T += vot;
            } else {
                p[AF]=0; p[AV]=0.7; key(T+0.004, p); T += 0.004;
            }
        }
        memcpy(cur, p, sizeof p);
    }
    memcpy(p, cur, sizeof p); p[AV]=0; p[AF]=0; p[AH]=0; key(T+0.05, p); key(T+0.08, p);
    (void)last_stress_idx;
}

/* ---------- sintetizador ---------- */
typedef struct { double a, b, c, y1, y2; } Res;
static void res_set(Res *r, double f, double bw) {
    if (f < 50) f = 50; if (f > SR*0.45) f = SR*0.45;
    double c = -exp(-2*PI*bw/SR), b = 2*exp(-PI*bw/SR)*cos(2*PI*f/SR);
    r->a = 1 - b - c; r->b = b; r->c = c;
}
static double res_run(Res *r, double x) { double y = r->a*x + r->b*r->y1 + r->c*r->y2; r->y2 = r->y1; r->y1 = y; return y; }
typedef struct { double a, b, c, x1, x2; } Anti;
static void anti_set(Anti *r, double f, double bw) {
    double c = -exp(-2*PI*bw/SR), b = 2*exp(-PI*bw/SR)*cos(2*PI*f/SR), a = 1 - b - c;
    r->a = 1/a; r->b = -b/a; r->c = -c/a;
}
static double anti_run(Anti *r, double x) { double y = r->a*x + r->b*r->x1 + r->c*r->x2; r->x2 = r->x1; r->x1 = x; return y; }

static double gnoise(void) { return frand() + frand() + frand() + frand() - 2.0; }



/* ---------- render: keyframes -> muestras (habla o voz cantada) ---------- */
static double *render_keys(long *pN, double f0base, int sung, double f0s, double maxlen, int vib) {
    double total = keys[nkeys-1].t; if (maxlen > 0 && total > maxlen) total = maxlen;
    long N = (long)(total*SR); double *buf = malloc(sizeof(double)*(N+1));
    Res r[5], rf; Anti az; memset(r,0,sizeof r); memset(&rf,0,sizeof rf); memset(&az,0,sizeof az);
    double phase = 0, gprev = 0, hpf = 0, vlp = 0, nvp = 0; int k = 0;
    double NSC = sqrt(SR/22050.0);             /* compensa el ruido al cambiar la frecuencia de muestreo */
    int question = 0; if (!sung) for (int i = 0; i < nseg; i++) if (seg[i].qend) question = 1;
    for (long n = 0; n < N; n++) {
        double t = n/SR;
        while (k+1 < nkeys-1 && keys[k+1].t <= t) k++;
        Key *a = &keys[k], *b = &keys[k+1]; double u = (b->t > a->t) ? (t - a->t)/(b->t - a->t) : 1; if (u<0) u=0; if (u>1) u=1;
        double p[NP+2]; for (int j = 0; j < NP+2; j++) p[j] = a->p[j] + (b->p[j]-a->p[j])*u;
        if ((n & 15) == 0) {
            for (int j = 0; j < 5; j++) res_set(&r[j], p[F1+j]*FSCALE, p[B1+j]);
            res_set(&rf, p[FFC], p[FBW]); anti_set(&az, p[NZF], 120);
        }
        double f0;
        if (sung) {
            f0 = f0s;
            if (vib && t > 0.15) f0 *= 1.0 + VIBD*sin(2*PI*VIBR*(t-0.15))*fmin(1.0, (t-0.15)/0.25);
        } else {
            double prog = t/total;
            f0 = f0base * (1.0 - F0DECL*prog) * p[F0M];
            if (question) f0 *= 1.0 + QRISE*fmax(0, (prog-0.80)/0.20);
            else f0 *= 1.0 - FALL*fmax(0, (prog-0.88)/0.12);
        }
        f0 *= 1.0 + JITTER*gnoise();
        phase += f0/SR; if (phase >= 1) phase -= 1;
        double Tp = 0.40, Tn = 0.16, g;
        if (phase < Tp) g = 0.5*(1 - cos(PI*phase/Tp));
        else if (phase < Tp+Tn) g = cos(0.5*PI*(phase-Tp)/Tn);
        else g = 0;
        double dg = (g - gprev)*(SR/ (f0>0?f0:100.0)) * 0.05; gprev = g;
        double voice = p[AV]*dg; vlp += TILT*(voice - vlp); voice = vlp;
        double asp = p[AH]*gnoise()*0.15*NSC;
        double nvv = gnoise(); double x = voice + asp + NOISEV*p[AV]*(nvv - nvp)*NSC; nvp = nvv;
        if (p[NZ] > 0.01) { double z = anti_run(&az, x); x = (1-p[NZ])*x + p[NZ]*z*0.8; }
        double y = x;
        for (int j = 0; j < 5; j++) y = res_run(&r[j], y);
        double fr = res_run(&rf, gnoise()) * p[AF] * 0.22 * FRICG * NSC;
        double fd = fr - hpf; hpf = fr; fr = fd*1.0 + 0.2*fr;
        buf[n] = y*0.4 + fr;
    }
    *pN = N; return buf;
}

/* ---------- archivo de voz .ctt ---------- */
typedef struct { const char *nombre, *clave; double *ptr; const char *desc; } Param;
static Param PARAMS[] = {
 {"pitch","F0",&G_F0,"tono base en Hz (hombre ~100-120, mujer ~190-220)"},
 {"velocidad","SPEED",&G_SPEED,"velocidad del habla (1.0 normal)"},
 {"volumen","VOL",&VOL,"volumen de salida 0..1"},
 {"formantes","FSCALE",&FSCALE,"escala de formantes (1.0 neutral, mayor = mas agudo/chico)"},
 {"declinacion","F0DECL",&F0DECL,"cuanto baja el tono a lo largo de la frase (0..0.3)"},
 {"acento_tono","ACCF",&ACCF,"subida de tono en la silaba acentuada"},
 {"atona_tono","UNSF",&UNSF,"tono relativo de silabas sin acento"},
 {"pregunta_subida","QRISE",&QRISE,"subida de tono al final de una pregunta"},
 {"final_bajada","FALL",&FALL,"bajada de tono al final de una afirmacion"},
 {"jitter","JITTER",&JITTER,"variacion aleatoria del tono (voz menos robotica)"},
 {"acento_duracion","STRESSD",&STRESSD,"alargamiento de la vocal acentuada"},
 {"vocal_duracion","VDUR",&VDUR,"multiplicador de duracion de vocales"},
 {"consonante_duracion","CDUR",&CDUR,"multiplicador de duracion de consonantes"},
 {"semiconsonante_duracion","GLD",&GLD,"duracion de i/u semiconsonantes (ie, ua...)"},
 {"semiconsonante_volumen","GLAV",&GLAV,"volumen de semiconsonantes"},
 {"transicion","T_TR",&T_TR,"segundos de transicion entre sonidos"},
 {"pausa_coma","PCOMA",&PCOMA,"segundos de pausa en coma"},
 {"pausa_punto","PPUNTO",&PPUNTO,"segundos de pausa en punto"},
 {"cola","TAIL",&TAIL,"segundos de silencio al final"},
 {"inicio","LEAD",&LEAD,"segundos de silencio al inicio"},
 {"rafaga","BURST",&BURST,"fuerza de la rafaga de p t k b d g"},
 {"aspiracion","ASP",&ASP,"aspiracion despues de p t k"},
 {"vot","VOTS",&VOTS,"duracion de la aspiracion (VOT)"},
 {"cierre","CLOS",&CLOS,"duracion del cierre de oclusivas"},
 {"friccion","FRICG",&FRICG,"volumen de s f j (fricativas)"},
 {"respiracion","NOISEV",&NOISEV,"soplo durante las vocales"},
 {"pendiente","TILT",&TILT,"filtro pasa-bajos de la voz (1.0 = sin filtro, menor = mas oscuro)"},
 {"nasal_vocal","NVN",&NVN,"nasalizacion de vocales junto a m/n"},
 {"nasal_volumen","NASAV",&NASAV,"volumen de m n ni"},
 {"nasal_f1","NF1",&NF1,"primer formante de las nasales (Hz)"},
 {"nasal_ancho","NASB1",&NASB1,"ancho de banda de F1 nasal"},
 {"nasal_b2","NB2",&NB2,"ancho de banda de F2 nasal"},
 {"nasal_b3","NB3",&NB3,"ancho de banda de F3 nasal"},
 {"nasal_cero","NZA",&NZA,"cuanto actua el antiresonador nasal 0..1"},
 {"nasal_cero_ganancia","NZG",&NZG,"ganancia del antiresonador nasal"},
 {"nasal_f2","NF2S",&NF2S,"escala del F2 de las nasales"},
 {"lateral_f1","LF1",&LF1,"escala del F1 de la l"},
 {"lateral_f2","LF2S",&LF2S,"escala del F2 de la l"},
 {"lateral_volumen","LAV",&LAV,"volumen de la l"},
 {"lateral_ancho","LB1",&LB1,"ancho de banda de F1 en la l"},
 {"aprox_volumen","APXAV",&APXAV,"volumen de b d g suaves entre vocales"},
 {"tap_volumen","TAPAV",&TAPAV,"volumen durante la r simple (0 = cierre total)"},
 {"tap_duracion","TAPD",&TAPD,"duracion de la r simple"},
 {"trino_duracion","TRD",&TRD,"duracion de la rr"},
 {"trino_golpes","TRN",&TRN,"numero de golpes de la rr"},
 {"trino_profundidad","TRF",&TRF,"volumen entre golpes de la rr (0 = corte total)"},
 {"muestreo","SRATE",&SRATE,"frecuencia de muestreo en Hz (0 = automatico: 22050 habla, 44100 canciones)"},
 {"base","BASE",&RBASE,"rbsong: duracion base de una unidad $1, en segundos"},
 {"voz_volumen","VOZG",&VOZG,"rbsong: ganancia de la voz CTTS (calibrada contra la de GemTunes)"},
 {"sin_instrumentos","SININT",&SININT,"rbsong: 1 = solo voz (omite los canales sin notas voz_; igual que --no-int)"},
 {"palatalizacion","IPAP",&IPAP,"IPA: consonante suave (ʲ) antes de vocal suma un deslizamiento j (ruso: бю, ня...)"},
 {"ruso_y","IPAY",&IPAY,"IPA ruso: la ы (espeak la escribe y) se canta como vocal central, no como i"},
 {"voz_motor","VOZM",&VOZM,"rbsong: voz cantada 0 = CTTS (mas inteligible), 1 = GemTunes (identica al motor Ruby)"},
 {"vibrato_prof","VIBD",&VIBD,"rbsong: profundidad del vibrato de la voz (0 = sin vibrato)"},
 {"vibrato_vel","VIBR",&VIBR,"rbsong: velocidad del vibrato en Hz"},
 {"h_volumen","HAMP",&HAMP,"volumen de la h aspirada (ingles, etc.)"},
 {NULL,NULL,NULL,NULL}
};
/* Timbre por canal:  canal.formantes = X  (en un .ctt o como directiva  #! canal.formantes = X  dentro de la cancion).
 * Un .ctt manda sobre la directiva de la cancion (lock = 1). */
static struct { char name[64]; double fs; int lock; } TFS[32]; static int ntfs;
static double track_fs(const char *name) { for (int i = 0; i < ntfs; i++) if (!strcasecmp(TFS[i].name, name)) return TFS[i].fs; return 1.0; }
static void set_track_fs(const char *name, double v, int lock) {
    int i = 0; for (; i < ntfs; i++) if (!strcasecmp(TFS[i].name, name)) break;
    if (i == ntfs) { if (ntfs >= 32) return; snprintf(TFS[ntfs].name, 64, "%s", name); TFS[ntfs].lock = 0; ntfs++; }
    if (lock || !TFS[i].lock) { TFS[i].fs = v; if (lock) TFS[i].lock = 1; }
}
static void lower(char *s) { for (; *s; s++) *s = tolower((unsigned char)*s); }
static char *trim(char *s) { while (*s == ' ' || *s == '\t') s++; char *e = s + strlen(s); while (e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\n'||e[-1]=='\r')) *--e = 0; return s; }
static void set_cfg(const char *clave, double v) {
    for (int i = 0; i < ncfg; i++) if (!strcmp(cfg[i].k, clave)) { cfg[i].v = v; return; }
    if (ncfg < 256) { snprintf(cfg[ncfg].k, 32, "%s", clave); cfg[ncfg].v = v; ncfg++; }
}
static void load_ctt(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) { perror(path); exit(1); }
    char line[512]; int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++; char *h = strchr(line, '#'); if (h) *h = 0;
        char *p = trim(line); if (!*p) continue;
        char *eq = strchr(p, '='); if (!eq) eq = strchr(p, ':');
        if (!eq) { fprintf(stderr, "%s:%d: falta '=' \n", path, ln); continue; }
        *eq = 0; char *k = trim(p), *v = trim(eq+1);
        char korig[64]; snprintf(korig, sizeof korig, "%s", k); lower(k);          /* el nombre del fonema distingue mayusculas (N, I, E...); el resto no */
        for (char *c = v; *c; c++) if (*c == ',') *c = '.';
        { char *dt = strchr(k, '.');
          if (dt && strncmp(k, "fonema.", 7) != 0) {                /* canal.formantes = X */
              *dt = 0; const char *prop = dt + 1;
              if (!strcmp(prop, "formantes") || !strcmp(prop, "fscale")) set_track_fs(k, atof(v), 1);
              else fprintf(stderr, "%s:%d: propiedad de canal desconocida '%s.%s'\n", path, ln, k, prop);
              continue; } }
        if (!strncmp(k, "fonema.", 7) && k[7] && !k[8]) {          /* fonema.a = F1 F2 F3 [duracion] */
            Ph *ph = phf(korig[7]); double a,b,c2,d;
            int nn = sscanf(v, "%lf %lf %lf %lf", &a, &b, &c2, &d);
            if (!ph || nn < 3) { fprintf(stderr, "%s:%d: fonema invalido\n", path, ln); continue; }
            ph->f1 = a; ph->f2 = b; ph->f3 = c2; if (nn >= 4) ph->dur = d;
            continue;
        }
        int found = 0;
        for (Param *q = PARAMS; q->nombre; q++) {
            char ck[32]; snprintf(ck, sizeof ck, "%s", q->clave); lower(ck);
            if (!strcmp(k, q->nombre) || !strcmp(k, ck)) { set_cfg(q->clave, atof(v)); found = 1; break; }
        }
        if (!found) fprintf(stderr, "%s:%d: parametro desconocido '%s'\n", path, ln, k);
    }
    fclose(f);
}
static void dump_ctt(void) {
    printf("# CTTS - archivo de voz (.ctt)\n# Formato:  nombre = valor     (# comenta, coma o punto decimal)\n# Uso:      ./ctts -c voz.ctt \"texto\" salida.wav\n# Los argumentos velocidad/f0 de la linea de comandos pisan estos valores.\n# Dentro de un .rbsong se puede poner lo mismo como comentario:  #! voz_volumen = 1.4   (GemTunes lo ignora)\n# Timbre por canal (voz CTTS): en un .ctt  agente.formantes = 0.85  (o como directiva  #! agente.formantes = 0.85  en la cancion; el .ctt manda)\n#   menor = voz mas grave/grande; mayor = mas aguda/chica\n\n");
    for (Param *q = PARAMS; q->nombre; q++) printf("%-24s = %-8g # %s\n", q->nombre, *q->ptr, q->desc);
    printf("\n# Formantes por fonema (F1 F2 F3 [duracion en s]); quita el # para cambiar uno:\n");
    for (Ph *p = PH; p->c; p++) if (p->cls=='V'||p->cls=='G'||p->cls=='N'||p->cls=='L')
        printf("# fonema.%c = %.0f %.0f %.0f %.3f\n", p->c, p->f1, p->f2, p->f3, p->dur);
}


/* ======================================================================
 *  Motor de sonido .rbsong  (compatible con GemTunes / synth_engine.rb)
 *  osciladores + DrumKit + mixer identicos a la referencia en Ruby.
 *  La voz cantada puede ser la de CTTS (mas inteligible) o la de GemTunes.
 * ====================================================================== */
static int ipa_fetch(const char *lang, const char *text, char *out, size_t cap);
static int g_noint = 0;                                   /* --no-int dado en la linea de comandos */
static const char *STEMS = NULL;                        /* --stems DIR: exporta cada canal por separado */
typedef struct { int track; long start, end; char lang[12]; char text[1100]; } Cap;
static Cap caps[8192]; static int ncaps;
static double VOZM;      /* 0 = voz CTTS, 1 = voz GemTunes (identica al motor Ruby) */

/* ---- utilidades de la referencia ---- */
static double lfsr_step(unsigned *st, int tap) {       /* SynthEngine.nes_lfsr_step */
    unsigned fb = (*st & 1) ^ ((*st >> tap) & 1); *st = (*st >> 1) | (fb << 14);
    return (*st & 1) ? -1.0 : 1.0;
}
typedef struct { double a, b, c, y1, y2; } GRes;       /* Voz::Resonator (ganancia unitaria en continua) */
static void gres_init(GRes *r, double f, double bw) {
    double rr = exp(-PI*bw/SR), th = 2.0*PI*f/SR; r->b = 2.0*rr*cos(th); r->c = -rr*rr; r->a = 1.0 - r->b - r->c; r->y1 = r->y2 = 0;
}
static double gres_step(GRes *r, double x) { double y = r->a*x + r->b*r->y1 + r->c*r->y2; r->y2 = r->y1; r->y1 = y; return y; }
static double kb_sum(const double *v, int n) {          /* Array#sum de Ruby (Kahan-Babuska) */
    double f = 0.0, c = 0.0;
    for (int i = 0; i < n; i++) { double x = v[i], t = f + x; if (fabs(f) >= fabs(x)) c += ((f - t) + x); else c += ((x - t) + f); f = t; }
    return f + c;
}
static double clamp1(double s) { return s < -1.0 ? -1.0 : (s > 1.0 ? 1.0 : s); }
static long rround(double x) { return (long)(x < 0 ? -floor(-x + 0.5) : floor(x + 0.5)); }   /* Float#round */

/* ---- osciladores (ingenuos, como la referencia) ---- */
typedef enum { W_SQ, W_TRI, W_SAW, W_SIN, W_HEX, W_NOISE, W_NOISES } Wave;
typedef struct { Wave w; unsigned ns; } Osc;
static double osc_sample(Osc *o, double phase) {
    switch (o->w) {
        case W_SQ:  return phase < 0.5 ? 1.0 : -1.0;
        case W_TRI: return 4.0 * (phase < 0.5 ? phase : 1.0 - phase) - 1.0;
        case W_SAW: return 2.0*phase - 1.0;
        case W_SIN: return sin(2.0*PI*phase);
        case W_NOISE:  return lfsr_step(&o->ns, 1);
        case W_NOISES: return lfsr_step(&o->ns, 6);
        case W_HEX: {                         /* SynthEngine.hexagon_xy: x del perimetro de un hexagono */
            double theta = phase*2.0*PI, seg = (2.0*PI)/6, rel = fmod(theta, seg), half = seg/2.0;
            double apothem = cos(half), r = apothem/cos(rel - half); return r*cos(theta);
        }
    }
    return 0;
}
/* Envelope.new(attack: 0.005, decay: 0.03, sustain: 0.8, release: 0.03).amplitude_at */
static double env_at(double t, double dur) {
    const double A = 0.005, D = 0.03, S = 0.8, R = 0.03; double on = dur - R; if (on < 0) on = 0;
    if (t < A) return A == 0 ? 1.0 : t/A;
    else if (t < A + D) return 1.0 - (1.0 - S)*((t - A)/D);
    else if (t < on) return S;
    else { double rt = t - on; if (rt >= R) return 0.0; return S*(1.0 - rt/R); }
}
static double *render_osc(Wave w, double freq, double dur, double vol, long *pn) {      /* Voice#render_note */
    long n = rround(dur*SR); double *b = calloc(n + 1, sizeof(double)); Osc o = {w, 1}; double phase = 0, inc = freq/SR;
    for (long i = 0; i < n; i++) { double t = i/SR; b[i] = osc_sample(&o, phase)*env_at(t, dur)*vol; phase += inc; if (phase >= 1.0) phase -= 1.0; }
    *pn = n; return b;
}

/* ---- DrumKit (port exacto) ---- */
static double *drum(const char *nm, double freq, double vol, double dur, long *pn) {
    long n = rround(dur*SR); if (n < 1) n = 1; double *b = calloc(n + 1, sizeof(double)); *pn = n;
    if (!strcmp(nm,"kick")) {
        if (freq <= 0) freq = 60.0; double sf = freq*3.0, ef = freq*0.55, pt = 0.045, at = fmax(dur/3.0, 0.12), ct = 0.004; double ph = 0; unsigned ns = 1;
        for (long i = 0; i < n; i++) { double t = i/SR, f = ef + (sf - ef)*exp(-t/pt); ph += f/SR; if (ph >= 1.0) ph -= 1.0;
            double tone = sin(2.0*PI*ph), ae = exp(-t/at), ce = exp(-t/ct), nz = lfsr_step(&ns, 1); b[i] = clamp1((tone*ae + nz*ce*0.35)*vol); }
    } else if (!strcmp(nm,"snare")) {
        if (freq <= 0) freq = 190.0; double tt = 0.07, nt = fmax(dur/2.5, 0.09); double ph = 0; unsigned ns = 1;
        for (long i = 0; i < n; i++) { double t = i/SR; ph += freq/SR; if (ph >= 1.0) ph -= 1.0;
            double tone = sin(2.0*PI*ph)*exp(-t/tt), nz = lfsr_step(&ns, 1)*exp(-t/nt); b[i] = clamp1((tone*0.4 + nz*0.85)*vol); }
    } else if (!strcmp(nm,"hihat")) {
        double tau = fmin(dur*0.35, 0.15); unsigned sa = 1, sb = 1;
        for (long i = 0; i < n; i++) { double t = i/SR, xa = lfsr_step(&sa, 1), xb = lfsr_step(&sb, 6), env = exp(-t/tau); b[i] = clamp1((xa + xb)*0.5*env*vol); }
    } else if (!strcmp(nm,"tom")) {
        if (freq <= 0) freq = 130.0; double sf = freq*1.8, ef = freq*0.85, pt = 0.06, at = fmax(dur/2.5, 0.18); double ph = 0;
        for (long i = 0; i < n; i++) { double t = i/SR, f = ef + (sf - ef)*exp(-t/pt); ph += f/SR; if (ph >= 1.0) ph -= 1.0; b[i] = clamp1(sin(2.0*PI*ph)*exp(-t/at)*vol); }
    } else if (!strcmp(nm,"crash")) {
        static const double R[6] = {1.0, 1.44, 1.86, 2.29, 2.66, 3.32}; if (freq <= 0) freq = 300.0; double tau = fmin(dur*0.6, 1.8); GRes rs[6]; unsigned sa = 1, sb = 1;
        for (int k = 0; k < 6; k++) gres_init(&rs[k], freq*R[k], 400.0);
        for (long i = 0; i < n; i++) { double t = i/SR, xa = lfsr_step(&sa, 1), xb = lfsr_step(&sb, 6), nz = (xa + xb)*0.5, pv[6];
            for (int k = 0; k < 6; k++) pv[k] = gres_step(&rs[k], nz); double ring = kb_sum(pv, 6)/6.0, env = exp(-t/tau); b[i] = clamp1((nz*0.45 + ring*1.6)*env*vol); }
    } else if (!strcmp(nm,"ride")) {
        static const double R[4] = {1.4, 2.6, 3.3, 4.8}; if (freq <= 0) freq = 400.0; double tau = fmin(dur*0.5, 1.0), bt = tau*1.3; GRes bell, rs[4]; unsigned st = 1;
        gres_init(&bell, freq*2.0, 40.0); for (int k = 0; k < 4; k++) gres_init(&rs[k], freq*R[k], 300.0);
        for (long i = 0; i < n; i++) { double t = i/SR, nz = lfsr_step(&st, 1), by = gres_step(&bell, nz), pv[4];
            for (int k = 0; k < 4; k++) pv[k] = gres_step(&rs[k], nz); double ring = kb_sum(pv, 4)/4.0, env = exp(-t/tau), be = exp(-t/bt); b[i] = clamp1((by*0.9*be + ring*0.55*env + nz*0.12*env)*vol); }
    } else if (!strcmp(nm,"clap")) {
        static const double BS[4] = {0.0, 0.012, 0.024, 0.038}; GRes rs; unsigned st = 1; gres_init(&rs, 1200.0, 800.0); double ts = 0.045;
        for (long i = 0; i < n; i++) { double t = i/SR, nz = lfsr_step(&st, 1), y = gres_step(&rs, nz), env = 0.0;
            for (int k = 0; k < 4; k++) { double dt = t - BS[k]; if (dt < 0) continue; env = fmax(env, exp(-dt/0.008)); }
            if (t >= ts) { double dt = t - ts; env = fmax(env, 0.6*exp(-dt/0.15)); }
            b[i] = clamp1(y*env*vol*0.8); }
    }
    return b;
}

/* ---- voz de GemTunes (port exacto de Voz, para reproducir el motor Ruby) ---- */
static const struct { const char *ipa; char ph; } GIPA[] = {
 {"a",'a'},{"e",'e'},{"i",'i'},{"o",'o'},{"u",'u'},{"ɪ",'i'},{"ɛ",'e'},{"æ",'a'},{"ʌ",'a'},{"ɔ",'o'},{"ʊ",'u'},{"ə",'e'},{"ɜ",'e'},{"ɐ",'a'},{"ɑ",'a'},{"ɒ",'o'},
 {"ä",'a'},{"ɯ",'u'},{"ɨ",'i'},{"ʉ",'u'},{"ʏ",'i'},{"y",'i'},{"ø",'e'},{"œ",'e'},{"ɵ",'o'},{"ɤ",'o'},{"ɘ",'e'},{"ɶ",'a'},{"ɚ",'e'},{"ɝ",'e'},
 {"m",'m'},{"n",'n'},{"ɲ",'n'},{"ŋ",'n'},{"ɱ",'m'},{"ɳ",'n'},{"ɴ",'n'},
 {"l",'l'},{"ʎ",'y'},{"ɭ",'l'},{"ɫ",'l'},{"ɾ",'r'},{"r",'r'},{"ɹ",'r'},{"ʀ",'r'},{"ʁ",'r'},{"ɽ",'r'},{"ʙ",'r'},
 {"j",'y'},{"w",'w'},{"ɥ",'w'},{"ʝ",'y'},
 {"k",'k'},{"t",'t'},{"p",'p'},{"ʈ",'t'},{"c",'k'},{"q",'k'},{"ʔ",'t'},
 {"b",'b'},{"d",'d'},{"g",'g'},{"ɡ",'g'},{"ɖ",'d'},{"ɟ",'g'},{"ɢ",'g'},{"ɓ",'b'},{"ɗ",'d'},{"ʄ",'g'},{"ɠ",'g'},
 {"s",'s'},{"z",'s'},{"ʃ",'s'},{"ʒ",'s'},{"ʂ",'s'},{"ʐ",'s'},{"ɕ",'s'},{"ʑ",'s'},
 {"f",'f'},{"v",'b'},{"β",'b'},{"ɸ",'f'},{"θ",'s'},{"ð",'d'},{"x",'x'},{"ɣ",'g'},{"χ",'x'},{"ç",'h'},
 {"h",'h'},{"ɦ",'h'},{"ħ",'h'},{"ʕ",'h'},{"ʦ",'t'},{"ʣ",'d'},{"ʧ",'t'},{"ʤ",'d'},{"ʨ",'t'},{"ʥ",'d'},
 {NULL,0}
};
static int gcat(char ph) {          /* 0 vocal, 1 sonante, 2 fricativa, 3 oclusiva */
    if (strchr("aeiou", ph)) return 0; if (strchr("mnlryw", ph)) return 1; if (strchr("sfxh", ph)) return 2; if (strchr("ktpgdb", ph)) return 3; return 0;
}
static void gform(char ph, double *f1, double *f2, double *f3) {
    switch (ph) {
        case 'a': *f1=730; *f2=1090; *f3=2440; break; case 'e': *f1=530; *f2=1840; *f3=2480; break; case 'i': *f1=270; *f2=2290; *f3=3010; break;
        case 'o': *f1=570; *f2= 840; *f3=2410; break; case 'u': *f1=300; *f2= 870; *f3=2240; break; case 'm': *f1=250; *f2= 900; *f3=2600; break;
        case 'n': *f1=250; *f2=1500; *f3=2600; break; case 'l': *f1=360; *f2=1800; *f3=2600; break; case 'r': *f1=400; *f2=1500; *f3=2600; break;
        case 'y': *f1=300; *f2=2200; *f3=2600; break; case 'w': *f1=320; *f2= 750; *f3=2600; break; default: *f1=500; *f2=1500; *f3=2500;
    }
}
static double glottal(double ph) {
    if (ph < 0.40) { double t = ph/0.40; return 0.5*(1.0 - cos(PI*t)); }
    else if (ph < 0.56) { double t = (ph - 0.40)/0.16; return cos(PI*t/2.0); }
    return 0.0;
}
static long g_voiced(double f1, double f2, double f3, double freq, double vol, long n, int first, int last, double totd, double *out) {
    GRes r1, r2, r3; gres_init(&r1, f1, 70.0); gres_init(&r2, f2, 110.0); gres_init(&r3, f3, 180.0);
    double attack = first ? 0.02 : 0.005, release = last ? fmin(totd*0.12, 0.05) : 0.005, sd = n/SR;
    double phase = 0, jc = 1.0; unsigned js = 1; double vramp = fmax(SR*0.2, 1.0);
    for (long i = 0; i < n; i++) {
        double t = i/SR, vd = 0.011*fmin(i/vramp, 1.0), vib = 1.0 + vd*sin(2.0*PI*5.5*t), fi = freq*vib*jc; if (fi < 40.0) fi = 40.0;
        phase += fi/SR; if (phase >= 1.0) { phase -= 1.0; double jb = lfsr_step(&js, 1); jc = 1.0 + jb*0.006; }
        double src = glottal(phase), y = gres_step(&r3, gres_step(&r2, gres_step(&r1, src)));
        double env = t < attack ? t/attack : (t > sd - release ? fmax((sd - t)/release, 0.0) : 1.0);
        out[i] = tanh(y*env*vol*0.45);
    }
    return n;
}
static long g_fric(double cf, double bw, double vol, long n, double *out) {
    GRes rs; gres_init(&rs, cf, bw); unsigned st = 1; long an = (long)(n*0.08); if (an < 1) an = 1; long rn = (long)(n*0.25); if (rn < 1) rn = 1;
    for (long i = 0; i < n; i++) {
        double nz = lfsr_step(&st, 1), y = gres_step(&rs, nz), env = i < an ? (double)i/an : (i > n - rn ? fmax((double)(n - i)/rn, 0.0) : 1.0);
        out[i] = tanh(y*env*vol*0.8*0.45);
    }
    return n;
}
static double *g_voz(const char *lang, const char *text, double freq, double vol, double dur, long *pn) {
    static int warned = 0; char ph[512]; int np = 0; char ipa[4096]; int ok = ipa_fetch(lang, text, ipa, sizeof ipa);
    if (ok) {                                    /* Voz.map_ipa_to_phonemes */
        for (const char *s = ipa; *s && np < 500; ) {
            unsigned char c = *s; size_t L = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2);
            int skip = 0;
            if (L == 1) skip = (strchr(" .|0123456789\n\r\t", c) != NULL) || c == 0;
            else if (!strncmp(s,"ˈ",2)||!strncmp(s,"ˌ",2)||!strncmp(s,"‿",3)||!strncmp(s,"‖",3)||!strncmp(s,"ː",2)||!strncmp(s,"ˑ",2)||!strncmp(s,"ʰ",2)||!strncmp(s,"ʲ",2)||!strncmp(s,"ʷ",2)||!strncmp(s,"ˠ",2)||!strncmp(s,"ˤ",2)||!strncmp(s,"˞",2)||!strncmp(s,"ʼ",2)) skip = 1;
            else if (L == 2 && c == 0xCC) skip = 1;                          /* U+0300..U+033F */
            else if (L == 2 && c == 0xCD && (unsigned char)s[1] <= 0xAF) skip = 1;   /* U+0340..U+036F */
            if (!skip) for (int i = 0; GIPA[i].ipa; i++) if (strlen(GIPA[i].ipa) == L && !strncmp(s, GIPA[i].ipa, L)) { ph[np++] = GIPA[i].ph; break; }
            s += L;
        }
    } else {                                      /* fallback_phonemize (espanol) */
        if (!warned) { fprintf(stderr, "aviso: espeak-ng no disponible; fonemas por reglas del espanol\n"); warned = 1; }
        for (const char *s = text; *s && np < 500; ) {
            unsigned char c = *s; char lo = tolower(c);
            if (c >= 0x80) { unsigned char d = s[1];
                if (c == 0xC3) { const char *m = NULL; switch (d) { case 0xA1: case 0x81: m = "a"; break; case 0xA9: case 0x89: m = "e"; break; case 0xAD: case 0x8D: m = "i"; break; case 0xB3: case 0x93: m = "o"; break; case 0xBA: case 0x9A: case 0xBC: case 0x9C: m = "u"; break; case 0xB1: case 0x91: m = "n"; break; }
                    if (m) ph[np++] = m[0]; } s += (c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2); continue; }
            char two[3] = {lo, (char)tolower((unsigned char)s[1]), 0};
            if (!strcmp(two,"ch")) { ph[np++]='t'; ph[np++]='s'; s += 2; continue; } if (!strcmp(two,"ll")) { ph[np++]='y'; s += 2; continue; }
            if (!strcmp(two,"rr")) { ph[np++]='r'; s += 2; continue; } if (!strcmp(two,"gu")) { ph[np++]='g'; s += 2; continue; } if (!strcmp(two,"qu")) { ph[np++]='k'; s += 2; continue; }
            switch (lo) { case 'a': case 'e': case 'i': case 'o': case 'u': case 'm': case 'n': case 'l': case 'r': case 'k': case 't': case 'p': case 'b': case 'd': case 'g': case 's': case 'f': case 'w': case 'x': ph[np++] = lo; break;
                case 'j': ph[np++]='x'; break; case 'c': case 'q': ph[np++]='k'; break; case 'v': ph[np++]='b'; break; case 'z': ph[np++]='s'; break; case 'y': ph[np++]='i'; break; default: break; }
            s++;
        }
    }
    if (np == 0) ph[np++] = 'a';
    long total = rround(dur*SR); if (total < 1) total = 1; double wt[512], tw = 0; const double WV[4] = {1.5, 1.0, 0.6, 0.25};
    for (int i = 0; i < np; i++) wt[i] = WV[gcat(ph[i])];
    tw = kb_sum(wt, np); if (tw == 0) tw = 1.0;                /* weights.sum de Ruby usa suma compensada (Kahan-Babuska) */
    long sn[512], ssum = 0; for (int i = 0; i < np; i++) { sn[i] = (long)(total*wt[i]/tw); if (sn[i] < 1) sn[i] = 1; ssum += sn[i]; }
    sn[np-1] += total - ssum; if (sn[np-1] < 1) sn[np-1] = 1;
    long all = 0; for (int i = 0; i < np; i++) all += sn[i]; double *buf = calloc(all + 8, sizeof(double)); long pos = 0;
    for (int i = 0; i < np; i++) {
        long n = sn[i]; int c = gcat(ph[i]);
        if (c == 0 || c == 1) { double f1, f2, f3; gform(ph[i], &f1, &f2, &f3); g_voiced(f1, f2, f3, freq, vol, n, i == 0, i == np-1, dur, buf + pos); }
        else if (c == 2) { double cf = ph[i]=='s' ? 4800 : ph[i]=='f' ? 1200 : 1500, bw = ph[i]=='s' ? 1800 : ph[i]=='f' ? 1600 : 1200; g_fric(cf, bw, vol, n, buf + pos); }
        else {
            long cl = (long)(n*0.35); if (cl < 1) cl = 1; long bn = n - cl; if (bn < 1) bn = 1; double *bb = buf + pos + cl;
            if (ph[i]=='k' || ph[i]=='t' || ph[i]=='p') { double cf = ph[i]=='k' ? 1900 : ph[i]=='t' ? 3800 : 900, bw = ph[i]=='k' ? 1200 : ph[i]=='t' ? 1500 : 800; g_fric(cf, bw, vol, bn, bb); }
            else { double f1 = ph[i]=='g' ? 300 : ph[i]=='d' ? 380 : 250, f2 = ph[i]=='g' ? 1600 : ph[i]=='d' ? 1900 : 800; g_voiced(f1, f2, 2600.0, freq, vol, bn, 1, 1, bn/SR, bb); }
            n = cl + bn;
        }
        pos += n;
    }
    *pn = pos; return buf;
}

/* ---- IPA (via espeak-ng) -> fonemas internos de CTTS ---- */
static const struct { const char *ipa, *out; } IPAM[] = {
  {"tʃ","C"},{"dʒ","Z"},{"tɕ","C"},{"dʑ","Z"},
  {"eɪ","ej"},{"aɪ","aj"},{"aʊ","aw"},{"ɔɪ","Oj"},{"əʊ","ow"},{"oʊ","ow"},{"ɪə","I~@"},{"eə","E~@"},{"ɛə","E~@"},{"ʊə","U~@"},
  {"ɑː","P"},{"ɜː","3"},{"ɔː","O"},{"iː","i"},{"uː","u"},
  {"a","a"},{"ɑ","P"},{"ɒ","Q"},{"æ","A"},{"ɐ","V"},{"ʌ","V"},{"e","e"},{"ɛ","E"},{"ə","@"},{"ɜ","3"},{"ɚ","3"},
  {"i","i"},{"ɪ","I"},{"ɨ","1"},{"y","i"},{"ʏ","I"},{"o","o"},{"ɔ","O"},{"ø","e"},{"œ","E"},
  {"u","u"},{"ʊ","U"},{"ʉ","u"},{"ɯ","u"},{"ɤ","o"},
  {"p","p"},{"b","b"},{"t","t"},{"d","d"},{"k","k"},{"g","g"},{"ɡ","g"},{"c","k"},{"ɟ","g"},{"q","k"},
  {"m","m"},{"ɱ","m"},{"n","n"},{"ɲ","N"},{"ŋ","M"},{"ɳ","n"},{"ɴ","n"},
  {"f","f"},{"v","v"},{"θ","T"},{"ð","D"},{"s","s"},{"z","z"},{"ʃ","S"},{"ʒ","Y"},{"ç","x"},{"x","x"},{"ɣ","G"},{"χ","x"},
  {"h","H"},{"ɦ","H"},{"ɸ","f"},{"β","B"},{"ʝ","J"},{"ɕ","S"},{"ʑ","Y"},
  {"l","l"},{"ɫ","l"},{"ɭ","l"},{"ʎ","J"},{"ɾ","r"},{"r","R"},{"ɹ","L"},{"ɻ","L"},{"ʀ","R"},{"ʁ","R"},{"ɽ","r"},
  {"w","w"},{"j","j"},{"ɥ","j"},{"ʋ","v"},
  {NULL,NULL}
};
static char IPA_LANG[16] = "";
static int ipa_next_vowel(const char *s) {          /* ¿el siguiente simbolo IPA (saltando el acento) es una vocal? */
    while (!strncmp(s, "ˈ", 2) || !strncmp(s, "ˌ", 2)) s += 2;
    int best = -1; size_t bl = 0;
    for (int i = 0; IPAM[i].ipa; i++) { size_t L = strlen(IPAM[i].ipa); if (L > bl && !strncmp(s, IPAM[i].ipa, L)) { best = i; bl = L; } }
    if (best < 0) return 0; const char *o = IPAM[best].out; while (*o == '~') o++;
    Ph *p = phf(*o); return p && p->cls == 'V';
}
static int ipa_next_vowel(const char *s);
static void ipa_to_seg(const char *s) {
    int pend = 0, depth = 0; nseg = 0;
    while (*s) {
        unsigned char c = *s;
        if (c == '(') { depth++; s++; continue; }
        if (c == ')') { if (depth) depth--; s++; continue; }
        if (depth) { s++; continue; }
        if (!strncmp(s, "ˈ", 2)) { pend = 1; s += 2; continue; }
        if (!strncmp(s, "ʲ", 2)) {                      /* consonante suave: antes de vocal suma un deslizamiento j */
            if (IPAP >= 0.5 && nseg && ipa_next_vowel(s + 2)) { Ph *pp = phf(seg[nseg-1].c); if (pp && pp->cls != 'V' && pp->cls != 'G') put('j', 0, 0); }
            s += 2; continue;
        }
        int best = -1; size_t bl = 0;
        for (int i = 0; IPAM[i].ipa; i++) { size_t L = strlen(IPAM[i].ipa); if (L > bl && !strncmp(s, IPAM[i].ipa, L)) { best = i; bl = L; } }
        if (best >= 0) {
            const char *o = IPAM[best].out; int off = 0;
            if (IPAY >= 0.5 && !strncmp(IPA_LANG, "ru", 2) && !strcmp(IPAM[best].ipa, "y")) o = "1";
            for (; *o; o++) {
                if (*o == '~') { off = 1; continue; }
                Ph *ph = phf(*o); int st = (pend && ph && ph->cls == 'V' && !off) ? 1 : 0; if (st) pend = 0;
                put(*o, st, 0); if (off) { seg[nseg-1].off = 1; off = 0; }
            }
            s += bl; continue;
        }
        s += (c < 0x80) ? 1 : (c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2);      /* simbolo desconocido / diacritico: se ignora */
    }
    if (nseg) seg[nseg-1].wend = 1;
}
static int ipa_fetch(const char *lang, const char *text, char *out, size_t cap) {
    char l[16]; int k = 0; for (const char *c = lang; *c && k < 14; c++) if (isalnum((unsigned char)*c) || *c == '-') l[k++] = *c; l[k] = 0;
    char esc[1100]; int e = 0; for (const char *c = text; *c && e < 1080; c++) { if (*c == '\'') { esc[e++]='\''; esc[e++]='\\'; esc[e++]='\''; esc[e++]='\''; } else esc[e++] = *c; } esc[e] = 0;
    char cmd[1400]; snprintf(cmd, sizeof cmd, "espeak-ng -q --ipa -v '%s' -- '%s' 2>/dev/null", l, esc);
    FILE *p = popen(cmd, "r"); if (!p) return 0;
    size_t L = fread(out, 1, cap - 1, p); out[L] = 0; int rc = pclose(p);
    return L > 0 && rc == 0;
}

/* ---- voz cantada: voz_<idioma>_<texto> ---- */
static double *rb_sing(const char *lang, const char *text, double hz, double dur, long *pn) {
    static int warned = 0;
    nseg = 0; int es = !strncmp(lang, "es", 2);
    if (!es) {
        static char ipa[4096];
        snprintf(IPA_LANG, sizeof IPA_LANG, "%s", lang);
        if (ipa_fetch(lang, text, ipa, sizeof ipa)) ipa_to_seg(ipa);
        else { if (!warned) { fprintf(stderr, "aviso: espeak-ng no disponible para '%s'; uso reglas del espanol\n", lang); warned = 1; } es = 1; }
    }
    if (es) parse(text);
    while (nseg && seg[nseg-1].c == ' ') nseg--;
    if (nseg == 0) return NULL;
    double target = dur - 0.015; if (target < 0.06) target = 0.06;
    SUNG = 1; VSTRETCH = 1.0; double sp = 1.0;
    plan(1.0); double T1 = T;
    if (T1 >= target) { sp = T1/target; plan(sp); }
    else {
        VSTRETCH = 3.0; plan(1.0); double T3 = T, slope = (T3 - T1)/2.0;
        VSTRETCH = slope > 1e-6 ? 1.0 + (target - T1)/slope : 1.0; if (VSTRETCH > 14) VSTRETCH = 14;
        plan(1.0);
    }
    double *b = render_keys(pn, hz, 1, hz, dur + 0.02, dur > 0.35);
    SUNG = 0; VSTRETCH = 1.0;
    double pk = 0; for (long i = 0; i < *pn; i++) if (fabs(b[i]) > pk) pk = fabs(b[i]);
    if (pk > 1e-9) for (long i = 0; i < *pn; i++) b[i] /= pk;
    return b;
}

/* ---- lenguajes de espeak-ng (como Voz.available_langs) y flag voz_<idioma>_<texto> ---- */
static char langs[300][12]; static int nlangs = -1;
static void langs_load(void) {
    static const char *FB[] = {"en","es","fr","de","it","pt","ru","ja","zh","cmn","ko","hi","ar","tr","pl","nl","sv","no","da","fi","el","he","th","vi","id","ms","uk","cs","sk","hu","ro","bg","hr","sr","ca","eu","gl","fa","ur","bn","ta","te","ml","kn","mr","gu","pa","si","my","km","lo","ka","hy","sq","mk","lt","lv","et","is","ga","cy","af","sw","zu","xh",NULL};
    nlangs = 0; FILE *p = popen("espeak-ng --voices 2>/dev/null", "r");
    if (p) { char ln[512]; int first = 1;
        while (fgets(ln, sizeof ln, p)) { if (first) { first = 0; continue; }
            char *tok = strtok(ln, " \t\r\n"); tok = strtok(NULL, " \t\r\n"); if (!tok) continue;
            char c[12]; snprintf(c, sizeof c, "%s", tok); char *h = strchr(c, '-'); if (h) *h = 0;
            int dup = 0; for (int i = 0; i < nlangs; i++) if (!strcmp(langs[i], c)) dup = 1;
            if (!dup && nlangs < 300 && c[0]) snprintf(langs[nlangs++], 12, "%s", c); }
        pclose(p); }
    if (nlangs == 0) for (int i = 0; FB[i]; i++) snprintf(langs[nlangs++], 12, "%s", FB[i]);
}
static void parse_voz(const char *body, char *lang, const char **text) {      /* Voz.parse_voz_flag */
    if (nlangs < 0) langs_load();
    int best = -1; size_t bl = 0;
    for (int i = 0; i < nlangs; i++) { size_t L = strlen(langs[i]); if (L > bl && !strncmp(body, langs[i], L) && body[L] == '_') { best = i; bl = L; } }
    if (best >= 0) { snprintf(lang, 12, "%s", langs[best]); *text = body + bl + 1; } else { snprintf(lang, 12, "es"); *text = body; }
}
static void utf8_lower(char *s) {      /* String#downcase para ASCII, Latin-1 y cirilico */
    unsigned char *p = (unsigned char *)s;
    while (*p) {
        if (*p < 0x80) { *p = tolower(*p); p++; }
        else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E && p[1] != 0x97) { p[1] += 0x20; p += 2; }
        else if (*p == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) { p[1] += 0x20; p += 2; }
        else if (*p == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) { p[0] = 0xD1; p[1] -= 0x20; p += 2; }
        else if (*p == 0xD0 && p[1] >= 0x80 && p[1] <= 0x8F) { p[0] = 0xD1; p[1] += 0x10; p += 2; }
        else p += (*p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : 2);
    }
}

/* ---- pistas (cada def = un canal secuencial; un def repetido continua el anterior) ---- */
typedef struct { char name[64]; double *b; long n, cap; int voz; } Track;
static Track tracks[64]; static int ntr;
static void trk_add(Track *t, const double *src, long n) {
    if (t->n + n > t->cap) { t->cap = t->n + n + (long)(SR*4); t->b = realloc(t->b, t->cap*sizeof(double)); }
    if (src) memcpy(t->b + t->n, src, n*sizeof(double)); else memset(t->b + t->n, 0, n*sizeof(double)); t->n += n;
}
static Track *trk_get(const char *nm) {
    for (int i = 0; i < ntr; i++) if (!strcmp(tracks[i].name, nm)) return &tracks[i];
    if (ntr >= 64) return &tracks[63]; snprintf(tracks[ntr].name, 64, "%s", nm); return &tracks[ntr++];
}
static int is_drum(const char *s) { static const char *D[] = {"kick","snare","hihat","tom","crash","ride","clap",NULL}; for (int i = 0; D[i]; i++) if (!strcmp(s, D[i])) return 1; return 0; }

/* Directivas de CTTS dentro de un .rbsong:   #! nombre = valor
 * Para GemTunes (y cualquier lector que ignore los comentarios con #) son una linea de comentario,
 * asi que el archivo sigue siendo compatible. Prioridad: defecto < directiva de la cancion < .ctt (-c) < entorno. */
static int cfg_has(const char *k) { if (getenv(k)) return 1; for (int i = 0; i < ncfg; i++) if (!strcmp(cfg[i].k, k)) return 1; return 0; }
static int apply_directive(const char *path, int ln, char *body) {
    char *eq = strchr(body, '='); if (!eq) eq = strchr(body, ':');
    if (!eq) { fprintf(stderr, "%s:%d: directiva sin '=' (se ignora)\n", path, ln); return 0; }
    *eq = 0; char *k = trim(body), *v = trim(eq + 1); lower(k);
    for (char *c = v; *c; c++) if (*c == ',') *c = '.';
    char *dot = strchr(k, '.');
    if (dot) {                                                  /* directiva por canal:  canal.formantes = X */
        *dot = 0; char *prop = dot + 1;
        if (!strcmp(prop, "formantes") || !strcmp(prop, "fscale")) {
            set_track_fs(k, atof(v), 0);
            return 0;
        }
        fprintf(stderr, "%s:%d: propiedad de canal desconocida '%s.%s' (se ignora)\n", path, ln, k, prop); return 0;
    }
    for (Param *q = PARAMS; q->nombre; q++) {
        char ck[32]; snprintf(ck, sizeof ck, "%s", q->clave); lower(ck);
        if (!strcmp(k, q->nombre) || !strcmp(k, ck)) {
            if (cfg_has(q->clave)) return 0;                       /* un .ctt o el entorno mandan sobre la cancion */
            set_cfg(q->clave, atof(v)); return 1;
        }
    }
    fprintf(stderr, "%s:%d: directiva desconocida '%s' (se ignora)\n", path, ln, k);
    return 0;
}

static int run_song(const char *path, const char *outpath) {
    if (STEMS && mkdir(STEMS, 0777) != 0 && errno != EEXIST) { perror(STEMS); return 1; }
    FILE *f = fopen(path, "r"); if (!f) { perror(path); return 1; }
    { char l0[4096]; int n0 = 0, nd = 0;                      /* 1a pasada: solo las directivas #! */
      while (fgets(l0, sizeof l0, f)) { n0++; char *p0 = trim(l0); if (p0[0] == '#' && p0[1] == '!') nd += apply_directive(path, n0, p0 + 2); }
      rewind(f);
      if (nd) { load_tun(); if (g_noint) SININT = 1; SR = SRATE > 0 ? SRATE : 44100.0; } }
    char line[4096]; int ln = 0, nnotes = 0, nvoz = 0; Track *cur = NULL;
    while (fgets(line, sizeof line, f)) {
        ln++; char *hc = NULL; for (char *q = line; *q; q++) if ((*q == ' ' || *q == '\t') && q[1] == '#') { hc = q; break; } if (hc) *hc = 0;   /* comentario al final de la linea */
        char *p = trim(line); if (!*p || *p == '#') continue;
        if (!strncmp(p, "def", 3) && (p[3] == ' ' || p[3] == '\t')) { cur = trk_get(trim(p+4)); continue; }
        if (!strcmp(p, "end")) { cur = NULL; continue; }
        if (!cur) { fprintf(stderr, "%s:%d: nota fuera de 'def' (se ignora)\n", path, ln); continue; }
        /* silencios: $N, r, r-# */
        if (*p == '$') { double m = atof(p+1); trk_add(cur, NULL, rround(RBASE*m*SR)); continue; }
        if ((*p == 'r' || *p == 'R') && (p[1] == 0 || p[1] == '-')) { int sost = strchr(p, '#') != NULL; trk_add(cur, NULL, rround(RBASE*(sost ? 2 : 1)*SR)); continue; }
        /* FRECUENCIA[+FRECUENCIA...]hz-VOLUMENv[-flag]... */
        char *hz = strstr(p, "hz-"); if (!hz) { fprintf(stderr, "%s:%d: sintaxis no reconocida: %s\n", path, ln, p); continue; }
        char fs[256]; size_t fl = hz - p; if (fl > 250) fl = 250; memcpy(fs, p, fl); fs[fl] = 0;
        double fr[8]; int nf = 0; for (char *q = strtok(fs, "+"); q && nf < 8; q = strtok(NULL, "+")) fr[nf++] = atof(q);
        char *vp = hz + 3; char *ve = strchr(vp, 'v'); if (!ve || nf == 0) { fprintf(stderr, "%s:%d: sintaxis no reconocida: %s\n", path, ln, p); continue; }
        double vol = atof(vp)/255.0; if (vol > 1) vol = 1; if (vol < 0) vol = 0;
        int sost = 0, hasmul = 0; double mul = 1; char wf[1024] = ""; char *fl2 = ve + 1;
        for (char *tk = strtok(fl2, "-"); tk; tk = strtok(NULL, "-")) {
            if (!*tk) continue;
            if (!strcmp(tk, "#")) sost = 1; else if (tk[0] == '$') { hasmul = 1; mul = atof(tk+1); }
            else snprintf(wf, sizeof wf, "%s", tk);
        }
        double dur = RBASE*(hasmul ? mul : (sost ? 2 : 1)); char sl[1024]; snprintf(sl, sizeof sl, "%s", wf[0] ? wf : "square"); utf8_lower(sl);
        if (SININT >= 0.5 && strncmp(sl, "voz_", 4) != 0) { long sn = rround(dur*SR); if (sn < 1) sn = 1; trk_add(cur, NULL, sn); continue; }
        long n; double *nb = NULL; int is_voz = 0; char cap_lang[12] = ""; const char *cap_txt = "";
        if (is_drum(sl)) nb = drum(sl, fr[0], vol, dur, &n);
        else if (!strncmp(sl, "voz_", 4)) {
            char lang[12]; const char *txt; parse_voz(sl + 4, lang, &txt); nvoz++; is_voz = 1; snprintf(cap_lang, sizeof cap_lang, "%s", lang); cap_txt = txt;
            if (VOZM >= 0.5) nb = g_voz(lang, txt, fr[0], vol, dur, &n);
            else {
                double fs0 = FSCALE; FSCALE = fs0 * track_fs(cur->name);
                long vn; double *vb = rb_sing(lang, txt, fr[0] > 0 ? fr[0] : 220.0, dur, &vn); FSCALE = fs0; long tot = rround(dur*SR); if (tot < 1) tot = 1;
                nb = calloc(tot + 1, sizeof(double)); n = tot;
                if (vb) { long fade = (long)(0.012*SR); if (fade > vn/3) fade = vn/3; for (long i = 0; i < fade; i++) { vb[i] *= (double)i/fade; vb[vn-1-i] *= (double)i/fade; }
                    for (long i = 0; i < vn && i < tot; i++) nb[i] = vb[i]*vol*VOZG; free(vb); }
            }
        } else {
            Wave w = W_SQ; int ok = 1;
            if (!strcmp(sl,"square")) w = W_SQ; else if (!strcmp(sl,"triangle")) w = W_TRI; else if (!strcmp(sl,"saw")) w = W_SAW; else if (!strcmp(sl,"sine")) w = W_SIN;
            else if (!strcmp(sl,"hexagon")) w = W_HEX; else if (!strcmp(sl,"noise")) w = W_NOISE; else if (!strcmp(sl,"noiseshort")) w = W_NOISES; else ok = 0;
            if (!ok) fprintf(stderr, "%s:%d: flag desconocida '%s' (uso square)\n", path, ln, wf);
            if (nf == 1) nb = render_osc(w, fr[0], dur, vol, &n);
            else {                                   /* acorde: una voz por tono, suma / sqrt(N) */
                double *tb[8]; long tn = 0; for (int k = 0; k < nf; k++) tb[k] = render_osc(w, fr[k], dur, vol, &tn);
                double norm = sqrt((double)nf); nb = calloc(tn + 1, sizeof(double)); n = tn;
                for (int k = 0; k < nf; k++) for (long i = 0; i < tn; i++) nb[i] += tb[k][i];
                for (long i = 0; i < tn; i++) nb[i] /= norm; for (int k = 0; k < nf; k++) free(tb[k]);
            }
        }
        if (is_voz && ncaps < 8192) {
            Cap *c = &caps[ncaps++]; c->track = (int)(cur - tracks); c->start = cur->n; c->end = cur->n + n;
            snprintf(c->lang, sizeof c->lang, "%s", cap_lang); snprintf(c->text, sizeof c->text, "%s", cap_txt);
            for (char *q = c->text; *q; q++) if (*q == '_') *q = ' ';
        }
        if (is_voz) cur->voz = 1;
        trk_add(cur, nb, n); free(nb); nnotes++;
    }
    fclose(f);
    if (SININT >= 0.5) {                      /* --no-int: solo quedan los canales que tienen voz */
        int map[64], nk = 0, dropped = 0;
        for (int i = 0; i < ntr; i++) {
            if (tracks[i].voz) { map[i] = nk; if (nk != i) tracks[nk] = tracks[i]; nk++; }
            else { map[i] = -1; free(tracks[i].b); dropped++; }
        }
        for (int c = 0; c < ncaps; c++) caps[c].track = map[caps[c].track];
        ntr = nk;
        if (ntr == 0) { fprintf(stderr, "--no-int: la cancion no tiene ningun canal de voz\n"); return 1; }
        fprintf(stderr, "--no-int: %d canal(es) de instrumentos omitido(s), quedan %d de voz\n", dropped, ntr);
    }
    long N = 0; for (int i = 0; i < ntr; i++) if (tracks[i].n > N) N = tracks[i].n;
    if (N == 0) { fprintf(stderr, "la cancion esta vacia\n"); return 1; }
    /* Mixer.mix: suma de canales; si el pico pasa de 1.0 se divide entre el pico */
    double *mix = calloc(N, sizeof(double)); for (int t = 0; t < ntr; t++) for (long i = 0; i < tracks[t].n; i++) mix[i] += tracks[t].b[i];
    double pk = 0; for (long i = 0; i < N; i++) if (fabs(mix[i]) > pk) pk = fabs(mix[i]); if (pk > 1.0) for (long i = 0; i < N; i++) mix[i] /= pk;
    short *out = malloc(sizeof(short)*N); for (long i = 0; i < N; i++) out[i] = (short)rround(clamp1(mix[i])*32767.0);
    FILE *fo = fopen(outpath, "wb"); if (!fo) { perror("wav"); return 1; }
    int sr = (int)SR, br = sr*2, ds = (int)(N*2), cs = 36 + ds; short one = 1, bits = 16, al = 2; int sixteen = 16;
    fwrite("RIFF",1,4,fo); fwrite(&cs,4,1,fo); fwrite("WAVEfmt ",1,8,fo); fwrite(&sixteen,4,1,fo); fwrite(&one,2,1,fo); fwrite(&one,2,1,fo);
    fwrite(&sr,4,1,fo); fwrite(&br,4,1,fo); fwrite(&al,2,1,fo); fwrite(&bits,2,1,fo); fwrite("data",1,4,fo); fwrite(&ds,4,1,fo); fwrite(out,2,N,fo); fclose(fo);
    fprintf(stderr, "rbsong: %d pistas, %d notas (%d de voz), %.2f s, %d Hz, voz %s%s\n", ntr, nnotes, nvoz, (double)N/SR, sr, VOZM >= 0.5 ? "GemTunes" : "CTTS", pk > 1.0 ? "" : "");
    for (int i = 0; i < ntr; i++) fprintf(stderr, "  %-12s %.2f s\n", tracks[i].name, tracks[i].n/SR);
    if (STEMS) {
        if (mkdir(STEMS, 0777) != 0 && errno != EEXIST) { perror(STEMS); return 1; }
        char fn[1024]; snprintf(fn, sizeof fn, "%s/manifest.json", STEMS); FILE *mj = fopen(fn, "w"); if (!mj) { perror(fn); return 1; }
        fprintf(mj, "{\n  \"sample_rate\": %d,\n  \"samples\": %ld,\n  \"mix_peak\": %.9g,\n  \"voz_motor\": %d,\n  \"tracks\": [\n", sr, N, pk, VOZM >= 0.5 ? 1 : 0);
        float *fb = malloc(sizeof(float)*N);
        for (int t = 0; t < ntr; t++) {
            for (long k = 0; k < N; k++) fb[k] = k < tracks[t].n ? (float)tracks[t].b[k] : 0.0f;
            snprintf(fn, sizeof fn, "%s/track%d.f32", STEMS, t); FILE *tf = fopen(fn, "wb"); if (!tf) { perror(fn); return 1; } fwrite(fb, sizeof(float), N, tf); fclose(tf);
            fprintf(mj, "    {\"name\": \"%s\", \"file\": \"track%d.f32\", \"samples\": %ld, \"captions\": [", tracks[t].name, t, tracks[t].n);
            int first = 1;
            for (int c = 0; c < ncaps; c++) if (caps[c].track == t) {
                fprintf(mj, "%s\n      {\"start\": %.6f, \"end\": %.6f, \"lang\": \"%s\", \"text\": \"", first ? "" : ",", caps[c].start/SR, caps[c].end/SR, caps[c].lang); first = 0;
                for (const char *q = caps[c].text; *q; q++) { unsigned char u = *q; if (u == '"' || u == '\\') fprintf(mj, "\\%c", u); else if (u < 0x20) fprintf(mj, "\\u%04x", u); else fputc(u, mj); }
                fprintf(mj, "\"}");
            }
            fprintf(mj, "%s]}%s\n", first ? "" : "\n    ", t + 1 < ntr ? "," : "");
        }
        fprintf(mj, "  ]\n}\n"); fclose(mj); free(fb);
        fprintf(stderr, "canales exportados en %s/ (%d pistas)\n", STEMS, ntr);
    }
    return 0;
}

int main(int argc, char **argv) {
    static char stdin_buf[100000];
    const char *ctt = NULL, *infile = NULL, *song = NULL; int dump = 0, noint = 0; const char *pos[8]; int np = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i+1 < argc) ctt = argv[++i];
        else if (!strcmp(argv[i], "-f") && i+1 < argc) infile = argv[++i];
        else if (!strcmp(argv[i], "-s") && i+1 < argc) song = argv[++i];
        else if (!strcmp(argv[i], "--stems") && i+1 < argc) STEMS = argv[++i];
        else if (!strcmp(argv[i], "--no-int")) noint = 1;
        else if (!strcmp(argv[i], "--dump-ctt")) dump = 1;
        else if (np < 8) pos[np++] = argv[i];
    }
    if (ctt) load_ctt(ctt);
    load_tun();
    g_noint = noint; if (g_noint) SININT = 1;
    if (dump) { dump_ctt(); return 0; }
    if (song) { if (np < 1) { fprintf(stderr, "uso: %s -s cancion.rbsong salida.wav\n", argv[0]); return 1; } SR = SRATE > 0 ? SRATE : 44100.0; return run_song(song, pos[0]); }
    SR = SRATE > 0 ? SRATE : 22050.0;
    const char *text = NULL, *outpath = NULL; int pi = 0;
    if (infile) {
        FILE *fi = strcmp(infile, "-") ? fopen(infile, "rb") : stdin; if (!fi) { perror("entrada"); return 1; }
        size_t L = fread(stdin_buf, 1, sizeof stdin_buf - 1, fi); stdin_buf[L] = 0; if (fi != stdin) fclose(fi); text = stdin_buf;
    } else if (np > pi) text = pos[pi++];
    if (np > pi) outpath = pos[pi++];
    if (!text || !outpath) {
        fprintf(stderr, "CTTS (Carlos TTS)\nuso: %s [-c voz.ctt] \"texto\" salida.wav [velocidad] [f0]\n     %s [-c voz.ctt] -f archivo.txt|- salida.wav [velocidad] [f0]\n     %s --dump-ctt > voz.ctt     (genera un archivo de voz de ejemplo)\n     %s [-c voz.ctt] -s cancion.rbsong salida.wav [--stems DIR] [--no-int]    (motor rbsong; --stems exporta cada canal; --no-int = solo voz)\n", argv[0], argv[0], argv[0], argv[0]);
        return 1;
    }
    double speed = np > pi ? atof(pos[pi++]) : G_SPEED;
    double f0base = np > pi ? atof(pos[pi++]) : G_F0;
    argv[1] = (char *)text; argv[2] = (char *)outpath;
    parse(text); plan(speed);
    if (getenv("CTTS_DEBUG")) { for (int i = 0; i < nseg; i++) fprintf(stderr, "%c %.3f\n", seg[i].c==' '?'_':seg[i].c, segT[i]); }

    long N; double *buf = render_keys(&N, f0base, 0, 0, 0, 0); double total = keys[nkeys-1].t;
    short *out = malloc(sizeof(short)*N);
    double peak = 0; for (long n = 0; n < N; n++) if (fabs(buf[n]) > peak) peak = fabs(buf[n]);
    if (peak < 1e-9) peak = 1;
    for (long n = 0; n < N; n++) out[n] = (short)(buf[n]/peak*VOL*32767);
    FILE *f = fopen(argv[2], "wb"); if (!f) { perror("wav"); return 1; }
    int sr = (int)SR; int br = sr*2; int ds = N*2, cs = 36+ds; short one=1, bits=16, al=2; int sixteen=16;
    fwrite("RIFF",1,4,f); fwrite(&cs,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&sixteen,4,1,f);
    fwrite(&one,2,1,f); fwrite(&one,2,1,f); fwrite(&sr,4,1,f); fwrite(&br,4,1,f); fwrite(&al,2,1,f); fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&ds,4,1,f); fwrite(out,2,N,f); fclose(f);
    fprintf(stderr, "%.2f s\n", total);
    return 0;
}
