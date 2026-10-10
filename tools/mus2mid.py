#!/usr/bin/env python3
"""mus2mid.py - convierte música MUS de Doom a MIDI (tipo 0, 70 ppq a 120 BPM = 140 ticks/s, como el MUS).
Uso: mus2mid.py entrada.mus salida.mid     (o con un WAD:  mus2mid.py --wad DOOM.WAD D_E1M1 salida.mid)"""
import struct, sys

def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n: b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))

def mus_to_mid(d):
    if d[:4] != b'MUS\x1a': raise ValueError('no es un MUS')
    score_len, score_start = struct.unpack_from('<HH', d, 4)
    p, end = score_start, min(len(d), score_start + score_len)
    out = bytearray(); delta = 0; lastvol = [127] * 16
    cmap = lambda c: 9 if c == 15 else (c + 1 if c >= 9 else c)
    CC = {1: 0, 2: 1, 3: 7, 4: 10, 5: 11, 6: 91, 7: 93, 8: 64, 9: 67}
    SYS = {10: 120, 11: 123, 12: 126, 13: 127, 14: 121}
    def ev(*bs):
        nonlocal delta
        out.extend(vlq(delta)); out.extend(bytes(bs)); delta = 0
    while p < end:
        b = d[p]; p += 1
        last, typ, ch = b & 0x80, (b >> 4) & 7, cmap(b & 15)
        if typ == 0:
            n = d[p] & 127; p += 1; ev(0x80 | ch, n, 64)
        elif typ == 1:
            n = d[p]; p += 1
            if n & 0x80: lastvol[ch] = d[p] & 127; p += 1
            ev(0x90 | ch, n & 127, lastvol[ch])
        elif typ == 2:
            v = d[p] << 6; p += 1; ev(0xE0 | ch, v & 127, v >> 7)
        elif typ == 3:
            c = d[p]; p += 1
            if c in SYS: ev(0xB0 | ch, SYS[c], 0)
        elif typ == 4:
            c, v = d[p], d[p + 1]; p += 2
            if c == 0: ev(0xC0 | ch, v & 127)
            elif c == 1: ev(0xB0 | ch, 0, v & 127)
            elif c in CC: ev(0xB0 | ch, CC[c], v & 127)
        elif typ == 6: break
        elif typ == 5: pass
        else: p += 1
        if last:
            t = 0
            while True:
                x = d[p]; p += 1; t = t * 128 + (x & 127)
                if not x & 0x80: break
            delta += t
    out.extend(vlq(delta)); out.extend(b'\xFF\x2F\x00')
    tempo = b'\x00\xFF\x51\x03' + (500000).to_bytes(3, 'big')
    trk = tempo + bytes(out)
    return b'MThd' + struct.pack('>IHHH', 6, 0, 1, 70) + b'MTrk' + struct.pack('>I', len(trk)) + trk

def wad_lump(path, name):
    f = open(path, 'rb').read()
    n, ofs = struct.unpack_from('<ii', f, 4)
    for i in range(n - 1, -1, -1):
        pos, size, nm = struct.unpack_from('<ii8s', f, ofs + 16 * i)
        if nm.rstrip(b'\0').decode('ascii', 'ignore').upper() == name.upper(): return f[pos:pos + size]
    raise KeyError(name)

if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--wad': data, out = wad_lump(a[1], a[2]), a[3]
    elif len(a) == 2: data, out = open(a[0], 'rb').read(), a[1]
    else: sys.exit(__doc__)
    open(out, 'wb').write(mus_to_mid(data))
