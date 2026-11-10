# SCUMM v7 (Full Throttle) on the console: where the memory goes

Branch `scumm7` only. The console has ~900 KB of heap. Full Throttle runs in gwemu (intro,
videos, first room) with everything below; The Dig does not fit (see the end).

Tools: `make host-gwheap` runs the PC build at the console's heap sizes (`GW_HEAP_BIG=470
GW_HEAP_SMALL=283 GW_HEAP_SCREEN=160`); `GW_HEAP_PEAK=<file>` keeps what was live at the
peak (or at the first failure) with 8 frames of callers, `tools/host/heap_peak.py` sums it;
`GW_HEAP_MAP=<file>` writes every block of every pool at the first failure (fragmentation).
The PC is 64-bit: its pointers make the same game ~60 KB heavier than the console, so a PC
run at `GW_HEAP_BIG=540` is about what the console has.

## The data file does not fit in the flash: resource packs

FT.LA1 is 152 MB: 120 MB of sounds, 22 MB of costumes (AKOS, up to 550 KB each), 4.5 MB of
rooms, scripts and charsets. Loaded into the heap, one costume is more than the heap has.

- `src/pack.c`: on the first launch the resources other than sounds are copied into
  `FT.LA1.gwpack` next to it on the card (28 MB, 1063 blocks), which is cached in the flash
  like the game's other files. A table maps each block's offset in FT.LA1 to the pack.
- `src/gw_system.cpp`, `GWPackedStream`: the stream FT.LA1 is read through serves reads that
  fall in a packed block from the flash, and `gwDirectResource()` hands those blocks out
  where they lie, so `loadResource()` uses them in place as with v4-v6 data files.
- Sounds: those over 8 KB are never loaded. Music streams read them from the card
  (`ImuseDigiSndMgr::openSound`), and effects are read through six 4 KB windows
  (`IMuseDigiFilesHandler::soundData()`); scripts' "load sound" skips them.
- Of the subfolders (DATA, VIDEO) only the .NUT fonts go to the flash; the videos are read
  from the card.
- The firmware allows 6 open files. Read streams on card files share at most 3 real handles
  (`GWCardFile` in `src/gw_system.cpp`), reopened on demand.

## Cuts (all under `GW_V7_SLIM`)

- iMUSE: stream buffers 12 KB each for FT (24 KB for The Dig); no fade buffers for FT (88 KB,
  it crossfades through its own buffer, 12 KB here); mixer and IMC tables as const data in
  flash (`tools/gen/`); maps 1 KB; output delayed 3 feeds instead of 7
- codec 37: two blocks of frame + 31-row margins (2 x 82 KB) instead of one of 207 KB. For FT
  the decoder is made at start and kept (made when a video started, it no longer found two
  holes that big), and its first block is lent to the room's back buffer while no video
  plays: a video draws on the front buffer and the room is redrawn whole after it
- SMUSH: one 48 KB frame-object buffer, kept. Audio tracks: a video delivers a sound up to
  twice as fast as it plays, and the player holds what is ahead in rings of 100 KB (three)
  and 400 KB; smaller rings make it skip forward (speech in pieces), and a sound picks its
  track by those sizes. Here the four rings are 20 KB, made when a video uses them, and a
  sound still picks its track by the original sizes. A piece that does not fit yet is not
  copied: its place in the video file is queued (`_gwPend`) and `gwPumpAudio()` reads it
  from the card at the end of a frame once there is room. Music that comes as fast as it
  plays never waits and is never read twice
- NUT fonts (and Insane's sprites) decoded a row at a time from the flash, not up front
- Insane (bike scenes) made at the first one: ~50 KB of tables and fonts
- v7 never composes the text surface: 64 KB, and the composite buffer, gone
- blast text queue 16 instead of 50 (27 KB -> 8 KB)
- the game's subfolders are no longer listed into memory (`GW_FS_PROBE`, 40 KB for FT)
- the heap asks the resource cache to drop something before giving up an allocation, and v7
  lets blocks of 16 KB or more use what its screen buffers leave of their heap
- `make core HEAP_MAP_LOG=1`: at the first failed allocation, scummvm.log gets every block of
  2 KB or more with its caller, and the free holes (a heap map from the console itself)

## Diagnostics

scummvm.log is saved while the game runs (every few seconds when it has new lines), not only
on exit. Each video is logged with its size and first bytes; one that is not found gets the
listing of its folder.

## What is left (PC, at the peak of a video)

codec 37 164 KB, screens 137 KB, SMUSH audio up to 94 KB (four tracks), resource
index 77 KB, the engine object 66 KB, the backend's screen 62 KB, iMUSE ~70 KB, actors 32 KB.

## The Dig

DIG.LA1 is 85 MB, of which 57 MB are costumes (one is 1.7 MB) and 6.5 MB rooms: its pack
alone would be the whole flash. It would need costumes read from the card on demand.
