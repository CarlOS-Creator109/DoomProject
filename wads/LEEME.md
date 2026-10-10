# BUNKER.WAD - niveles propios para Doom 1

Dos niveles (reemplazan E1M1 y E1M2) hechos con `mkbunker.py`. No llevan texturas ni sonidos propios ni de id: usan los del IWAD que cargues (`DOOM.WAD`).

- **E1M1 «Bunker»** - prueba de resistencia: sala de inicio, sala con foso de nukage y pasarela, llave azul, patio al aire libre, puerta azul, salida con interruptor y un cuarto secreto. Los enemigos cambian con la dificultad (hasta 30 a la vez en el nivel más alto).
- **E1M2 «Azotea»** - un respiro: patio abierto con pilares y un charco de nukage, cuarto oscuro con la llave roja, puerta roja y la sala de la salida, más botiquines y un cuarto secreto con armadura azul y chaingun.

## Jugar
    ./doom_game DOOM.WAD BUNKER.WAD        # o:  ./doom_game DOOM.WAD -file BUNKER.WAD
Nuevo juego -> episodio 1 empieza directo en el bunker. Al salir de E1M2 **este motor termina el juego** y muestra el texto final del WAD (lumps `BUNKEND` y `BUNKTXT`). 
Con GZDoom / Chocolate Doom / PrBoom el WAD también carga (`gzdoom -iwad DOOM.WAD -file BUNKER.WAD`), pero esos programas ignoran `BUNKEND`: tras E1M2 siguen con E1M3 del IWAD.

## Regenerar / editar
    sudo apt install zdbsp   # o el constructor de nodos que prefieras
    python3 mkbunker.py BUNKER.WAD
Cada mapa es una tabla de rectángulos (`sec(...)`) más su lista `THINGS` en `mkbunker.py`. El WAD sale en formato Doom clásico, así que también se abre en Ultimate Doom Builder / GZDoom Builder para retocarlo.
