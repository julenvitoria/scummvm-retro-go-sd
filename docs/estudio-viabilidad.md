# Estudio de viabilidad: ScummVM como core de Retro-Go SD

Fecha: 2026-11-07. ScummVM `24e1cad` (rama master, "2026.4.1git"), solo el motor SCUMM
v0–v6 (sin HE ni v7/v8). Juego de prueba: *The Secret of Monkey Island*, EGA, DOS,
español (SCUMM v4, ~2,7 MB de datos).

Las cifras de memoria se tomaron en el PC (backend `null` de ScummVM con capturas y
entrada programada, ver `tools/host/`), contando cada `malloc`/`new` vivo. Las de código,
compilando para Cortex-M7 (`-Os`, Thumb-2, `-ffunction-sections`) y enlazando con
`--gc-sections`.

## 1. Memoria mientras se juega

ScummVM completo tiene unos **2,1 MB** vivos en plena partida (dos escenas, arrancando con `-b 1` y `-b 3`).
Repartido:

| Qué | Bytes | ¿Hace falta en la consola? |
|---|---:|---|
| GUI de ScummVM (lanzador, temas, menú del motor `ScummMenuDialog`) | ~880 K | **No**: el menú es el de Retro-Go |
| Configuración, traducciones, keymaps, resto del framework | ~200 K | Casi nada |
| Objeto `ScummEngine` (una sola reserva de ~196 K + ~42 K) | 238 K | Sí (recortable) |
| Superficies de `setupScumm` (`_textSurface`, `_compositeBuf`) | 129 K | Parcial: la composición puede ir al framebuffer del LCD |
| Pantallas virtuales + buffers de fondo | 128–150 K | Sí |
| Recursos del juego (salas, scripts, costumes…) | 128–150 K | **Pueden leerse desde la flash** (ver §3) |
| `init()`, arrays, tablas, opcodes | ~100 K | Sí |
| Emulador AdLib (OPL de MAME) | 179 K | Opcional: casi todo son tablas que pueden ir a flash, o usar el altavoz de PC |
| iMuse / sonido / mezclador | 27–68 K | Sí |
| Copia de pantalla del backend | 64 K | No: se dibuja directo al LCD |

**Lo que necesita el motor para jugar: ~0,75–1,0 MB**, según la música y cuánto se deje
de recursos en RAM. ScummVM además limita su caché de recursos a 400–550 KB en estos
juegos (`setHeapThreshold`), y la purga sola.

**Lo que hay en la consola para datos** (cifras del CLAUDE.md de la plantilla de sylver):

| Zona | Tamaño | Uso previsto |
|---|---:|---|
| RAM principal (RAM_EMU) | 724 K | código caliente + objeto del motor + pantallas |
| Bonus del modo LUT8 (256 colores) | ~150 K | pantallas virtuales / recursos (SCUMM ≤ v6 es de 256 colores) |
| DTCM | ~104 K | estado caliente del intérprete |
| AHB (con `free`) | ~56 K | reservas pequeñas que se liberan |
| **Total** | **~1,03 MB** | |

Cabe, pero sin margen si se copia ScummVM tal cual: hay que quitar la GUI, dibujar en el
framebuffer, dejar los recursos y las tablas del OPL en la flash, y darle a ScummVM un
gestor de memoria de verdad (el `operator new` del SDK no libera nunca, y ScummVM
reserva y libera constantemente).

## 2. Tamaño del código

ScummVM completo con el motor SCUMM, para Cortex-M7: **2,86 MB** (1,93 MB de código +
0,93 MB de datos de solo lectura). Por partes (`tools/host/mapsize.py`):

| Parte | Bytes | Nota |
|---|---:|---|
| `engines/scumm` | 836 K | incluye GUI de Mac (79 K), NES (31 K), HE (29 K) y reproductores de otras plataformas |
| `graphics` | 552 K | 254 K son escaladores y 95 K el renderizador de la GUI: fuera |
| Cadenas de texto (fusionadas) | 544 K | mensajes de error/aviso y textos de la GUI: gran parte se puede quitar |
| `gui` | 303 K | fuera casi entero |
| `audio` | 186 K | OPL, iMuse, mezclador, resampler |
| libc / libgcc / libstdc++ | 166 K | |
| `common`, `base`, `image`, motor base | ~240 K | |

Para un core solo de LucasArts sin la GUI, la estimación es **~1,3–1,6 MB**. No cabe en
los 724 K de RAM, así que va como planteaste: **lo esencial en RAM/ITCM y el resto
ejecutándose desde la flash externa**.

## 3. Lo que ya ofrece el firmware (SDK de sylver)

- `odroid_overlay_cache_file_in_flash()` copia un archivo de la SD a la flash externa y
  devuelve su dirección mapeada (`0x90000000…`). Sirve para los datos del juego: se copian
  una vez y ScummVM los lee en sitio. Los `.LEC` de Monkey vienen cifrados con XOR 0x69;
  se pueden descifrar al copiarlos (variante `_relocate`, con callback por bloque).
- La variante `_relocate` también sirve para el **código frío**: se enlaza para una
  dirección base y el callback aplica las reubicaciones al grabarlo, porque la dirección
  final la elige el firmware.
- `store_data_in_flash` / `lookup_data_in_flash` guardan blobs derivados con una clave
  (por ejemplo las tablas del OPL ya calculadas).
- `flash_cache_usable_size()` dice cuánto hay libre.
- Partidas: archivos en la SD por FatFs (`/saves/scummvm/…`).

**Avisos del propio SDK:** mientras el firmware graba en la flash, el mapeo XIP se
desactiva, así que cualquier callback que se ejecute durante la grabación tiene que estar
en RAM. Y hay que comprobar en la consola que la MPU del firmware permite **ejecutar**
desde la flash externa (DosAT lee datos de ahí, no ejecuta código ARM).

## 4. Conclusión

**Viable**, con el reparto que propusiste:

1. **Un core por familia** (empezando por LucasArts/SCUMM), cada uno su `.bin` con su
   carpeta de juegos.
2. **En RAM/ITCM**: el bucle principal, el intérprete de scripts, el dibujado y la
   descompresión de gráficos, el mezclador. **En la flash externa**: el resto del código,
   los datos del juego, las tablas del OPL.
3. **Sin la GUI de ScummVM**: menú y opciones con el de Retro-Go.
4. **Con un gestor de memoria propio** que sí libere.

Riesgos por orden: ejecutar desde la flash (MPU y velocidad), cuánto código hay que dejar
en RAM para que vaya fluido (solo se puede medir en la consola), y la licencia (ScummVM es
GPLv3, el core también tendrá que serlo).

## 5. Siguiente paso propuesto

Un primer core mínimo que arranque Monkey Island en la consola, todavía sin sonido ni
partidas: backend propio de ScummVM para Retro-Go (pantalla LUT8, botones, tiempo), datos
del juego en la flash y código frío en la flash. Ahí se mide lo que no se puede medir en
el PC: velocidad y si la ejecución desde la flash funciona.
