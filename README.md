# ScummVM for Game & Watch (Retro-Go SD)

[English](#english) · [Español](#español)

---

## English

A [ScummVM](https://github.com/scummvm/scummvm) core for the Nintendo Game & Watch running
sylverb's Retro-Go SD firmware, version 2.0.0-rc1 or later (or its `external_cores` branch).
Version 1.4.1 and earlier cannot load cores from `/cores/`.

No games are included, nor will they be: bring your own.

### Compatibility

✅ tested on the console · 🟡 should work, not tested on the console yet · ❌ not supported

| Game | Engine | Status | Notes |
|---|---|:-:|---|
| Maniac Mansion (PC) | SCUMM | ✅ | |
| Maniac Mansion (NES) | SCUMM | ✅ | |
| Zak McKracken and the Alien Mindbenders | SCUMM | ✅ | |
| Indiana Jones and the Last Crusade | SCUMM | ✅ | |
| Loom | SCUMM | ✅ | |
| The Secret of Monkey Island (EGA, VGA, talkie) | SCUMM | ✅ | |
| Monkey Island 2: LeChuck's Revenge | SCUMM | ✅ | |
| Indiana Jones and the Fate of Atlantis | SCUMM | ✅ | |
| Day of the Tentacle | SCUMM | ✅ | |
| Sam & Max Hit the Road | SCUMM | ✅ | |
| Full Throttle | SCUMM | ✅ | first launch prepares the game (a few minutes) |
| Beneath a Steel Sky | Sky | ✅ | first launch prepares the game (a few minutes) |
| Sołtys | CGE | ✅ | |
| Lure of the Temptress | Lure | ✅ | |
| Broken Sword: The Shadow of the Templars | Sword1 | 🟡 | no cutscene videos; see the notes |
| The Dig | SCUMM | ❌ | too big for the flash |
| The Curse of Monkey Island | SCUMM | ❌ | too big for the memory |
| Macintosh versions | — | ❌ | |
| FM-Towns versions | — | ❌ | |

In development, not in this core yet: Ween: The Prophecy and Gobliiins (no saving from the
Retro-Go menu in these two: their engine has none), Cruise for a Corpse, Operation Stealth.

Open a section:

<details>
<summary><b>Installation</b></summary>

1. Copy `scummvm.bin` to `/cores/` on the SD card.
2. Copy each game's files, as they are (`000.lfl`, `901.lfl`, `disk01.lec`…), to a folder of
   its own inside `/scummvm/` — outside `/roms`, so that it does not show in the menu.
3. Put one `.scummvm` text file per game in `/roms/scummvm/`. Its name is what the menu
   shows; inside it, write the name of the game's folder:

   ```
   /roms/scummvm/The Secret of Monkey Island.scummvm    (contains: monkey)
   /scummvm/monkey/000.lfl
   /scummvm/monkey/901.lfl
   ...
   ```

   If the file is empty, the folder `/scummvm/<file name>` is used. A full path also works
   (`/games/monkey`). Make sure the file does not end in `.txt` (show file extensions on
   Windows).
4. The first time a game is opened its files are copied to the external flash (progress
   bars); after that it starts right away. Files over 24 MB (the speech of talkie versions)
   are read from the SD card instead.

   Games copied to the flash by an older version of this core are copied once more (their
   data files are now stored decrypted, see "How it is made").

</details>

<details>
<summary><b>Notes for each game</b></summary>

**Full Throttle**

Copy the whole game as it is (`FT.LA0`, `FT.LA1`, `MONSTER.SOU` and the `DATA` and `VIDEO`
folders) into its folder. `FT.LA1` (152 MB) does not fit in the flash: the first time, the
core copies what it holds besides the sounds into `FT.LA1.gwpack` next to it (28 MB, so leave
that much free on the card) and puts that in the flash; a progress bar shows it, and it takes
a few minutes (if the bar seems stuck, it is still working). The sounds, voices and videos
are read from the SD card as they play. If the
game's files change, the pack is made again; it can be deleted at any time.

The Dig does not fit: its characters' graphics alone are 57 MB.

**Beneath a Steel Sky**

Copy `sky.dsk`, `sky.dnr` and `sky.cpt` into the game's folder (`sky.cpt` comes with ScummVM,
not with the game). The first time, the core unpacks everything but the speech into
`sky.dsk.gwpack` next to them (38 MB, so leave that much free on the card) and puts it in
the flash; a progress bar shows it, and it takes a few minutes. The speech is read from the
SD card. TIME opens the game's own control panel (save, load, volume, text speed).

**Sołtys**

Copy `vol.cat` and `vol.dat` into the game's folder (the game is freeware: scummvm.org/games).
`vol.dat` is kept decrypted in the flash, and the sprites are drawn from there.

**Lure of the Temptress**

Copy `DISK1.VGA` to `DISK4.VGA` into the game's folder, with ScummVM's `lure.dat` (it comes
with ScummVM, not with the game). Everything goes to the flash; nothing is built on the
first launch.

**Broken Sword: The Shadow of the Templars** (PC CD)

- Copy the CD's `clusters`, `music`, `speech` and `smackshi` folders into the game's folder.
  **The cutscene videos are skipped** (each needs over 1.5 MB of memory), but ScummVM
  recognises the game by `smackshi/intro.smk`: that one file is enough.
- Add `language=` to the `.scummvm` file (`language=es`…), or the subtitles are in English.
- The game is 150 MB and does not fit in the flash whole: the flash holds the common part
  and one or two **zones** (Paris 1, Paris 2, Ireland, Syria…). The first launch copies
  some 50 MB: it takes a while.
- When you travel to a zone that is not in the flash, the game **saves itself and goes back
  to the menu** with a notice (in the Retro-Go menu's language). Start it again: that zone
  is copied (progress bar) and you go on exactly where you were. That next start always
  goes on from there, whether you pick resume or new game; after it, the menus work as usual.
- The progress bar only shows when something has to be copied: loading a save from a zone
  already in the flash starts straight away. Loading one from a zone that was dropped to
  make room for another copies it again, once.
- To start over: *New game* in Retro-Go opens the game's panel when there are saves —
  *Start* begins the game from the beginning, *Restore* loads a save, *Quit* goes back to
  the menu. *Restart* in the in-game panel does the same.
- The game is drawn at half size, as the screen shows it, so nothing is lost.

</details>

<details>
<summary><b>Telling ScummVM which game it is</b></summary>

ScummVM recognises a game by a fingerprint of its files, and knows thousands of versions.
With an unusual copy (a rare release, a fan-made talkie…) it has to guess, and may guess
wrong. You can tell it, with optional lines in the `.scummvm` file, below the folder name:

```
monkey_talkie
game=monkey
platform=pc
language=es
```

| Line | Meaning |
|---|---|
| `game=` | which game it is, by ScummVM's short name (table below). Usually all that is needed. |
| `platform=` | the computer the version was made for: `pc`, `amiga`, `atari`, `fmtowns`, `segacd`… |
| `language=` | `en`, `es`, `fr`, `de`, `it`… |
| `extrapath=` | a subfolder (of the game's folder, or an absolute path) with more of the game's files, such as CD music tracks: `extrapath=cd_music_flac` for the Monkey Island Ultimate Talkie. |

With `game=`, only that game is considered, the way you would add it by hand in ScummVM's
own launcher; `platform=` and `language=` rule out the versions that do not match. Without
these lines nothing changes: ScummVM picks what it finds in the folder.

| Game | `game=` |
|---|---|
| Maniac Mansion | `maniac` |
| Zak McKracken and the Alien Mindbenders | `zak` |
| Indiana Jones and the Last Crusade | `indy3` |
| Loom | `loom` |
| The Secret of Monkey Island | `monkey` |
| Monkey Island 2: LeChuck's Revenge | `monkey2` |
| Indiana Jones and the Fate of Atlantis | `atlantis` |
| Day of the Tentacle | `tentacle` |
| Sam & Max Hit the Road | `samnmax` |
| Full Throttle | `ft` |
| Beneath a Steel Sky | `sky` |
| Sołtys | `soltys` |
| Lure of the Temptress | `lure` |
| Broken Sword | `sword1` |

Every game's short name is in ScummVM's
[compatibility list](https://www.scummvm.org/compatibility/), column "ScummVM ID", without
the engine in front (`scumm:ft` → `game=ft`). The list covers every engine, not only
LucasArts', but this core runs the games above for now: SCUMM (not The Dig nor The Curse of Monkey
Island), Beneath a Steel Sky, Sołtys, Lure of the Temptress and Broken Sword.

They do not fix an incomplete copy, nor the versions this core leaves out on purpose:
Macintosh (its buffers are cut to save memory) and FM-Towns.

</details>

<details>
<summary><b>Saved games</b></summary>

- **Retro-Go menu (PAUSE/SET):** *Save & Continue*, *Save & Quit* and *Reload*, with the
  firmware's 4 slots and their previews, as in the other cores. Powering off saves too, and
  *Resume* in the game list carries on from there. These saves live where the firmware keeps
  every core's.
- **The game's own menu (TIME):** what F5 opens on a PC — save, load, play, quit. Type the
  save's name with the on-screen keyboard (GAME); left empty, it becomes the date and time. These saves are ScummVM's usual files, in `/saves/scummvm/`.
- Saves carry no thumbnail (it would take 128 KB of memory).

</details>

<details>
<summary><b>Controls</b></summary>

| Button | Action |
|---|---|
| D-pad | move the pointer (speeds up while held); it starts in the middle of the screen |
| B | left click |
| A | right click |
| GAME | on-screen keyboard |
| TIME | F5: the game's own menu (save, load, quit) |
| START (Zelda) | Esc: skip cutscene |
| SELECT (Zelda) | `.`: skip the current line of dialogue |
| PAUSE/SET | Retro-Go menu (save, load, options) |

**On-screen keyboard** (GAME): two rows above the picture and two below it, so the game
is never covered. D-pad moves, A types, B or GAME closes it; `Shf` makes the next letter a
capital (press it twice to lock). It has Esc, Enter, Space, Backspace, F1–F10, numbers and
letters — on a Mario unit, Esc (skip cutscene), `.` (skip line) and Enter ("press ENTER to
continue") are typed from here.

**Subtitles** in talkie versions are on. To change it: PAUSE/SET → Options → Subtitles
(voice + text / text only / voice only), or `Txt` on the keyboard (Ctrl+T). The choice is
kept for every game.

</details>

<details>
<summary><b>When a game does not start</b></summary>

If a game goes back to the Retro-Go menu, the reason is shown on screen and written to
`/saves/scummvm/scummvm.log`.

</details>

<details>
<summary><b>How it is made</b></summary>

- The essentials (boot, SD card access, the heap) live in the core's RAM; the rest of
  ScummVM (~2.5 MB of code) runs straight from the external flash (XIP). At boot the core
  is copied to the flash and relocated to wherever it landed (`tools/xip_pack.py`,
  `src/boot.c`).
- Game files are read from the external flash without being copied to RAM. The data files
  of SCUMM v4-v6 games (encrypted with XOR 0x69 on disk) are stored decrypted, so the
  engine uses the rooms, costumes and scripts where they lie instead of loading them into
  the heap — what lets Sam & Max and the other big VGA games fit.
- Speech and CD music in compressed form are decoded by small single-file decoders instead
  of the libraries ScummVM uses (`src/gw_codecs.h`): Ogg Vorbis with stb_vorbis (`.sog`,
  `trackN.ogg`), MP3 with minimp3 (`.so3`, `trackN.mp3`) and FLAC with dr_flac (`.sof`,
  `trackN.flac`). Uncompressed speech (`.sou`) works as in ScummVM.
- The heap takes all the RAM the firmware leaves free (~900 KB in several regions,
  `src/heap.c`); the SCUMM engine's screen buffers have a region of their own so that room
  changes do not fragment it.
- `patches/` holds changes to ScummVM applied at build time (`GW_SCUMM_SLIM`): smaller
  buffers, no GUI dialogs (they go to the log), saves without thumbnails and written at
  once for the Retro-Go menu, resources freed when memory runs low, and the fingerprints of
  a few Spanish copies ScummVM did not know.

</details>

<details>
<summary><b>Building</b></summary>

```
git submodule update --init
make              # out/scummvm.bin (arm-none-eabi-gcc on the PATH, or GCC_PATH=...)
make host         # PC test build: build/host/scummvm_host <game folder>
make host-gwheap  # the same with the console's heap, to find out-of-memory spots
```

`tools/host/` has the PC memory tools ([tools/host/README.md](tools/host/README.md)).

</details>

---

## Español

Core de [ScummVM](https://github.com/scummvm/scummvm) para la Game & Watch con el firmware
Retro-Go SD de sylverb, versión 2.0.0-rc1 o posterior (o su rama `external_cores`).
La 1.4.1 y anteriores no pueden cargar cores desde `/cores/`.

No incluye juegos ni los incluirá: usa los tuyos.

### Compatibilidad

✅ probado en la consola · 🟡 debería funcionar, aún sin probar en la consola · ❌ no soportado

| Juego | Motor | Estado | Notas |
|---|---|:-:|---|
| Maniac Mansion (PC) | SCUMM | ✅ | |
| Maniac Mansion (NES) | SCUMM | ✅ | |
| Zak McKracken and the Alien Mindbenders | SCUMM | ✅ | |
| Indiana Jones y la Última Cruzada | SCUMM | ✅ | |
| Loom | SCUMM | ✅ | |
| The Secret of Monkey Island (EGA, VGA, hablada) | SCUMM | ✅ | |
| Monkey Island 2: LeChuck's Revenge | SCUMM | ✅ | |
| Indiana Jones and the Fate of Atlantis | SCUMM | ✅ | |
| Day of the Tentacle | SCUMM | ✅ | |
| Sam & Max Hit the Road | SCUMM | ✅ | |
| Full Throttle | SCUMM | ✅ | el primer arranque prepara el juego (unos minutos) |
| Beneath a Steel Sky | Sky | ✅ | el primer arranque prepara el juego (unos minutos) |
| Sołtys | CGE | ✅ | |
| Lure of the Temptress | Lure | ✅ | |
| Broken Sword: La leyenda de los templarios | Sword1 | 🟡 | sin vídeos; mira las notas |
| The Dig | SCUMM | ❌ | no cabe en la flash |
| The Curse of Monkey Island | SCUMM | ❌ | no cabe en la memoria |
| Versiones de Macintosh | — | ❌ | |
| Versiones de FM-Towns | — | ❌ | |

En desarrollo, todavía no en este core: Ween: The Prophecy y Gobliiins (en estos dos no se
puede guardar desde el menú de Retro-Go: su motor no lo permite), Cruise for a Corpse,
Operation Stealth.

Abre una sección:

<details>
<summary><b>Instalación</b></summary>

1. Copia `scummvm.bin` en `/cores/` de la SD.
2. Copia los ficheros de cada juego, tal cual (`000.lfl`, `901.lfl`, `disk01.lec`…), en una
   carpeta propia dentro de `/scummvm/` — fuera de `/roms`, para que no salga en el menú.
3. Pon en `/roms/scummvm/` un fichero de texto `.scummvm` por juego. Su nombre es lo que
   muestra el menú; dentro, escribe el nombre de la carpeta del juego:

   ```
   /roms/scummvm/The Secret of Monkey Island.scummvm    (contiene: monkey)
   /scummvm/monkey/000.lfl
   /scummvm/monkey/901.lfl
   ...
   ```

   Si el fichero está vacío se usa la carpeta `/scummvm/<nombre del fichero>`. También vale
   una ruta completa (`/juegos/monkey`). Comprueba que el fichero no termine en `.txt`
   (activa ver las extensiones en Windows).
4. La primera vez que se abre un juego sus ficheros se copian a la flash externa (barras de
   progreso); después arranca directamente. Los ficheros de más de 24 MB (las voces de las
   versiones habladas) se leen de la SD.

   Los juegos que una versión anterior de este core ya copió a la flash se copian una vez
   más (sus ficheros de datos ahora se guardan descifrados, ver "Cómo está hecho").

</details>

<details>
<summary><b>Notas de cada juego</b></summary>

**Full Throttle**

Copia el juego entero tal cual (`FT.LA0`, `FT.LA1`, `MONSTER.SOU` y las carpetas `DATA` y
`VIDEO`) en su carpeta. `FT.LA1` (152 MB) no cabe en la flash: la primera vez, el core copia
lo que contiene salvo los sonidos en `FT.LA1.gwpack`, a su lado (28 MB, deja ese espacio
libre en la SD), y eso lo pone en la flash; lo muestra una barra de progreso y tarda unos
minutos (si la barra parece parada, sigue trabajando). Los sonidos, las voces y los vídeos
se leen de la SD según se reproducen. Si los
ficheros del juego cambian, el paquete se vuelve a hacer; se puede borrar cuando se quiera.

The Dig no cabe: solo los gráficos de sus personajes ocupan 57 MB.

**Beneath a Steel Sky**

Copia `sky.dsk`, `sky.dnr` y `sky.cpt` en la carpeta del juego (`sky.cpt` viene con ScummVM,
no con el juego). La primera vez, el core descomprime todo menos las voces en
`sky.dsk.gwpack`, a su lado (38 MB, deja ese espacio libre en la SD), y lo pone en la flash;
lo muestra una barra de progreso y tarda unos minutos. Las voces se leen de la SD. TIME abre
el panel de control del propio juego (guardar, cargar, volumen, velocidad del texto).

**Sołtys**

Copia `vol.cat` y `vol.dat` en la carpeta del juego (es freeware: scummvm.org/games).
`vol.dat` se guarda descifrado en la flash y los sprites se dibujan desde ahí.

**Lure of the Temptress**

Copia `DISK1.VGA` a `DISK4.VGA` en la carpeta del juego, junto con `lure.dat` de ScummVM
(viene con ScummVM, no con el juego). Todo va a la flash; no se crea nada en el primer
arranque.

**Broken Sword: La leyenda de los templarios** (PC CD)

- Copia las carpetas `clusters`, `music`, `speech` y `smackshi` del CD en la carpeta del juego.
  **Los vídeos se saltan** (cada uno necesita más de 1,5 MB de memoria), pero ScummVM
  reconoce el juego por `smackshi/intro.smk`: basta con ese archivo.
- Añade `language=` al fichero `.scummvm` (`language=es`…), o los subtítulos salen en inglés.
- El juego ocupa 150 MB y no cabe entero en la flash: en ella van la parte común y una o dos
  **zonas** (París, Irlanda, Siria…). El primer arranque copia unos 44 MB: tarda un rato.
- Al viajar a una zona que no está en la flash, el juego **se guarda solo y vuelve al menú**
  con un aviso. Vuelve a entrar: se copia esa zona y sigues justo donde estabas.
- El juego se dibuja a la mitad de tamaño, como lo muestra la pantalla, así que no se pierde nada.

</details>

<details>
<summary><b>Decirle a ScummVM qué juego es</b></summary>

ScummVM reconoce cada juego por una huella de sus ficheros y conoce miles de versiones. Con
una copia poco común (una edición rara, una versión hablada de aficionados…) tiene que
adivinar, y puede equivocarse. Se lo puedes indicar con líneas opcionales en el `.scummvm`,
debajo del nombre de la carpeta:

```
monkey_talkie
game=monkey
platform=pc
language=es
```

| Línea | Significado |
|---|---|
| `game=` | qué juego es, con el nombre corto de ScummVM (tabla abajo). Casi siempre basta con esta. |
| `platform=` | el ordenador para el que se hizo la versión: `pc`, `amiga`, `atari`, `fmtowns`, `segacd`… |
| `language=` | `es`, `en`, `fr`, `de`, `it`… |
| `extrapath=` | una subcarpeta (de la carpeta del juego, o una ruta absoluta) con más ficheros del juego, como las pistas de música de CD: `extrapath=cd_music_flac` para el Monkey Island Ultimate Talkie. |

Con `game=` solo se tiene en cuenta ese juego, igual que al añadirlo a mano en el lanzador de
ScummVM; `platform=` y `language=` descartan las versiones que no coinciden. Sin estas
líneas no cambia nada: ScummVM elige lo que encuentra en la carpeta.

| Juego | `game=` |
|---|---|
| Maniac Mansion | `maniac` |
| Zak McKracken and the Alien Mindbenders | `zak` |
| Indiana Jones y la Última Cruzada | `indy3` |
| Loom | `loom` |
| The Secret of Monkey Island | `monkey` |
| Monkey Island 2: LeChuck's Revenge | `monkey2` |
| Indiana Jones and the Fate of Atlantis | `atlantis` |
| Day of the Tentacle | `tentacle` |
| Sam & Max Hit the Road | `samnmax` |
| Full Throttle | `ft` |
| Beneath a Steel Sky | `sky` |
| Sołtys | `soltys` |
| Lure of the Temptress | `lure` |
| Broken Sword | `sword1` |

El nombre corto de cada juego está en la
[lista de compatibilidad](https://www.scummvm.org/compatibility/) de ScummVM, columna
"ScummVM ID", sin el motor delante (`scumm:ft` → `game=ft`). La lista incluye todos los
motores, no solo los de LucasArts, pero por ahora este core ejecuta los juegos de la
tabla: SCUMM (The Dig y The Curse of Monkey Island no), Beneath a Steel Sky, Sołtys, Lure of the Temptress y Broken Sword.

No arreglan una copia incompleta ni las versiones que este core deja fuera a propósito:
Macintosh (sus buffers están recortados para ahorrar memoria) y FM-Towns.

</details>

<details>
<summary><b>Partidas guardadas</b></summary>

- **Menú de Retro-Go (PAUSE/SET):** *Save & Continue*, *Save & Quit* y *Reload*, con los 4
  huecos del firmware y su vista previa, como en los demás cores. Al apagar también se
  guarda, y *Resume* en la lista de juegos sigue desde ahí. Estas partidas van donde el
  firmware guarda las de todos los cores.
- **Menú propio del juego (TIME):** lo que abre F5 en un PC: salvar, cargar, jugar, salir.
  El nombre se escribe con el teclado en pantalla (GAME); si se deja vacío, se usa la fecha y
  la hora. Estas partidas son los ficheros normales de ScummVM, en `/saves/scummvm/`.
- Las partidas no llevan miniatura (costaría 128 KB de memoria).

</details>

<details>
<summary><b>Controles</b></summary>

| Botón | Acción |
|---|---|
| Cruceta | mover el puntero (acelera al mantener); empieza en el centro de la pantalla |
| B | clic izquierdo |
| A | clic derecho |
| GAME | teclado en pantalla |
| TIME | F5: menú propio del juego (salvar, cargar, salir) |
| START (Zelda) | Esc: saltar escena |
| SELECT (Zelda) | `.`: saltar la frase actual |
| PAUSE/SET | menú de Retro-Go (guardar, cargar, opciones) |

**Teclado en pantalla** (GAME): dos filas encima de la imagen y dos debajo, así nunca tapa
el juego. La cruceta mueve, A escribe, B o GAME lo cierran; `Shf` pone la siguiente letra en
mayúscula (dos veces: bloqueada). Tiene Esc, Enter, Espacio, Retroceso, F1–F10, números y
letras; en una Mario, Esc (saltar escena), `.` (saltar frase) y Enter ("pulsa ENTER para
continuar") se escriben desde aquí.

**Subtítulos**: en las versiones habladas están activados. Para cambiarlo: PAUSE/SET →
Opciones → Subtítulos (voz y texto / solo texto / solo voz), o `Txt` en el teclado (Ctrl+T).
La elección se guarda para todos los juegos.

</details>

<details>
<summary><b>Cuando un juego no arranca</b></summary>

Si un juego vuelve al menú de Retro-Go, el motivo sale en pantalla y queda escrito en
`/saves/scummvm/scummvm.log`.

</details>

<details>
<summary><b>Cómo está hecho</b></summary>

- Lo esencial (arranque, acceso a la SD, el heap) va en la RAM del core; el resto de ScummVM
  (~2,5 MB de código) se ejecuta directamente desde la flash externa (XIP). Al arrancar, el
  core se copia a la flash y se reubica a la dirección donde quedó (`tools/xip_pack.py`,
  `src/boot.c`).
- Los ficheros del juego se leen desde la flash externa, sin copiarlos a RAM. Los de datos
  de los juegos SCUMM v4-v6 (cifrados con XOR 0x69 en el disco) se guardan descifrados, así
  el motor usa las habitaciones, disfraces y scripts donde están en vez de cargarlos en el
  heap — lo que permite que quepan Sam & Max y los demás juegos VGA grandes.
- Las voces y la música de CD comprimidas se decodifican con decodificadores pequeños de un
  solo fichero en vez de las librerías que usa ScummVM (`src/gw_codecs.h`): Ogg Vorbis con
  stb_vorbis (`.sog`, `trackN.ogg`), MP3 con minimp3 (`.so3`, `trackN.mp3`) y FLAC con
  dr_flac (`.sof`, `trackN.flac`). Las voces sin comprimir (`.sou`) funcionan como en ScummVM.
- El heap usa toda la RAM libre que deja el firmware (~900 KB en varias zonas, `src/heap.c`);
  los buffers de pantalla del motor SCUMM tienen una zona propia para que los cambios de
  habitación no la fragmenten.
- `patches/` contiene cambios a ScummVM que se aplican al compilar (`GW_SCUMM_SLIM`): buffers
  más pequeños, sin diálogos de la GUI (van al registro), partidas sin miniatura y escritas al
  momento para el menú de Retro-Go, recursos liberados cuando falta memoria y las huellas de
  algunas copias españolas que ScummVM no conocía.

</details>

<details>
<summary><b>Compilar</b></summary>

```
git submodule update --init
make              # out/scummvm.bin (arm-none-eabi-gcc en el PATH o GCC_PATH=...)
make host         # versión de PC para pruebas: build/host/scummvm_host <carpeta del juego>
make host-gwheap  # lo mismo con el heap de la consola, para encontrar faltas de memoria
```

`tools/host/` tiene las herramientas de medición de memoria en el PC
([tools/host/README.md](tools/host/README.md)).

</details>
