# SCUMM v7 (Full Throttle, The Dig) on the console: where the memory goes

Branch `scumm7` only. Measured on the PC with the console's heap (`make host-gwheap`,
`GW_HEAP_TRACE=<bytes>` lists every allocation at least that big with its caller;
`tools/host/README.md`). The console has ~900 KB of heap.

## Full Throttle, start of the intro (concurrent use)

| | unmodified | with the cuts in this branch |
|---|---|---|
| peak heap | ~3.4 MB | ~1.75 MB |

Cuts made (all under `GW_V7_SLIM`, `make ... V7_SLIM=` builds without them to compare):

- Digital iMUSE stream buffers 110+220+198 KB -> 3 x 24 KB, dispatch feed 16 KB -> 4 KB
- crossfade buffers 526 KB -> 88 KB (fades clamped to ~1 s)
- mixer amplitude/volume tables 213 KB -> const in flash (`tools/gen/dimuse_mixtable.c`)
- dispatch maps 8 x 8 KB -> 8 x 1 KB (The Dig's maps are <= 1 KB; COMI refused)
- SMUSH audio ring buffers 700 KB -> 112 KB
- codec 37 decoder 271 KB -> 207 KB (the margin past the second frame)

What is left, and why it does not go away by changing a number:

- sound resources: FT.LA1 is 156 MB, too big for the flash cache, so every sound is read
  from the card into the heap; three at once in the intro are ~310 KB
- codec 37's two frames and their motion-vector margins: 207 KB while a video plays
- NUT fonts, decoded: ~130 KB
- the engine objects themselves: ScummEngine_v7 86 KB, IMuseDigital 100 KB
- virtual screens (v7 keeps wider ones for scrolling): ~200 KB

Getting under ~900 KB would mean streaming the sounds from the card instead of loading
them, and rewriting how fonts and video frames are held. Parked.
