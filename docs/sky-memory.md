# Beneath a Steel Sky on the console: where the memory goes

The engine is ScummVM's `sky` (Revolution's Virtual Theatre), built with
`-DGW_SKY_SLIM` (`make ... SKY_SLIM=` builds the PC test program without the cuts).

## The engine loads every file whole

`sky.dsk` holds 5099 files, most of them packed (RNC ProPack, method 1). `Disk::loadFile()`
reads one, unpacks it and hands out the buffer. Unpacked, the CD version's biggest are an
animation of 998 KB, a sprite set of 580 KB, and 34 files over 100 KB: more than the heap.

- `src/pack.c`, `sky_build()`: on the first launch every file but the speech (numbers
  50000-58999, 3493 files of at most 58 KB) is unpacked into `sky.dsk.gwpack` next to it
  (38 MB), exactly as `loadFile()` would return it, and the pack is cached in the flash like
  Full Throttle's. The unpacker is ScummVM's, rewritten to stream: input through an 8 KB
  buffer, output through a 64 KB window (matches reach back 32 KB), so neither the packed
  file (up to 400 KB) nor the unpacked one is ever whole in memory.
- `Disk::loadFile()` asks `gwSkyFile()` (`src/gw_system.cpp`) first and returns the file
  where it lies in the flash. **Read-only**; `free()` leaves a pointer outside the heap alone.
- The callers that write into what they load use `Disk::loadFileRam()` (a copy): the screen a
  picture is shown on, the grids, the music (the AdLib driver keeps its register mirror
  inside the music data). The intro's voices are played from the flash past their header
  instead of silencing the header in place.
- On the PC the pack is mapped read-only, so a caller that writes into a file used in place
  crashes there instead of faulting on the console. Only the paths a test run goes through
  are proven this way.

## The compacts

`sky.cpt` (419 KB) unpacks to 297 KB of "compacts" plus 42 KB of names for the debugger,
through a second buffer as big: 640 KB at start.

- Only what a saved game holds is ever written: the compacts proper (52 KB), the route
  buffers and the get-to tables. Scripts, animation sequences and tables (244 KB) are used
  where they lie in `sky.cpt` in the flash. The talk table too is copied: the engine patches
  it for a bug in old `sky.cpt` files.
- The file is read in pieces as it is unpacked; no names.
- 404 KB -> 109 KB.

## The rest

- The SCUMM screens' heap is everyone's (`heap_screen_spill`).
- Retro-Go's save and load: slot 99, `SKY-VM.099`, redirected to the firmware's file like
  SCUMM's `.s99`. A load asked for from the menu is done by the engine between two frames.
- At the peak of a test run (saving in the first room): 578 KB of 893 KB, 128 KB of it the
  engine's scratch buffer for the saved game.
