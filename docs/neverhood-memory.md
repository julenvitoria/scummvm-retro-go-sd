# The Neverhood: memory study (2026-11-19)

Data: the Windows English CD (`A C HD I M S T .BLB`, 621 MB). The engine builds for the console
(`neverhood.svm` 2.0 MB) and runs on the PC test build through the intro.

## What the intro takes (PC, first minute): 2,257 KB peak

| | KB |
|---|---|
| background resource, decompressed whole (`Background` -> `loadResource`) | 301 |
| `Screen` back buffer, 640x480 | 300 |
| the background's own surface, 640x480 | 300 |
| the backend's screen copy, 640x480 | 300 |
| two sound effects, decompressed whole, + the DCL decompressor's window | 155 + 96 + 82 |
| resource index (`ResourceMan::_entries`, a HashMap of 2,750 entries) + `BlbArchive` entries | ~250 |

## What the data holds (all seven archives, decompressed sizes)

| type | count | total | biggest |
|---|---|---|---|
| 2 sprite / background bitmap, DCL | 1,374 | 93 MB | 313 KB |
| 4 animation, DCL | 287 | 32 MB | **1,882 KB** |
| 7 sound, raw / DCL | 147 / 148 | 3.7 / 5.7 MB | 213 / 157 KB |
| 8 music, raw | 34 | 42 MB | 4.3 MB |
| 10 Smacker video, raw | 551 | 515 MB | 27 MB |

## Why it does not fit by the usual means

- Half resolution (as Touche) takes the four 300 KB screens to ~300 KB together.
- But the engine keeps every **animation decompressed whole** while it is on screen, and one is
  1.9 MB: frames are offsets into that buffer. DCL (PKWARE implode) is sequential, so a frame
  cannot be decompressed on its own without starting over from the resource's beginning.
- Sounds are decompressed whole too (up to 213 KB): those could be streamed.

## The only way seen

A pack built on the first start, as `sci.gwpack` is for SCI: every sprite and animation
decompressed and **downscaled to half size**, kept in the external flash and read in place (no
RAM). Sizes: 93 MB / 4 + 32 MB / 4 = **~31 MB of flash**, plus the engine's drawing code taught
to draw half-size pixel data at full-size coordinates (as Touche's `GW_TOUCHE_HALF`). Smacker
video (also used inside rooms) needs the lightweight streaming player of its own.

Several sessions of work, and the flash must have ~31 MB free beside the core.
