# BUNKER.WAD - nivel propio para Doom 1

Un nivel (reemplaza E1M1) hecho con `mkbunker.py`: sala de inicio, puerta, sala con foso de nukage y pasarela,
sala de la llave azul, patio al aire libre, puerta azul, salida con interruptor y un cuarto secreto.
No lleva texturas ni sonidos propios ni de id: usa los del IWAD que cargues (`DOOM.WAD`).

## Jugar
    ./doom_game DOOM.WAD BUNKER.WAD        # o:  ./doom_game DOOM.WAD -file BUNKER.WAD
    gzdoom -iwad DOOM.WAD -file BUNKER.WAD # GZDoom / Chocolate Doom / PrBoom cargan el mismo archivo
Nuevo juego -> episodio 1 empieza directo en el bunker. Al salir, sigue E1M2 del IWAD.

## Regenerar / editar
    sudo apt install zdbsp   # o el constructor de nodos que prefieras
    python3 mkbunker.py BUNKER.WAD
Los sectores son rectangulos en `mkbunker.py` (tabla `sec(...)`), las cosas estan en `THINGS`.
El WAD sale en formato Doom clasico, asi que tambien se abre en Ultimate Doom Builder / GZDoom Builder para retocarlo.
