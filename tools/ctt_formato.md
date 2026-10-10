# El archivo de voz `.ctt` de CTTS

Un `.ctt` es un archivo de texto plano donde cambias **cómo suena CTTS** (tono, velocidad, timbre, pausas, consonantes, vibrato…) **sin recompilar**. Sirve para el habla (`ctts "texto" salida.wav`) y para la voz cantada de las canciones `.rbsong`.

> Esta guía se generó a partir del propio motor (`ctts --dump-ctt`) y las reglas de abajo están comprobadas con él. Versión del motor: la de `ctts.c` entregado con esta guía.

---

## 1. Uso rápido

```bash
gcc -O2 -o ctts ctts.c -lm

./ctts --dump-ctt > voz.ctt                        # genera un .ctt con TODOS los valores actuales (plantilla)
./ctts -c voz.ctt "hola mundo" salida.wav           # habla con tu archivo
./ctts -c voz.ctt -f texto.txt salida.wav           # lee un texto largo
./ctts -c voz.ctt -s cancion.rbsong salida.wav      # canción (la voz cantada usa tu .ctt)
./ctts -c voz.ctt -s cancion.rbsong s.wav --no-int  # solo los canales de voz
```

Flujo recomendado: genera la plantilla con `--dump-ctt`, **borra todo lo que no quieras cambiar** y deja solo tus líneas. Lo que no pongas queda con su valor por defecto.

## 2. Sintaxis

Una línea por parámetro:

```
nombre = valor      # comentario
```

Reglas (todas comprobadas):

- `#` empieza un comentario (en cualquier parte de la línea). Las líneas vacías se ignoran.
- Se acepta `=` o `:` como separador: `pitch = 140` y `pitch: 140` son lo mismo.
- Decimales con **punto o coma**: `0.9` y `0,9`.
- Los **nombres no distinguen mayúsculas** (`Pitch`, `PITCH` y `pitch` valen).
- Además del nombre en español puedes usar la **clave interna** de la tabla (por ejemplo `F0 = 130` equivale a `pitch = 130`).
- Si un nombre no existe, CTTS avisa con el número de línea (`archivo.ctt:2: parametro desconocido 'f0x'`) y **sigue** con el resto.
- Si una línea no tiene `=` ni `:`, avisa `falta '='` y la salta.
- No hay validación de rangos: un valor absurdo se acepta tal cual (y puede sonar mal o no sonar).

## 3. Orden de prioridad (quién manda cuando hay varios valores)

**Habla:** valor por defecto  <  `.ctt`  <  variable de entorno  <  argumentos `velocidad` y `f0` de la línea de comandos.

**Canciones `.rbsong`:** valor por defecto  <  directiva `#!` dentro de la canción  <  `.ctt` (`-c`)  <  variable de entorno.

- **Variable de entorno:** se usa la clave interna. Ejemplo: `FSCALE=1.1 ./ctts -c voz.ctt ...` gana sobre `formantes = 1.2` del archivo (comprobado). Útil para probar sin editar el archivo.
- **Línea de comandos (habla):** `./ctts -c voz.ctt "hola" s.wav 0.5 110` fuerza velocidad 0.5 y tono 110 Hz aunque el `.ctt` diga otra cosa (comprobado: con `velocidad = 2.0` en el archivo la frase dura 0.55 s; con `0.5` por línea de comandos, 1.93 s).

## 4. Parámetros

"Aplica a": los de **habla** (tono, entonación, pausas, volumen) no hacen nada en una canción, porque ahí la nota ya trae su frecuencia y duración. Los demás (timbre, consonantes, nasales…) valen para el habla y para la voz cantada de CTTS.

### Voz y entonación (pensados para habla)

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `pitch` | `F0` | 118 | tono base en Hz (hombre ~100-120, mujer ~190-220) |
| `velocidad` | `SPEED` | 1.05 | velocidad del habla (1.0 normal) |
| `volumen` | `VOL` | 0.85 | volumen de salida 0..1 |
| `declinacion` | `F0DECL` | 0.14 | cuanto baja el tono a lo largo de la frase (0..0.3) |
| `acento_tono` | `ACCF` | 1.1 | subida de tono en la silaba acentuada |
| `atona_tono` | `UNSF` | 0.97 | tono relativo de silabas sin acento |
| `pregunta_subida` | `QRISE` | 0.3 | subida de tono al final de una pregunta |
| `final_bajada` | `FALL` | 0.1 | bajada de tono al final de una afirmacion |
| `jitter` | `JITTER` | 0.004 | variacion aleatoria del tono (voz menos robotica) |

### Timbre

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `formantes` | `FSCALE` | 1 | escala de formantes (1.0 neutral, mayor = mas agudo/chico) |
| `respiracion` | `NOISEV` | 0.0008 | soplo durante las vocales |
| `pendiente` | `TILT` | 1 | filtro pasa-bajos de la voz (1.0 = sin filtro, menor = mas oscuro) |
| `h_volumen` | `HAMP` | 2 | volumen de la h aspirada (ingles, etc.) |

### Duración y ritmo

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `acento_duracion` | `STRESSD` | 1.5 | alargamiento de la vocal acentuada |
| `vocal_duracion` | `VDUR` | 1 | multiplicador de duracion de vocales |
| `consonante_duracion` | `CDUR` | 1 | multiplicador de duracion de consonantes |
| `semiconsonante_duracion` | `GLD` | 1 | duracion de i/u semiconsonantes (ie, ua...) |
| `semiconsonante_volumen` | `GLAV` | 0.8 | volumen de semiconsonantes |
| `transicion` | `T_TR` | 0.02 | segundos de transicion entre sonidos |
| `pausa_coma` | `PCOMA` | 0.22 | segundos de pausa en coma |
| `pausa_punto` | `PPUNTO` | 0.4 | segundos de pausa en punto |
| `cola` | `TAIL` | 0.1 | segundos de silencio al final |
| `inicio` | `LEAD` | 0 | segundos de silencio al inicio |

### Consonantes oclusivas y fricativas

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `rafaga` | `BURST` | 0.6 | fuerza de la rafaga de p t k b d g |
| `aspiracion` | `ASP` | 1 | aspiracion despues de p t k |
| `vot` | `VOTS` | 1 | duracion de la aspiracion (VOT) |
| `cierre` | `CLOS` | 1 | duracion del cierre de oclusivas |
| `friccion` | `FRICG` | 1 | volumen de s f j (fricativas) |
| `aprox_volumen` | `APXAV` | 0.6 | volumen de b d g suaves entre vocales |

### Nasales

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `nasal_vocal` | `NVN` | 0.7 | nasalizacion de vocales junto a m/n |
| `nasal_volumen` | `NASAV` | 0.75 | volumen de m n ni |
| `nasal_f1` | `NF1` | 250 | primer formante de las nasales (Hz) |
| `nasal_ancho` | `NASB1` | 160 | ancho de banda de F1 nasal |
| `nasal_b2` | `NB2` | 150 | ancho de banda de F2 nasal |
| `nasal_b3` | `NB3` | 200 | ancho de banda de F3 nasal |
| `nasal_cero` | `NZA` | 1 | cuanto actua el antiresonador nasal 0..1 |
| `nasal_cero_ganancia` | `NZG` | 0.8 | ganancia del antiresonador nasal |
| `nasal_f2` | `NF2S` | 1 | escala del F2 de las nasales |

### Laterales y vibrantes (l, r, rr)

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `lateral_f1` | `LF1` | 1 | escala del F1 de la l |
| `lateral_f2` | `LF2S` | 1 | escala del F2 de la l |
| `lateral_volumen` | `LAV` | 0.75 | volumen de la l |
| `lateral_ancho` | `LB1` | 80 | ancho de banda de F1 en la l |
| `tap_volumen` | `TAPAV` | 0.15 | volumen durante la r simple (0 = cierre total) |
| `tap_duracion` | `TAPD` | 1 | duracion de la r simple |
| `trino_duracion` | `TRD` | 1 | duracion de la rr |
| `trino_golpes` | `TRN` | 3 | numero de golpes de la rr |
| `trino_profundidad` | `TRF` | 0.12 | volumen entre golpes de la rr (0 = corte total) |

### Canciones .rbsong

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `muestreo` | `SRATE` | 0 | frecuencia de muestreo en Hz (0 = automatico: 22050 habla, 44100 canciones) |
| `base` | `BASE` | 0.25 | rbsong: duracion base de una unidad $1, en segundos |
| `voz_volumen` | `VOZG` | 1 | rbsong: ganancia de la voz CTTS (calibrada contra la de GemTunes) |
| `sin_instrumentos` | `SININT` | 0 | rbsong: 1 = solo voz (omite los canales sin notas voz_; igual que --no-int) |
| `voz_motor` | `VOZM` | 0 | rbsong: voz cantada 0 = CTTS (mas inteligible), 1 = GemTunes (identica al motor Ruby) |
| `vibrato_prof` | `VIBD` | 0.006 | rbsong: profundidad del vibrato de la voz (0 = sin vibrato) |
| `vibrato_vel` | `VIBR` | 5.5 | rbsong: velocidad del vibrato en Hz |

### Otros idiomas (IPA, solo en notas voz_<idioma> distintas de es)

| Nombre | Clave interna | Por defecto | Qué hace |
|---|---|---|---|
| `palatalizacion` | `IPAP` | 1 | IPA: consonante suave (ʲ) antes de vocal suma un deslizamiento j (ruso: бю, ня...) |
| `ruso_y` | `IPAY` | 1 | IPA ruso: la ы (espeak la escribe y) se canta como vocal central, no como i |


Notas sobre algunos parámetros:

- `pitch`: referencia de tono masculino ~100–120 Hz, femenino ~190–220 Hz. Con 118 la voz es neutral.
- `formantes`: escala todas las resonancias de la boca. **1.0 = neutral; menor = voz más grave y grande; mayor = más aguda y chica.** Valores que usamos en la saga: agente 0.85, director 0.8, locutora 1.18, niña 1.4. Medido con Whisper: a 90 Hz, por debajo de ~0.8 la inteligibilidad cae (0.75 y 0.68 salieron claramente peor que 0.85); por encima de 1.45 no lo probé.
- `velocidad`: 1.0 = normal; 2.0 es el doble de rápida. (El valor por defecto del motor es 1.05.)
- `muestreo`: `0` = automático (22050 Hz para habla, 44100 Hz para canciones). Si pones un valor, aplica a ambos (comprobado con 44100).
- `voz_motor`: `0` voz de CTTS (más inteligible); `1` la voz de GemTunes, idéntica muestra por muestra al motor Ruby.
- `voz_volumen`: ganancia de la voz CTTS en canciones (1.0 equivale en nivel a la voz de GemTunes; en la saga usamos 1.25–1.4).
- `sin_instrumentos = 1`: igual que `--no-int` (descarta los canales sin notas `voz_`).
- `palatalizacion` y `ruso_y` solo actúan cuando una nota cantada usa un idioma distinto del español (`voz_ru_…`, `voz_en_…`), porque ahí los fonemas llegan por IPA desde `espeak-ng`.
- `tap_*` y `trino_*` controlan la `r` simple y la `rr`.

Fuera de `formantes` y `pitch`, los rangos **no están validados**: cambia de poco en poco (±20 %) y escucha.

## 5. Fonemas: `fonema.X = F1 F2 F3 [duración]`

Puedes redefinir los formantes (en Hz) y, opcionalmente, la duración (en segundos) de un fonema de la tabla:

```
fonema.a = 800 1400 2500 0.12      # una "a" un poco más cerrada y larga
fonema.N = 250 2000 2800 0.09      # la ñ
```

- Se escribe `fonema.` seguido de **un solo carácter**.
- **El carácter distingue mayúsculas de minúsculas**: `fonema.n` (n) y `fonema.N` (ñ) son fonemas distintos. *(Corrección: una versión anterior del motor convertía todo a minúsculas y `fonema.N` cambiaba la `n` por error; ya está arreglado en el `ctts.c` entregado.)*
- Si faltan números o el símbolo no existe, avisa `fonema invalido` y sigue.
- Aquí solo aparecen los fonemas vocálicos y sonorantes (los que lista `--dump-ctt`). Las fricativas y oclusivas (s, f, j, p, t, k, b, d, g, ch…) se ajustan con los parámetros de la sección 4 (`rafaga`, `friccion`, …).

| Símbolo | Sonido | F1 | F2 | F3 | Duración (s) |
|---|---|---|---|---|---|
| `a` | a | 840 | 1410 | 2550 | 0.105 |
| `e` | e | 504 | 1900 | 2540 | 0.100 |
| `i` | i | 290 | 2270 | 2840 | 0.095 |
| `o` | o | 552 | 870 | 2660 | 0.105 |
| `u` | u | 330 | 830 | 2350 | 0.095 |
| `j` | i/u semiconsonante: i (ie, ia) | 290 | 2270 | 2840 | 0.050 |
| `w` | u semiconsonante (ua, ue) | 330 | 830 | 2350 | 0.050 |
| `m` | m | 250 | 1100 | 2200 | 0.075 |
| `n` | n | 250 | 1500 | 2500 | 0.070 |
| `N` | ñ | 250 | 2100 | 2800 | 0.085 |
| `l` | l | 360 | 1150 | 2750 | 0.070 |
| `I` | ɪ (vocal breve, inglés) | 390 | 1990 | 2550 | 0.080 |
| `E` | ɛ | 530 | 1840 | 2480 | 0.095 |
| `A` | æ | 660 | 1720 | 2410 | 0.115 |
| `P` | ɑ | 730 | 1090 | 2440 | 0.125 |
| `Q` | ɒ | 590 | 880 | 2540 | 0.105 |
| `O` | ɔ | 570 | 840 | 2410 | 0.115 |
| `U` | ʊ | 440 | 1020 | 2240 | 0.085 |
| `V` | ʌ | 640 | 1190 | 2390 | 0.100 |
| `@` | ə (schwa) | 500 | 1500 | 2500 | 0.070 |
| `3` | ɜ | 490 | 1350 | 1690 | 0.120 |
| `1` | ɨ (ы rusa) | 330 | 1550 | 2350 | 0.090 |
| `M` | ŋ | 250 | 1900 | 2500 | 0.085 |
| `L` | ɹ (r inglesa) | 350 | 1300 | 1650 | 0.075 |

## 6. Directivas dentro de una canción (`#!`)

Dentro de un `.rbsong` puedes escribir una línea que empieza con `#!` y lleva exactamente lo mismo que un `.ctt`:

```
#! voz_volumen = 1.4
#! agente.formantes = 0.85
```

Para GemTunes (Ruby) es solo un comentario (empieza con `#`), así que **la canción sigue siendo compatible con los dos motores**; CTTS sí la lee. Prioridad: ver sección 3 (un `.ctt` o una variable de entorno pueden pisar la directiva).

### Timbre por canal

`canal.formantes = X` cambia el timbre de **todas las notas de voz CTTS de ese canal** (multiplica a `formantes`). El nombre del canal es el del `def`. Se puede escribir **en un `.ctt`** (`agente.formantes = 0.85`) o **como directiva dentro de la canción** (`#! agente.formantes = 0.85`); si están las dos, **manda el `.ctt`**. Ejemplo como directiva:

```
#! agente.formantes = 0.85     # el def agente: voz más grave
#! locutora.formantes = 1.18   # el def locutora: voz más aguda
```

Es lo que da personalidad distinta a cada personaje. Solo existe la propiedad `formantes` por canal; cualquier otra (`canal.algo`) da aviso y se ignora.

**Recomendado para la saga Hardbass:** poner todo el timbre de los personajes en un `.ctt` (`hardbass.ctt`) y renderizar con `-c hardbass.ctt`, en vez de depender de las directivas de cada canción.

## 7. Recetas

**Voz grave y lenta, muy clara**
```
pitch = 100
velocidad = 0.85
formantes = 0.9
pausa_coma = 0.3
```

**Voz aguda (más infantil)**
```
pitch = 190
formantes = 1.25
velocidad = 1.0
```

**Voz "robot" (monótona, sin vibración)**
```
declinacion = 0
acento_tono = 1.0
atona_tono = 1.0
final_bajada = 0
pregunta_subida = 0
jitter = 0
```

**Habla en 44,1 kHz con más cuerpo**
```
muestreo = 44100
pendiente = 0.8
```

**Canción: voz más presente y vibrato más marcado**
```
voz_volumen = 1.4
vibrato_prof = 0.012
vibrato_vel = 5
```

**Solo las voces de una canción (para ensayar la letra)**
```
sin_instrumentos = 1
```

> Las recetas son puntos de partida razonables, no valores medidos uno por uno: ajústalos a tu gusto.

## 8. Problemas frecuentes

| Síntoma | Causa y arreglo |
|---|---|
| `parametro desconocido 'xxx'` | El nombre no existe. Revisa la sección 4 o genera la plantilla con `--dump-ctt`. |
| `falta '='` | La línea no tiene `=` ni `:`. |
| `fonema invalido` | Faltan los 3 números (F1 F2 F3), o el símbolo no existe en la tabla de la sección 5. |
| Mi cambio no se nota | Mira la prioridad (sección 3): una variable de entorno o un argumento de línea de comandos puede estar pisándolo. |
| Un parámetro "de habla" no hace nada en la canción | Es normal: en canciones el tono y la duración los pone la nota (sección 4). |
| Suena peor tras un cambio | Borra la línea (vuelve al valor por defecto) y cambia de poco en poco. |
| `espeak-ng` no disponible | Para idiomas distintos del español, CTTS usa `espeak-ng`; sin él avisa y usa las reglas del español. |

## 9. Apéndice: variables de entorno

Cualquier parámetro se puede forzar con una variable de entorno con su clave interna (tabla de la sección 4), por ejemplo:

```bash
F0=150 SPEED=0.9 ./ctts "hola" s.wav
VOZG=1.2 ./ctts -s cancion.rbsong s.wav
```
