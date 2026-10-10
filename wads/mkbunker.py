#!/usr/bin/env python3
"""mkbunker.py - arma BUNKER.WAD: un nivel propio para Doom 1 (mapa E1M1), formato Doom clasico.
Los sectores se describen como rectangulos; el script saca vertices, lineas, lados, cosas, y zdbsp hace los nodos.
Usa solo texturas/flats del DOOM.WAD (se cargan del IWAD): el PWAD no lleva arte ajeno.
Uso: mkbunker.py [salida.wad]      (necesita zdbsp: apt install zdbsp)
Se juega con:  ./doom_game DOOM.WAD BUNKER.WAD      o      gzdoom -iwad DOOM.WAD -file BUNKER.WAD"""
import struct, subprocess, sys, os, tempfile, shutil

# ---------------------------------------------------------------- sectores
# rects: lista de (x1,y1,x2,y2). Todo con y hacia arriba (norte).
S = {}
def sec(name, rects, fl=0, ce=128, ft='FLOOR4_8', ct='CEIL3_5', light=160, special=0, wall='STARTAN3', **kw):
    S[name] = dict(rects=rects, fl=fl, ce=ce, ft=ft, ct=ct, light=light, special=special, wall=wall, **kw)

sec('R1',  [(0,0,384,320)],      wall='STARTAN3', light=176)
sec('D1',  [(384,96,400,224)],   fl=0, ce=0, ft='FLAT20', ct='FLAT20', light=128, door='BIGDOOR2', track='DOORTRAK', dspecial=1)
sec('C1',  [(400,96,720,224)],   wall='STARG3', ft='FLOOR5_1', ct='CEIL5_1', light=192, special=1)
# sala del nukage: anillo + foso (con isla y pasarela)
sec('N1',  [(720,0,1120,80),(720,240,1120,320),(720,80,800,240),(1040,80,1120,240)],
           ce=160, wall='BROWN1', ft='FLOOR4_1', ct='CEIL4_2', light=160)
sec('PIT', [(800,80,1040,128),(800,192,1040,240),(800,128,896,144),(800,176,896,192),(960,128,1040,192)],
           fl=-24, ce=160, wall='BROWN1', ft='NUKAGE1', ct='CEIL4_2', light=224, special=7, lowtex='SLADWALL')
sec('ISL', [(896,128,960,192)],  ce=160, wall='BROWN1', ft='FLAT5_5', ct='CEIL4_2', light=224)
sec('BRG', [(800,144,896,176)],  ce=160, wall='BROWN1', ft='FLAT23',  ct='CEIL4_2', light=208)
sec('C2',  [(976,320,1104,480)], wall='STARG3', ft='FLOOR5_1', ct='CEIL5_1', light=128)
sec('R2',  [(900,480,1300,720)], holes=['PL','LDG'], fl=8, wall='COMPSTA1', ft='FLOOR0_3', ct='CEIL3_5', light=144, special=1)
sec('PL',  [(1060,570,1124,634)],fl=128, ce=128, wall='COMPSTA1', ft='FLAT10', ct='FLAT10', light=144)
sec('LDG', [(1040,672,1160,720)],fl=24, wall='COMPSTA1', ft='FLOOR3_3', ct='CEIL3_5', light=255, special=8)
sec('C4',  [(700,544,900,672)],  wall='STARG3', ft='FLOOR5_1', ct='CEIL5_1', light=128)
sec('Y',   [(400,480,700,800)],  holes=['PA','PB'], wall='STONE3', ft='FLAT5_4', ct='F_SKY1', light=208)
sec('PA',  [(480,560,544,624)],  fl=128, ce=128, wall='STONE3', ft='FLAT10', ct='FLAT10', light=208)
sec('PB',  [(560,680,624,744)],  fl=128, ce=128, wall='STONE3', ft='FLAT10', ct='FLAT10', light=208)
sec('D3',  [(384,544,400,672)],  fl=0, ce=0, ft='FLAT20', ct='FLAT20', light=128, door='BIGDOOR4', track='DOORBLU', dspecial=26)
sec('C5',  [(192,544,384,672)],  wall='STARG3', ft='FLOOR5_1', ct='CEIL5_1', light=128)
sec('X',   [(0,448,192,768)],    wall='STARGR1', ft='FLOOR5_1', ct='CEIL5_1', light=200)
sec('EXT', [(-32,576,0,640)],    ce=72, wall='STARGR1', ft='FLAT5_5', ct='CEIL5_1', light=255)
sec('D4',  [(-16,96,0,224)],     fl=0, ce=0, ft='FLAT20', ct='FLAT20', light=160, door='STARTAN2', track='DOORTRAK', dspecial=1, secretline=True)
sec('SEC', [(-256,64,-16,256)],  wall='STARTAN3', ft='FLOOR4_8', ct='CEIL3_5', light=160, special=9)

# paredes con interruptor: (sector, direccion de la arista) -> (textura, special)
OVR = {('EXT','W'): ('SW1EXIT', 11)}

THINGS = [  # x, y, angulo, tipo, flags (7=todas las dificultades, +8 emboscada)
 (192,160,0,1,7),(192,224,0,2,7),(192,96,0,3,7),(128,160,0,4,7),
 (32,32,0,2028,7),(352,32,0,2028,7),(32,288,0,2028,7),(352,288,0,2028,7),
 (96,60,0,2007,7),(300,60,0,2011,7),(340,280,270,3004,7),
 (600,160,180,3004,7),
 (800,40,90,3004,7),(1000,40,90,3004,7),(1080,160,180,9,7),(900,280,270,3001,7),(1060,280,270,3001,7),
 (920,160,0,2001,7),(770,160,0,2007,7),(760,40,0,2011,7),(1100,300,0,2012,7),(740,300,0,2035,7),(760,300,0,2035,7),
 (1040,400,0,2007,7),(1040,440,0,2007,7),
 (1000,640,270,3001,7),(1240,540,180,3001,7),(1240,680,180,3002,15),(1100,696,0,5,7),
 (960,520,0,2008,7),(1260,520,0,2008,7),(940,700,0,2012,7),(924,500,0,2028,7),(1276,500,0,2028,7),(1276,700,0,2028,7),
 (800,608,180,3004,7),
 (600,700,270,3001,7),(500,700,270,3001,7),(480,520,0,9,7),(570,620,180,3002,7),
 (450,640,0,2035,7),(450,656,0,2035,7),(462,648,0,2035,7),
 (520,500,0,2014,7),(560,500,0,2014,7),(600,500,0,2014,7),(640,500,0,2014,7),(660,760,0,2008,7),(430,780,0,2011,7),
 (300,608,0,2011,7),
 (20,468,0,2028,7),(172,468,0,2028,7),(96,608,0,2018,7),
 (-136,160,0,2018,7),(-200,100,0,2015,7),(-200,220,0,2015,7),(-100,160,0,2007,7),(-60,100,0,2011,7),
]

# ---------------------------------------------------------------- geometria
def sub(r, h):
    # r menos h (ambos (x1,y1,x2,y2)); devuelve lista de rectangulos
    x1,y1,x2,y2 = r; a,b,c,d = h
    if min(x2,c)<=max(x1,a) or min(y2,d)<=max(y1,b): return [r]
    out = []
    if y1<b: out.append((x1,y1,x2,b))
    if d<y2: out.append((x1,d,x2,y2))
    ya,yb = max(y1,b),min(y2,d)
    if x1<a: out.append((x1,ya,a,yb))
    if c<x2: out.append((c,ya,x2,yb))
    return out
for n in list(S):
    for hn in S[n].get('holes',[]):
        for h in S[hn]['rects']:
            S[n]['rects'] = [q for r in S[n]['rects'] for q in sub(r,h)]
names = list(S)
idx = {n:i for i,n in enumerate(names)}
# comprobar que no hay solapes de area entre rectangulos
allr = [(n,r) for n in names for r in S[n]['rects']]
for i,(na,a) in enumerate(allr):
    assert a[0]<a[2] and a[1]<a[3], (na,a)
    for nb,b in allr[i+1:]:
        if min(a[2],b[2])>max(a[0],b[0]) and min(a[3],b[3])>max(a[1],b[1]):
            sys.exit('rectangulos solapados: %s %s y %s %s' % (na,a,nb,b))
corners = {(x,y) for _,r in allr for x in (r[0],r[2]) for y in (r[1],r[3])}

def split(a, b):
    # parte el segmento axis-aligned a->b en todos los puntos-esquina que caen en su interior
    (x1,y1),(x2,y2) = a,b
    pts = [a,b]
    for (x,y) in corners:
        if (x1==x2==x and min(y1,y2)<y<max(y1,y2)) or (y1==y2==y and min(x1,x2)<x<max(x1,x2)): pts.append((x,y))
    horiz = (y1==y2)
    pts.sort(key=lambda p:(p[0] if horiz else p[1]), reverse=(x2<x1 if horiz else y2<y1))
    return list(zip(pts, pts[1:]))

pieces = {}   # clave -> lista de (sector, a, b, dir)
for n in names:
    for (x1,y1,x2,y2) in S[n]['rects']:
        edges = [((x1,y1),(x1,y2),'W'), ((x1,y2),(x2,y2),'N'), ((x2,y2),(x2,y1),'E'), ((x2,y1),(x1,y1),'S')]   # sentido horario
        for a,b,d in edges:
            for p,q in split(a,b):
                pieces.setdefault(frozenset((p,q)), []).append((n,p,q,d))

verts = []; vid = {}
def V(p):
    if p not in vid: vid[p]=len(verts); verts.append(p)
    return vid[p]

lines = []; sides = []
def side(sector, upper, lower, mid):
    sides.append((0,0,upper,lower,mid,idx[sector])); return len(sides)-1
def isdoor(n): return 'door' in S[n]

for key, pcs in pieces.items():
    if len(pcs)==1:
        n,p,q,d = pcs[0]; s = S[n]
        tex = s['track'] if isdoor(n) else s['wall']; sp = 0
        if (n,d) in OVR: tex, sp = OVR[(n,d)]
        sd = side(n,'-','-',tex)
        lines.append((V(p),V(q),1,sp,0,sd,-1))
    elif len(pcs)==2:
        (na,pa,qa,da),(nb,pb,qb,db) = pcs
        if na==nb: continue                      # arista interior de un mismo sector
        assert pa==qb and qa==pb, (pcs)
        if isdoor(na) and not isdoor(nb): (na,pa,qa,da),(nb,pb,qb,db) = (nb,pb,qb,db),(na,pa,qa,da)
        # na = frente (el que se ve desde el cuarto), nb = dorso
        def up(x,y): return S[y]['door'] if isdoor(y) else S[x]['wall']
        def lo(x,y): return S[x].get('lowtex', S[x]['wall'])
        fs = side(na, up(na,nb), lo(na,nb), '-')
        bs = side(nb, up(nb,na), lo(nb,na), '-')
        flags = 4; special = 0
        for d_ in (na,nb):
            if isdoor(d_):
                flags |= 8; special = S[d_]['dspecial']
                if S[d_].get('secretline'): flags |= 32
        lines.append((V(pa),V(qa),flags,special,0,fs,bs))
    else:
        sys.exit('arista con %d sectores: %s' % (len(pcs), pcs))

# ---------------------------------------------------------------- lumps
def nm(s): return s.encode('ascii').ljust(8,b'\0')
L_vert = b''.join(struct.pack('<hh',*p) for p in verts)
L_line = b''.join(struct.pack('<HHHHHHH',a,b,f,sp,tg,r,(l if l>=0 else 0xFFFF)) for a,b,f,sp,tg,r,l in lines)
L_side = b''.join(struct.pack('<hh8s8s8sH',xo,yo,nm(u),nm(lw),nm(m),s) for xo,yo,u,lw,m,s in sides)
L_sect = b''.join(struct.pack('<hh8s8shhh',S[n]['fl'],S[n]['ce'],nm(S[n]['ft']),nm(S[n]['ct']),S[n]['light'],S[n]['special'],0) for n in names)
L_thng = b''.join(struct.pack('<hhhhh',*t) for t in THINGS)

def wad(lumps):
    out = b'PWAD' + struct.pack('<ii', len(lumps), 0); d=b''; pos=12
    for name,data in lumps:
        d += struct.pack('<ii8s', pos if data else 0, len(data), nm(name)); out += data; pos += len(data)
    return out[:8] + struct.pack('<i', pos) + out[12:] + d

info = ('Bunker de Carlos - nivel para Doom 1 (E1M1)\nHecho con Doom Project / mkbunker.py\n'
        'Necesita DOOM.WAD: doom_game DOOM.WAD BUNKER.WAD   o   gzdoom -iwad DOOM.WAD -file BUNKER.WAD\n').encode()
raw = wad([('E1M1',b''),('THINGS',L_thng),('LINEDEFS',L_line),('SIDEDEFS',L_side),('VERTEXES',L_vert),
           ('SEGS',b''),('SSECTORS',b''),('NODES',b''),('SECTORS',L_sect),('REJECT',b''),('BLOCKMAP',b'')])
out = sys.argv[1] if len(sys.argv)>1 else 'BUNKER.WAD'
tmp = tempfile.mkdtemp(); src = os.path.join(tmp,'in.wad'); dst = os.path.join(tmp,'out.wad')
open(src,'wb').write(raw)
r = subprocess.run(['zdbsp','-o',dst,src],capture_output=True,text=True)
sys.stdout.write(r.stdout[-600:]); sys.stderr.write(r.stderr[-600:])
if not os.path.exists(dst): sys.exit('zdbsp fallo')
shutil.copy(dst,out)
print('%s: %d sectores, %d vertices, %d lineas, %d lados, %d cosas' % (out,len(names),len(verts),len(lines),len(sides),len(THINGS)))
