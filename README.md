# ScummVM para Game & Watch (Retro-Go SD)

Core de [ScummVM](https://github.com/scummvm/scummvm) para la Game & Watch con el
firmware Retro-Go SD de sylverb (rama `external_cores`). Empieza por el motor SCUMM
(aventuras de LucasArts, versiones 0 a 6).

**Estado: primera versión de prueba.** Arranca y se juega *The Secret of Monkey Island*
(EGA), con música y efectos (AdLib / Sound Blaster FM, emulador OPL de DOSBox). Todavía
**sin partidas guardadas**. El estudio previo está en
[docs/estudio-viabilidad.md](docs/estudio-viabilidad.md).

Los juegos no se incluyen ni se incluirán: cada uno pone los suyos.

## Instalación

1. Copia `scummvm.bin` en `/cores/` de la SD.
2. Los ficheros de cada juego van, tal cual (`000.lfl`, `901.lfl`, `disk01.lec`...), en su
   propia carpeta dentro de `/scummvm/` (fuera de `/roms`, para que no salga en el menú).
3. En `/roms/scummvm/` va un fichero `.scummvm` por juego; su nombre es el que aparece en el
   menú y dentro lleva el nombre de la carpeta del juego:

   ```
   /roms/scummvm/The Secret of Monkey Island.scummvm    (contiene: monkey)
   /scummvm/monkey/000.lfl
   /scummvm/monkey/901.lfl
   ...
   ```

   Si el `.scummvm` está vacío se usa la carpeta `/scummvm/<nombre del fichero>`. También vale
   una ruta completa (`/juegos/monkey`). La disposición de la primera versión (el `.scummvm`
   dentro de la carpeta del juego) sigue funcionando.
4. La primera vez que se abre un juego, sus ficheros se copian a la flash externa (barras de
   progreso); las siguientes veces arranca directamente.

Si un juego vuelve al menú de Retro-Go sin más, el motivo queda en
`/saves/scummvm/scummvm.log`.

## Controles

| Botón | Acción |
|---|---|
| Cruceta | mover el puntero (acelera al mantener) |
| B | clic izquierdo |
| A | clic derecho |
| GAME (START) | Esc: saltar escena |
| TIME (SELECT) | `.` + Enter: saltar frase / "pulsa ENTER para continuar" |
| PAUSE/SET | menú de Retro-Go |

## Cómo está hecho

- Lo esencial (arranque, acceso a la SD, el heap) va en la RAM del core; el resto de ScummVM
  (~2,5 MB de código) se ejecuta directamente desde la flash externa (XIP). Al arrancar, el
  core se copia a la flash y se reubica a la dirección donde quedó (`tools/xip_pack.py`,
  `src/boot.c`).
- Los ficheros del juego se leen desde la flash externa, sin copiarlos a RAM.
- El heap usa toda la RAM libre que deja el firmware (~885 KB en tres zonas, `src/heap.c`).
- `patches/` contiene cambios a ScummVM que se aplican al compilar (buffers de las versiones
  Macintosh y NES recortados: esas versiones no están soportadas).

## Compilar

```
git submodule update --init
make            # out/scummvm.bin (arm-none-eabi-gcc en el PATH o GCC_PATH=...)
make host       # versión de PC para pruebas: build/host/scummvm_host <carpeta del juego>
```

`tools/host/` tiene las herramientas de medición de memoria en el PC
([tools/host/README.md](tools/host/README.md)).
