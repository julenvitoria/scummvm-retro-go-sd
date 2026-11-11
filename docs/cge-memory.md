# Sołtys on the console: where the memory goes

The engine is ScummVM's `cge`.

- A sprite's frames (`.VBM` files inside `vol.dat`) are already in the form they are drawn
  from, and the engine copied each into memory: 311 KB in a busy scene, which the console
  ran out of ("Bad VBM"). `vol.dat` is XORed with 0xA5 throughout, so the core keeps it in
  the flash decrypted (`svm_fixed_key()` in `src/pack.h`, the file goes into the flash cache
  under `folder///vol.dat`), reads through the stream give the original bytes back, and
  `Bitmap::loadVBM()` takes the frame where it lies (`EncryptedStream::gwDirect`).
  Read-only; `delete[]` leaves a pointer outside the heap alone.
- No fourth video page.
- `src/third_party/tlsf/tlsf.c`: a request is first looked for in its own size class.
  TLSF rounds a request up to the next class, so a free block of exactly the size asked
  for (a 64000-byte page just freed) was never found for the same request.
- Peak in a test run on the PC (64-bit, to the second scene of the game): 970 KB -> 658 KB.
