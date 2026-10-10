#!/usr/bin/env python3
"""midi2rbsong.py - convierte un archivo MIDI en una cancion .rbsong (CTTS / GemTunes).

Que hace
  * Lee el mapa de tempos y de compases del MIDI (varios cambios de tempo y de compas).
  * Reparte las notas en canales monofonicos (una voz por canal, como pide el formato):
      --mode tracks  cada pista/canal MIDI -> uno o mas canales (polifonia repartida en carriles)
      --mode piano   reduccion de piano -> melodia (arriba) + armonia (acordes) + bajo
      --mode auto    (por defecto) piano si el MIDI tiene una sola pista melodica; si no, tracks
  * Percusion del canal 10 de General MIDI -> bombo, caja, charles, tom, platillo, palmas.
    --drums march agrega una percusion de marcha (bombo/caja/charles) aunque el MIDI no la traiga.
  * Todo se cuantiza a muestras enteras de 44,1 kHz, asi los canales nunca se desfasan entre si
    y las duraciones de cada compas son exactas.
  * --bars 1-59,76-116 usa solo esos compases (se pueden saltar secciones); --max-seconds recorta.
  * --lyrics letra.txt  canta una letra con la voz de CTTS sobre la melodia (ver abajo).

Letra (opcional)
  Archivo de texto con lineas  "COMPAS: palabra palabra:2 _ palabra"  donde COMPAS es el numero
  de compas de la SALIDA (1 = el primero). Cada palabra dura 1 tiempo; "pa:2" = 2 tiempos,
  "y:.5" = medio tiempo, "_" = silencio de 1 tiempo. La altura sale de la melodia, llevada al
  registro cantable (--sing-range 175-350 Hz). Lineas que empiezan con # son comentarios.

Uso
  python3 midi2rbsong.py entrada.mid salida.rbsong
  python3 midi2rbsong.py entrada.mid --info            # solo analiza el MIDI
  python3 midi2rbsong.py entrada.mid salida.rbsong --bars 1-32 --drums march --mode piano

Requiere: nada (si esta instalado "mido" lo usa; si no, usa un lector MIDI propio incluido aqui).
"""
import argparse, bisect, collections, os, re, sys

try:
    import mido
except ImportError:
    mido = None


class _Msg:
    """Mensaje MIDI minimo con la misma forma que usa mido (type, time=delta en ticks, y los campos de cada tipo)."""
    def __init__(self, type, time, **kw):
        self.type = type; self.time = time; self.__dict__.update(kw)


class _MidiFile:
    """Lector SMF (formato 0/1) sin dependencias: solo lo que necesita leer()."""
    def __init__(self, path):
        d = open(path, 'rb').read()
        if d[:4] != b'MThd':
            raise ValueError('no es un archivo MIDI: ' + path)
        n, self.ticks_per_beat = int.from_bytes(d[10:12], 'big'), int.from_bytes(d[12:14], 'big')
        pos = 8 + int.from_bytes(d[4:8], 'big'); self.tracks = []
        for _ in range(n):
            while d[pos:pos + 4] != b'MTrk':
                pos += 8 + int.from_bytes(d[pos + 4:pos + 8], 'big')
            ln = int.from_bytes(d[pos + 4:pos + 8], 'big'); self.tracks.append(self._track(d[pos + 8:pos + 8 + ln])); pos += 8 + ln

    @staticmethod
    def _vlq(b, i):
        v = 0
        while True:
            c = b[i]; i += 1; v = (v << 7) | (c & 0x7f)
            if not c & 0x80:
                return v, i

    def _track(self, b):
        out, i, run = [], 0, 0
        while i < len(b):
            dt, i = self._vlq(b, i); st = b[i]
            if st < 0x80:
                st = run
            else:
                i += 1
                if st < 0xf0:
                    run = st
            if st == 0xff:
                mt = b[i]; ln, i = self._vlq(b, i + 1); data = b[i:i + ln]; i += ln
                if mt == 0x51 and ln == 3:
                    out.append(_Msg('set_tempo', dt, tempo=int.from_bytes(data, 'big')))
                elif mt == 0x58 and ln >= 2:
                    out.append(_Msg('time_signature', dt, numerator=data[0], denominator=2 ** data[1]))
                elif mt == 0x03:
                    out.append(_Msg('track_name', dt, name=data.decode('latin-1')))
                else:
                    out.append(_Msg('meta', dt))
            elif st in (0xf0, 0xf7):
                ln, i = self._vlq(b, i); i += ln; out.append(_Msg('sysex', dt))
            else:
                hi, ch = st & 0xf0, st & 0x0f
                if hi in (0xc0, 0xd0):
                    a1 = b[i]; i += 1
                    out.append(_Msg('program_change', dt, channel=ch, program=a1) if hi == 0xc0 else _Msg('aftertouch', dt))
                else:
                    a1, a2 = b[i], b[i + 1]; i += 2
                    if hi == 0x90:
                        out.append(_Msg('note_on', dt, channel=ch, note=a1, velocity=a2))
                    elif hi == 0x80:
                        out.append(_Msg('note_off', dt, channel=ch, note=a1, velocity=a2))
                    else:
                        out.append(_Msg('other', dt))
        return out


if mido is None:
    class mido:                                      # noqa: N801  (sustituto de mido)
        MidiFile = _MidiFile

SR = 44100
U = SR * 0.25                       # muestras por unidad de tiempo del formato (0.25 s)
Note = collections.namedtuple('Note', 'track chan pitch start end vel')


def fmt(u):
    return ('%.9f' % u).rstrip('0').rstrip('.')


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12.0)


# ------------------------------------------------------------------ instrumentos GM
FAMILIAS = ['piano', 'percusion', 'organo', 'guitarra', 'bajo', 'cuerdas', 'ensamble', 'metales',
            'lengueta', 'flauta', 'lead', 'pad', 'efectos', 'etnico', 'percusivo', 'sonidos']
ONDA_FAMILIA = ['triangle', 'sine', 'square', 'saw', 'triangle', 'saw', 'saw', 'square',
                'square', 'sine', 'square', 'triangle', 'saw', 'triangle', 'sine', 'square']
# percusion General MIDI -> (canal rbsong, instrumento del motor, frecuencia, duracion en unidades)
DRUMS = {}
for _n in (35, 36): DRUMS[_n] = ('bombo', 'kick', 60, 1.0)
for _n in (37, 38, 40): DRUMS[_n] = ('caja', 'snare', 190, 0.75)
for _n in (42, 44, 46, 54, 69, 70, 82): DRUMS[_n] = ('charles', 'hihat', 200, 0.4)
for _n in (41, 43, 45, 47, 48, 50): DRUMS[_n] = ('tom', 'tom', None, 1.0)
for _n in (49, 52, 55, 57): DRUMS[_n] = ('platillo', 'crash', 300, 4.0)
for _n in (51, 53, 59): DRUMS[_n] = ('platillo', 'ride', 300, 2.0)
for _n in (39,): DRUMS[_n] = ('palmas', 'clap', 190, 0.75)
DRUM_OTROS = ('charles', 'hihat', 200, 0.4)


def ident(s):
    s = re.sub(r'[^a-z0-9]+', '_', s.lower().encode('ascii', 'ignore').decode()).strip('_')
    s = s or 'pista'
    return ('p_' + s) if s[0].isdigit() else s


# ------------------------------------------------------------------ lectura del MIDI
def leer(path):
    mid = mido.MidiFile(path)
    tpb = mid.ticks_per_beat
    tempos, tsigs, notes = [], [], []
    progs, names = {}, {}
    for ti, tr in enumerate(mid.tracks):
        t = 0
        on = {}
        for m in tr:
            t += m.time
            if m.type == 'set_tempo':
                tempos.append((t, m.tempo))
            elif m.type == 'time_signature':
                tsigs.append((t, m.numerator, m.denominator))
            elif m.type == 'track_name':
                names.setdefault(ti, m.name)
            elif m.type == 'program_change':
                progs.setdefault((ti, m.channel), m.program)
            elif m.type == 'note_on' and m.velocity > 0:
                on.setdefault((m.channel, m.note), []).append((t, m.velocity))
            elif m.type in ('note_off', 'note_on'):
                q = on.get((m.channel, m.note))
                if q:
                    s, v = q.pop(0)
                    notes.append(Note(ti, m.channel, m.note, s, max(t, s + 1), v))
        for (ch, n), q in on.items():                      # notas sin cerrar: terminan con la pista
            for s, v in q:
                notes.append(Note(ti, ch, n, s, max(t, s + 1), v))
    tempos = sorted(set(tempos)) or [(0, 500000)]
    if tempos[0][0] != 0:
        tempos.insert(0, (0, tempos[0][1]))
    tsigs = sorted(set(tsigs)) or [(0, 4, 4)]
    if tsigs[0][0] != 0:
        tsigs.insert(0, (0, tsigs[0][1], tsigs[0][2]))
    notes.sort(key=lambda n: (n.start, n.pitch))
    return dict(tpb=tpb, tempos=tempos, tsigs=tsigs, notes=notes, progs=progs, names=names, ntracks=len(mid.tracks))


class Reloj:
    """ticks -> muestras, con mapa de tempos (y --tempo / --speed opcionales)."""
    def __init__(self, tempos, tpb, bpm=None, speed=1.0):
        if bpm:
            tempos = [(0, int(round(60e6 / bpm)))]
        self.tpb = tpb
        self.t = [x[0] for x in tempos]
        self.us = [x[1] / speed for x in tempos]
        self.sec0 = [0.0]
        for i in range(1, len(tempos)):
            self.sec0.append(self.sec0[-1] + (self.t[i] - self.t[i - 1]) * self.us[i - 1] / (tpb * 1e6))

    def sec(self, tick):
        i = bisect.bisect_right(self.t, tick) - 1
        return self.sec0[i] + (tick - self.t[i]) * self.us[i] / (self.tpb * 1e6)

    def smp(self, tick):
        return int(round(self.sec(tick) * SR))


def compases(tsigs, tpb, end_tick):
    out, t, i = [], 0, 0
    while t < end_tick:
        while i + 1 < len(tsigs) and tsigs[i + 1][0] <= t:
            i += 1
        n, d = tsigs[i][1], tsigs[i][2]
        L = max(1, int(round(n * tpb * 4 / d)))
        out.append((t, L, n, d))
        t += L
    return out


def parse_bars(txt, nbars):
    rng = []
    for part in txt.split(','):
        part = part.strip()
        if not part:
            continue
        a, _, b = part.partition('-')
        a = int(a); b = int(b) if b else a
        if a < 1 or b < a or b > nbars:
            sys.exit("--bars fuera de rango: %s (el MIDI tiene %d compases)" % (part, nbars))
        if rng and a == rng[-1][1] + 1:
            rng[-1] = (rng[-1][0], b)
        else:
            rng.append((a, b))
    return rng


# ------------------------------------------------------------------ salida (canales monofonicos)
class Canal:
    def __init__(self, nombre):
        self.nombre = nombre
        self.lines = []
        self.cur = 0
        self.notas = 0

    def rest(self, n):
        if n > 0:
            self.lines.append('  $' + fmt(n / U))

    def nota(self, st, en, freqs, vol, onda, gap):
        """escribe una nota entre las muestras st y en (con silencio de articulacion al final)."""
        dur = en - st
        if dur <= 0 or st < self.cur:
            return False
        g = min(gap, int(dur * 0.2))
        self.rest(st - self.cur)
        fr = "+".join('%.2f' % f for f in freqs)
        self.lines.append('  %shz-%dv-$%s-%s' % (fr, vol, fmt((dur - g) / U), onda))
        if g > 0:
            self.rest(g)
        self.cur = st + dur
        self.notas += 1
        return True

    def cerrar(self, total):
        if self.cur < total:
            self.rest(total - self.cur)
            self.cur = total


def mono(c, eventos, vol_fn, onda, gap):
    """eventos: (st, en, [pitches], vel) ordenados; cada evento corta al siguiente."""
    ev = sorted(eventos, key=lambda e: e[0])
    fus = []
    for st, en, ps, v in ev:                                   # mismo inicio = acorde
        if fus and fus[-1][0] == st:
            fus[-1] = (st, max(fus[-1][1], en), sorted(set(fus[-1][2] + ps)), max(fus[-1][3], v))
        else:
            fus.append((st, en, ps, v))
    for i, (st, en, ps, v) in enumerate(fus):
        if i + 1 < len(fus):
            en = min(en, fus[i + 1][0])
        c.nota(st, en, [hz(p) for p in ps], vol_fn(v), onda, gap)


# ------------------------------------------------------------------ principal
def main():
    ap = argparse.ArgumentParser(description='MIDI -> .rbsong (CTTS / GemTunes).', formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog='Ejemplos:\n  midi2rbsong.py cancion.mid --info\n  midi2rbsong.py cancion.mid salida.rbsong\n  midi2rbsong.py cancion.mid salida.rbsong --bars 1-32 --drums march --mode piano\n  midi2rbsong.py cancion.mid salida.rbsong --bars 1-59,76-116 --tempo 180\n  midi2rbsong.py cancion.mid salida.rbsong --lyrics letra.txt --lyrics-lang es\n\nLetra: lineas "COMPAS: palabra palabra:2 _ ..." (compas de la salida; ":N" = N tiempos; "_" = silencio).')
    ap.add_argument('midi'); ap.add_argument('salida', nargs='?')
    ap.add_argument('--info', action='store_true', help='solo analizar el MIDI')
    ap.add_argument('--mode', choices=['auto', 'tracks', 'piano'], default='auto')
    ap.add_argument('--bars', help='compases a usar, p. ej. 1-59,76-116')
    ap.add_argument('--max-seconds', type=float, help='recortar la salida a N segundos')
    ap.add_argument('--tempo', type=float, help='forzar un tempo fijo en BPM')
    ap.add_argument('--speed', type=float, default=1.0, help='factor de velocidad (2 = el doble de rapido)')
    ap.add_argument('--transpose', type=int, default=0, help='semitonos')
    ap.add_argument('--drums', choices=['auto', 'none', 'midi', 'march'], default='auto')
    ap.add_argument('--drum-from', type=int, default=1, help='(march) compas de salida donde entra la percusion')
    ap.add_argument('--lanes', default='melody,harmony,bass', help='(piano) carriles a escribir')
    ap.add_argument('--bass-split', type=int, default=55, help='(piano) notas MIDI por debajo van al bajo')
    ap.add_argument('--wave', help='forzar una onda (sine, square, saw, triangle, hexagon)')
    ap.add_argument('--gain', type=float, default=1.0)
    ap.add_argument('--gap', type=float, default=0.1, help='silencio de articulacion en unidades (0 = legato)')
    ap.add_argument('--max-lanes', type=int, default=3, help='(tracks) carriles por pista')
    ap.add_argument('--max-chord', type=int, default=4, help='notas maximas por acorde')
    ap.add_argument('--lyrics', help='archivo de letra (ver --help del modulo)')
    ap.add_argument('--lyrics-lang', default='es')
    ap.add_argument('--sing-range', default='175-350')
    a = ap.parse_args()

    M = leer(a.midi)
    tpb, notes = M['tpb'], M['notes']
    if not notes:
        sys.exit('el MIDI no tiene notas')
    end_tick = max(n.end for n in notes)
    reloj = Reloj(M['tempos'], tpb, a.tempo, a.speed)
    bars = compases(M['tsigs'], tpb, end_tick)
    drum_notes = [n for n in notes if n.chan == 9]
    mel_notes = [n for n in notes if n.chan != 9]
    grupos = collections.OrderedDict()
    for n in mel_notes:
        grupos.setdefault((n.track, n.chan), []).append(n)

    if a.info:
        print('archivo: %s | pistas: %d | ticks por negra: %d' % (a.midi, M['ntracks'], tpb))
        print('duracion: %.1f s | compases: %d | notas: %d (%d de percusion)' % (reloj.sec(end_tick), len(bars), len(notes), len(drum_notes)))
        print('tempos:', ', '.join('%d BPM @ tick %d' % (round(60e6 / us), t) for t, us in M['tempos'][:8]) + (' ...' if len(M['tempos']) > 8 else ''))
        print('compas:', ', '.join('%d/%d @ tick %d' % (n, d, t) for t, n, d in M['tsigs'][:8]))
        for (ti, ch), g in grupos.items():
            pr = M['progs'].get((ti, ch))
            ps = [x.pitch for x in g]
            print('  pista %d canal %2d  %-18s programa %-4s notas %5d  rango %d-%d' % (ti, ch + 1, M['names'].get(ti, '')[:18], pr if pr is not None else '-', len(g), min(ps), max(ps)))
        if drum_notes:
            print('  percusion (canal 10): %d notas, instrumentos: %s' % (len(drum_notes), sorted(set(n.pitch for n in drum_notes))))
        return
    if not a.salida:
        sys.exit('falta el archivo de salida (o usa --info)')

    # ---- seleccion de compases -> segmentos (tick inicial, tick final, muestra de salida)
    if a.bars:
        rng = parse_bars(a.bars, len(bars))
        segs = [(bars[x - 1][0], bars[y - 1][0] + bars[y - 1][1]) for x, y in rng]
    else:
        segs = [(0, bars[-1][0] + bars[-1][1])]
    segs2, off = [], 0
    for s, e in segs:
        segs2.append((s, e, off)); off += reloj.smp(e) - reloj.smp(s)
    TOTAL = off
    if a.max_seconds:
        TOTAL = min(TOTAL, int(a.max_seconds * SR))
    starts = [s[0] for s in segs2]
    sal = []                                        # compases de la SALIDA: (tick, longitud, num, den, segmento)
    for sg in segs2:
        for (bt, bl, bn, bd) in bars:
            if sg[0] <= bt < sg[1]:
                sal.append((bt, bl, bn, bd, sg))

    def seg_de(tick):
        i = bisect.bisect_right(starts, tick) - 1
        if i >= 0 and segs2[i][0] <= tick < segs2[i][1]:
            return segs2[i]
        return None

    def a_salida(n):                           # nota -> (st, en) en muestras de salida, o None
        sg = seg_de(n.start)
        if not sg:
            return None
        s0, e0, o0 = sg
        base = reloj.smp(s0)
        st = o0 + reloj.smp(n.start) - base
        en = o0 + reloj.smp(min(n.end, e0)) - base
        if st >= TOTAL:
            return None
        return st, min(en, TOTAL)

    def tick_a_salida(tick):                    # para letra / percusion
        sg = seg_de(tick)
        if not sg:
            return None
        s0, e0, o0 = sg
        return o0 + reloj.smp(tick) - reloj.smp(s0)

    gap = int(a.gap * U)
    gain = a.gain
    vol = lambda v, g=1.0: max(20, min(255, int(round(v * 1.57 * g * gain))))
    canales, informe, melodia_ev = collections.OrderedDict(), [], []
    cand_melodia = []                               # (mediana de altura, eventos) de cada carril melodico
    usados = set()

    def unico(nombre):
        n, k = nombre, 2
        while n in usados:
            n = '%s_%d' % (nombre, k); k += 1
        usados.add(n)
        return n

    def nuevo(nombre):
        nom = unico(nombre)
        canales[nom] = Canal(nom)
        return canales[nom]

    modo = a.mode
    if modo == 'auto':
        modo = 'piano' if len(grupos) <= 1 else 'tracks'
    # ---------------- modo piano
    barstarts = [b[0] for b in bars]
    if modo == 'piano':
        lanes = set(x.strip() for x in a.lanes.split(','))
        por_inicio = collections.OrderedDict()
        for n in mel_notes:
            por_inicio.setdefault(n.start, []).append(n)
        mel, cho, bas = [], [], []
        for t, g in por_inicio.items():
            io = a_salida(g[0])
            if not io:
                continue
            g = sorted(g, key=lambda x: -x.pitch)
            bi = bisect.bisect_right(barstarts, t) - 1
            bt, bl, bn, bd = bars[bi]
            off = t - bt
            fuertes = [0] + ([bl // 2] if bn % 2 == 0 else [])
            fuerte = any(abs(off - f) <= max(1, tpb // 16) for f in fuertes)
            def ev(x):
                ix = a_salida(x)
                return (io[0], ix[1] if ix else io[1], [x.pitch + a.transpose], x.vel)
            top, rest = g[0], g[1:]
            if top.pitch < a.bass_split:
                bas.append((io[0], max(a_salida(x)[1] for x in g), [x.pitch + a.transpose for x in g], max(x.vel for x in g)))
                continue
            mel.append(ev(top))
            if not rest:
                continue
            if fuerte and rest[-1].pitch <= a.bass_split + 2:
                bas.append(ev(rest[-1])); rest = rest[:-1]
            if rest:
                if len(rest) == 1 and rest[0].pitch <= a.bass_split - 3 and not fuerte:
                    bas.append(ev(rest[0]))
                else:
                    rest = rest[:a.max_chord]
                    cho.append((io[0], max(a_salida(x)[1] for x in rest), [x.pitch + a.transpose for x in rest], max(x.vel for x in rest)))
        melodia_ev = mel
        for nombre, evs, onda, g in (('melodia', mel, 'square', 1.0), ('armonia', cho, 'saw', 0.8), ('bajo', bas, 'triangle', 1.1)):
            key = {'melodia': 'melody', 'armonia': 'harmony', 'bajo': 'bass'}[nombre]
            if key in lanes and evs:
                c = nuevo(nombre)
                mono(c, evs, lambda v, g=g: vol(v, g), a.wave or onda, gap)
                informe.append((nombre, 'reduccion de piano (%s)' % key, c))
    # ---------------- modo pistas
    else:
        for (ti, ch), g in grupos.items():
            pr = M['progs'].get((ti, ch))
            fam = (pr // 8) if pr is not None else 0
            base = ident(M['names'].get(ti, '')) if M['names'].get(ti, '').strip() else ''
            nombre = base if base and base not in ('untitled', 'track') else FAMILIAS[fam]
            onda = a.wave or ONDA_FAMILIA[fam]
            ganancia = 1.1 if fam == 4 else 1.0
            # acordes = notas con el mismo inicio y fin
            por = collections.OrderedDict()
            for n in g:
                io = a_salida(n)
                if io:
                    por.setdefault(io, []).append(n)
            evs = []
            for (st, en), ns in por.items():
                ns = sorted(ns, key=lambda x: x.pitch)
                if len(ns) > a.max_chord:
                    ns = ns[:1] + ns[-(a.max_chord - 1):]
                evs.append((st, en, [x.pitch + a.transpose for x in ns], max(x.vel for x in ns)))
            evs.sort()
            carriles, ult = [], []
            descartadas = 0
            for e in evs:
                for i, u_ in enumerate(ult):
                    if u_ <= e[0]:
                        carriles[i].append(e); ult[i] = e[1]; break
                else:
                    if len(carriles) < a.max_lanes:
                        carriles.append([e]); ult.append(e[1])
                    else:
                        descartadas += 1
            for k, lane in enumerate(carriles):
                c = nuevo(nombre)
                if fam not in (4, 5, 11) and len(lane) >= 8:
                    ps_ = sorted(max(e[2]) for e in lane)
                    cand_melodia.append((ps_[len(ps_) // 2], lane))
                mono(c, lane, lambda v, g=ganancia: vol(v, g), onda, gap)
                informe.append((c.nombre, 'pista %d canal %d, programa %s%s' % (ti, ch + 1, pr if pr is not None else '-', '' if not descartadas or k else ', %d notas descartadas por exceso de polifonia' % descartadas), c))
        if cand_melodia:
            melodia_ev = max(cand_melodia, key=lambda x: x[0])[1]          # el carril melodico mas agudo

    # ---------------- percusion
    modo_dr = a.drums
    if modo_dr == 'auto':
        modo_dr = 'midi' if drum_notes else 'none'
    if modo_dr == 'midi' and drum_notes:
        por_tipo = collections.OrderedDict()
        for n in drum_notes:
            io = a_salida(n)
            if not io:
                continue
            nom, inst, f, d = DRUMS.get(n.pitch, DRUM_OTROS)
            por_tipo.setdefault((nom, inst, f, d), []).append((io[0], n.vel, n.pitch))
        for (nom, inst, f, d), evs in por_tipo.items():
            c = nuevo(nom)
            evs.sort()
            for i, (st, v, p) in enumerate(evs):
                nxt = evs[i + 1][0] if i + 1 < len(evs) else TOTAL
                dur = min(int(d * U), nxt - st, TOTAL - st)
                freq = f if f else 60 + (p - 41) * 6 + 40
                c.nota(st, st + dur, [float(freq)], vol(v, 1.1), inst, 0)
            informe.append((nom, 'percusion MIDI (%s)' % inst, c))
    elif modo_dr == 'march':
        kick, snare, hat, crash = nuevo('bombo'), nuevo('caja'), nuevo('charles'), nuevo('platillo')
        ev = {'k': [], 's': [], 'h': []}
        for ob, (bt, bl, bn, bd, (s0, e0, o0)) in enumerate(sal):
            if ob + 1 < a.drum_from:
                continue
            bticks = tpb * 4 / bd
            medio = bn // 2 if bn % 2 == 0 else None
            en_salida = lambda tick: o0 + reloj.smp(int(round(tick))) - reloj.smp(s0)
            for k in range(bn):
                fuerte = (k == 0) or (medio is not None and k == medio)
                ev['k' if fuerte else 's'].append(en_salida(bt + k * bticks))
                ev['h'].append(en_salida(bt + k * bticks)); ev['h'].append(en_salida(bt + (k + 0.5) * bticks))
        for c, key, inst, f, v, d in ((kick, 'k', 'kick', 60, 200, 1.0), (snare, 's', 'snare', 190, 150, 0.75), (hat, 'h', 'hihat', 200, 60, 0.4)):
            L = sorted(set(x for x in ev[key] if x < TOTAL))
            for i, st in enumerate(L):
                nxt = L[i + 1] if i + 1 < len(L) else TOTAL
                c.nota(st, min(st + int(d * U), nxt, TOTAL), [float(f)], v, inst, 0)
            informe.append((c.nombre, 'percusion de marcha', c))
        if sal and a.drum_from <= len(sal):
            t0 = sal[a.drum_from - 1][4][2] + reloj.smp(sal[a.drum_from - 1][0]) - reloj.smp(sal[a.drum_from - 1][4][0])
            crash.nota(t0, min(t0 + 4 * int(U * 4), TOTAL), [300.0], 100, 'crash', 0)
        informe.append((crash.nombre, 'percusion de marcha (platillo al entrar)', crash))

    # ---------------- letra cantada
    if a.lyrics:
        lo, hi = [float(x) for x in a.sing_range.split('-')]
        def fold(f):
            while f >= hi: f /= 2
            while f < lo: f *= 2
            return f
        me = sorted(melodia_ev, key=lambda e: e[0])
        if not me:
            sys.exit('no hay melodia de la que sacar las alturas para la letra')
        def pitch_en(t):
            nxt = None
            for st, en, ps, v in me:
                if st <= t < en: return hz(max(ps))
                if st > t and nxt is None and st - t < U * 2: nxt = hz(max(ps))
            if nxt: return nxt
            prev = [e for e in me if e[0] <= t]
            return hz(max(prev[-1][2])) if prev else 330.0
        voz = nuevo('voz'); nvoz = 0
        for ln, linea in enumerate(open(a.lyrics, encoding='utf-8'), 1):
            linea = linea.strip()
            if not linea or linea.startswith('#'):
                continue
            m = re.match(r'^(\d+)\s*:\s*(.*)$', linea)
            if not m:
                sys.exit('letra, linea %d: formato "COMPAS: palabras"' % ln)
            nb = int(m.group(1))
            if nb < 1 or nb > len(sal):
                sys.exit('letra, linea %d: el compas %d no existe en la salida (%d compases)' % (ln, nb, len(sal)))
            bt, bl, bn, bd, (s0, e0, o0) = sal[nb - 1]
            bticks = tpb * 4 / bd
            pos = 0.0
            for tk in m.group(2).split():
                w, _, b = tk.partition(':')
                w = re.sub(r'[-$#]', '', w) or '_'                   # esos caracteres romperian el formato
                b = float(b) if b else 1.0
                t0 = int(round(bt + pos * bticks)); t1 = int(round(bt + (pos + b) * bticks))
                if w != '_' and t0 < e0:
                    st = o0 + reloj.smp(t0) - reloj.smp(s0); en = o0 + reloj.smp(min(t1, e0)) - reloj.smp(s0)
                    if voz.nota(st, min(en, TOTAL), [fold(pitch_en(st))], 235, 'voz_%s_%s' % (a.lyrics_lang, w), gap):
                        nvoz += 1
                pos += b
        informe.append(('voz', 'letra (%d silabas, voz CTTS)' % nvoz, voz))

    for c in canales.values():
        c.cerrar(TOTAL)

    # ---------------- escribir
    dur_s = TOTAL / SR
    h = ['# %s  <-  %s' % (os.path.basename(a.salida), os.path.basename(a.midi)),
         '# Generado por midi2rbsong.py | modo: %s | %d canales | %d compases de salida | %.3f s (%d muestras a 44100 Hz)' % (modo, len(canales), len(sal), dur_s, TOTAL),
         '# Tempo: ' + (('%d BPM fijo' % a.tempo) if a.tempo else ', '.join('%d BPM' % round(60e6 / us * a.speed) for t, us in M['tempos'][:4])) + ' | compas: ' + ', '.join('%d/%d' % (n, d) for t, n, d in M['tsigs'][:3]) + (' | compases usados: ' + a.bars if a.bars else ''),
         '# Unidad de tiempo = 0.25 s; cada nota deja %s unidades de silencio al final para articular.' % fmt(a.gap),
         '#', '# Canales:']
    for nom, desc, c in informe:
        h.append('#   %-10s %5d notas  <- %s' % (nom, c.notas, desc))
    out = h + ['']
    for c in canales.values():
        out.append('def ' + c.nombre); out += c.lines; out += ['end', '']
    open(a.salida, 'w', encoding='utf-8').write('\n'.join(out))
    print('%s: %d canales, %.3f s, %d notas -> %s' % (os.path.basename(a.midi), len(canales), dur_s, sum(c.notas for c in canales.values()), a.salida))
    for nom, desc, c in informe:
        print('  %-10s %5d notas  %s' % (nom, c.notas, desc))


if __name__ == '__main__':
    try:
        main()
    except BrokenPipeError:
        sys.exit(0)
