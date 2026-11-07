# ScummVM para Game & Watch (Retro-Go SD)

Core de [ScummVM](https://github.com/scummvm/scummvm) para la Game & Watch con el
firmware Retro-Go SD de sylverb (rama `external_cores`). Empieza por el motor SCUMM
(aventuras de LucasArts).

**Estado: estudio de viabilidad.** Ver [docs/estudio-viabilidad.md](docs/estudio-viabilidad.md):
memoria que necesita un juego, tamaño del código para Cortex-M7 y qué ofrece el firmware
para usar la flash externa.

- `tools/host/`: herramientas de medición en el PC (backend headless para ScummVM,
  contador de memoria, informes). Ver [tools/host/README.md](tools/host/README.md).

Los juegos no se incluyen ni se incluirán: cada uno pone los suyos.
