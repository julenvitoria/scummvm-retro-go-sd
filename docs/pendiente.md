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
- Motores futuros: AGI primero, SCI después.
- Beneath a Steel Sky: podría usar la adopción de pantalla como Lure.
