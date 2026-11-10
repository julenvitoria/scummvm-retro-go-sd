# Lure of the Temptress on the console: where the memory goes

The engine is ScummVM's `lure`, built with `-DGW_LURE_SLIM`. The game is 2.5 MB (DISK1-4.VGA)
plus ScummVM's `lure.dat`; all five files are cached in the flash.

On the PC (64-bit) the unpatched engine peaks at 1678 KB before the first room. On the console
(gwemu) it now reaches the first room, through the whole intro, with the 886 KB heap.

- Movement frames: the 7400 steps of the game's animations were a list node, a shared pointer
  and a 6-byte object each. One array per list now (`MovementDataList`): 538 KB less on the PC.
- Resources (`Disk::getEntry`) are used where they lie in the flash, read-only; most are
  unpacked or parsed and dropped at once. `getEntryRam()` for the ones written to (the font,
  patched for Italian; the audio icon).
- A room's foreground layers keep only their occupied 32x32 cells, not a screen each
  (`RoomLayer::cellPixels`), and are unpacked without a screen of their own.
- Animations (the intro): the unpacker asked for 300000 bytes and cut the buffer afterwards.
  It runs twice, the first time only counting, and for VGA animations writes into 2 KB
  pieces (`vgaDecodeChunks`, `AnimationSequence::gwRead`): the biggest seen is 190 KB, and the
  console's heap is several regions, none with that much free in one block. 2 KB pieces fit
  the holes the small heaps are left with.
- Save and load from the Retro-Go menu go through ScummVM's own interface
  (`src/gw_system.cpp`, `generic_save`/`generic_load`), as for every engine without a hook.
