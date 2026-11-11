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
- Animations (the intro): the unpacker asked for 300000 bytes and cut the buffer afterwards;
  the biggest unpacks to 190 KB, which the console does not have left (seen there: out of
  memory in the intro's last scenes). The packing has no back references, so a VGA animation
  is unpacked as it is shown, a byte at a time, from the packed resource in the flash
  (`GwVgaStream`, `AnimationSequence::gwRead`); a loop starts the unpacker again.
- Sound: the AdLib music alone, scaled by the volume twice (driver and mixer), peaked at 2%
  of full scale. Full volume and eight times the output gain for this engine
  (`src/gw_system.cpp`, `g_gwGain`).
- Save and load from the Retro-Go menu go through ScummVM's own interface
  (`src/gw_system.cpp`, `generic_save`/`generic_load`), as for every engine without a hook.
