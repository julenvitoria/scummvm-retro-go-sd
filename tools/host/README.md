# Medición en el PC

ScummVM con el backend `null` modificado (`headless-null-backend.patch`): guarda la pantalla
en PPM y acepta entrada programada. Sin SDL ni ventana.

```
git clone --depth 1 https://github.com/scummvm/scummvm.git    # probado en 24e1cad
cd scummvm && git apply ../tools/host/headless-null-backend.patch && cd ..
mkdir build && cd build
../scummvm/configure --backend=null --disable-all-engines --enable-engine=scumm \
  --disable-engine=scumm-7-8,he --disable-detection-full --enable-release
make -j$(nproc)
gcc -O2 -shared -fPIC ../tools/host/memtrack.c -o memtrack.so -ldl -lpthread
```

Ejecutar (las rutas son ejemplos):

```
HL_SHOTS=shots HL_EVERY=2000 \                 # una captura cada 2 s en shots/fNNNNN.ppm
HL_EVENTS="3000:c:160,100;5000:k:a;9000:q" \   # ms:c=clic x,y | r=clic derecho | m=mover | k=tecla | q=salir
MEMTRACK_OUT=mem.txt \                          # memoria viva y pico cada 250 ms
MEMTRACK_BIG=allocs.txt MEMTRACK_MIN=1 \        # cada reserva >= MIN bytes con su pila
LD_PRELOAD=./memtrack.so ./scummvm -b 1 --path=/ruta/al/juego monkey
python3 ../tools/host/heap_report.py allocs.txt ./scummvm   # memoria viva por categoría
```

`-b N` es el "boot param" de SCUMM: en Monkey Island salta a distintas partes del juego.

Tamaño para la consola: configurar con `CXX=arm-none-eabi-g++`,
`CXXFLAGS="-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Os -ffunction-sections
-fdata-sections -fno-exceptions"`, `LDFLAGS=--specs=nosys.specs`, `--host=arm-none-eabi`
(cambiar el `-O2` que añade `--enable-release` por `-Os` en `config.mk`), `make -k`, enlazar
los `.o` con `-Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all -Wl,-Map=full.map` y
`python3 ../tools/host/mapsize.py full.map`.

## gwheap.c — the console's heap on the PC

Runs the PC build inside `src/heap.c` with pools of a fixed size, to see where a game runs out
of memory (and with a backtrace of the allocation that failed):

```
gcc -O2 -shared -fPIC -Isrc tools/host/gwheap.c src/third_party/tlsf/tlsf.c -DNDEBUG -o gwheap.so
GW_WAV=/dev/null GW_HEAP_BIG=700 GW_HEAP_SMALL=300 GW_HEAP_SCREEN=136 LD_PRELOAD=./gwheap.so \
    build/host/scummvm_host <game folder>
```

The PC has 64-bit pointers: the same game needs more memory than on the console, so this
shows where the pressure is rather than the console's exact limit.

`GW_NO_INPLACE=1` keeps the SCUMM data files as they are on disk (encrypted), so every
resource is loaded into the heap as before the in-place resources: to compare the two.
Otherwise the files are decrypted as on the console and mapped read-only, so the engine
writing into a resource it uses in place crashes here instead of passing unnoticed.

## codec_test.c — the audio decoders against a reference

Decodes a file with `src/gw_codecs.h` (what the core uses for speech and CD music) to raw
PCM, optionally after a seek, to compare with ffmpeg's output:

```
gcc -O2 -Isrc tools/host/codec_test.c src/codecs/*.c -lm -o codec_test
./codec_test ogg|mp3|flac in.ogg out.raw [seek_frame]
ffmpeg -i in.ogg -f s16le ref.raw
```

MP3 output starts with the encoder's delay (ffmpeg trims it, minimp3 and libmad do not).

