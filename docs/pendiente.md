# Pendiente / ideas para más adelante

Cosas medidas o vistas que se dejan para cuando hagan falta. Cada una dice qué se sabe y qué
falta comprobar.

## Memoria

### Remapeo de teclas de ScummVM (keymapper)

- Ocupa unos 37 KB en el programa de pruebas del PC durante toda la partida (medido con
  Operation Stealth, `GW_HEAP_PEAK` + `tools/host/heap_peak.py`; en la consola, menos, por los
  punteros de 32 bits). Lo reservan `Common::Keymapper::reloadKeymapMappings`,
  `OSystem::getEventManager` y el `initKeymaps` de cada motor.
- Los avisos `HardwareInput with ID '...' not known` del log salen de ahí.
- Riesgo: algunos motores dependen de sus acciones para teclas concretas (saltar escenas con
  START, la pausa...). Antes de quitarlo o recortarlo hay que comprobar juego a juego qué deja
  de funcionar.
- Hacerlo en una rama aparte.

### Ya quitado

- Autoguardado de ScummVM (cada 5 minutos, con miniatura y copia de la pantalla):
  desactivado en `src/gw_system.cpp` (`autosave_period` = 0).

## Otros

- Full Throttle: la voz se entrecorta en algunos vídeos (el usuario prefiere dejarlo así de
  momento). Posible tabla de 38 KB de `Sound::setupSfxFile` con las voces comprimidas.
- Decodificador OGG y Loom FM-Towns: para el final.
- Motores futuros: solo juegos de puntero, sin escribir órdenes (en la Game & Watch no hay
  teclado y el teclado en pantalla no sirve para jugar). Decidido el 2026-11-15.
  - Fuera: AGI (todo de escribir salvo Manhunter, Black Cauldron y los infantiles) y la primera
    generación de SCI (SCI0: King's Quest 4, Space Quest 3, Larry 2 y 3, el remake de KQ1).
    El trabajo de AGI queda en la rama `agi`, sin fusionar.
  - Orden propuesto: Queen, Drascula, Teenagent, Dreamweb, AGOS (Simon, Elvira), Kyra; SCI1 y
    SCI1.1 (de ratón: KQ5, SQ4, el remake de Larry 1) al final, por la memoria.
- Beneath a Steel Sky: podría usar la adopción de pantalla como Lure.
- Full Throttle, sala 2: negra, 320x1200; no cabe ni con el doble de memoria. Puede que sean
  los créditos finales en texto. Rama `ft-wide` (barrido de salas del 2026-11-16).
- Motores en prueba (2026-11-16), cada uno en su rama, encadenadas: `queen` -> `drascula` ->
  `dreamweb`. Pendiente de que el usuario los pruebe en la consola antes de fusionar.
  - Drascula: `drascula.dat` de ScummVM en la carpeta del juego; los packet se descomprimen
    la primera vez en `gwx/` (63 MB en la tarjeta).
  - DreamWeb: las terminales piden escribir órdenes (teclado en pantalla, GAME).
- Teenagent: sin ficheros del juego todavía (es gratuito, scummvm.org/games).
- Igor: Objetivo Uikokahonia: el motor `igor` ya no está en ScummVM (se quitó hace años); no
  se puede sin portarlo de una versión antigua.
