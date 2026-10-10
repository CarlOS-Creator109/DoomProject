#!/usr/bin/env python3
"""build_music.py - arma la música de Doom Project:  MUS -> MIDI -> .rbsong (midi2rbsong.py) -> WAV (motor CTTS en C).
Uso: build_music.py DOOM.WAD [carpeta_salida=music] [--only D_E1M1,D_E1M2] [--keep]   (necesita: python3 mido, gcc o ./ctts)"""
import os, subprocess, sys, struct, shutil
from concurrent.futures import ThreadPoolExecutor
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
from mus2mid import mus_to_mid

def lumps(path):
    f = open(path, 'rb').read(); n, ofs = struct.unpack_from('<ii', f, 4); d = {}
    for i in range(n):
        pos, size, nm = struct.unpack_from('<ii8s', f, ofs + 16 * i); d[nm.rstrip(b'\0').decode('ascii', 'ignore').upper()] = f[pos:pos + size]
    return d

def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]; only = None; keep = '--keep' in sys.argv
    for i, x in enumerate(sys.argv):
        if x == '--only': only = set(sys.argv[i + 1].upper().split(',')); a = [y for y in a if y != sys.argv[i + 1]]
    if not a: sys.exit(__doc__)
    out = a[1] if len(a) > 1 else 'music'; os.makedirs(out, exist_ok=True)
    ctts = os.path.join(here, 'ctts.exe' if os.name == 'nt' else 'ctts')
    if not os.path.exists(ctts): subprocess.check_call([os.environ.get('CC', 'gcc'), '-O2', '-o', ctts, os.path.join(here, 'ctts.c'), '-lm'])
    L = lumps(a[0]); names = [n for n, d in L.items() if n.startswith('D_') and d[:4] == b'MUS\x1a' and (not only or n in only)]
    def one(n):
        mid = os.path.join(out, n + '.mid'); rb = os.path.join(out, n + '.rbsong'); wav = os.path.join(out, n + '.wav')
        open(mid, 'wb').write(mus_to_mid(L[n]))
        subprocess.run([sys.executable, os.path.join(here, 'midi2rbsong.py'), mid, rb, '--mode', 'tracks', '--drums', 'midi', '--gain', '1.0'], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([ctts, '-s', rb, wav], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not keep:
            os.remove(mid); os.remove(rb)
        return n, os.path.getsize(wav) / 88200
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as ex:
        for n, s in ex.map(one, sorted(names)): print('%-10s %6.1f s' % (n, s), flush=True)
    print('listo: %d piezas en %s/' % (len(names), out))
main()
