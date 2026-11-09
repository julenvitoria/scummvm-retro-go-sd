# ScummVM core for Retro-Go SD.
#
#   make host        PC test build: build/host/scummvm_host <game folder>
#   make             the core (needs arm-none-eabi-gcc on PATH or GCC_PATH=...)
#
# ScummVM itself is the submodule scummvm/ (pinned); config/ holds the config.h and engine
# tables its configure produced for this build (SCUMM v0-v6 only, see config/README.md), and
# config/sources.txt the ScummVM files compiled.

SVM      := scummvm
SVM_SRC  := $(shell cat config/sources.txt)
OWN_CXX  := src/gw_system.cpp src/gw_audio_decoders.cpp
CODECS   := src/codecs/gwc.c src/codecs/gwc_vorbis.c src/codecs/gwc_mp3.c src/codecs/gwc_flac.c

DEFS     := -DHAVE_CONFIG_H -DENABLE_SCUMM=STATIC_PLUGIN -DRELEASE_BUILD \
            -DDISABLE_HELP -DDISABLE_FANCY_THEMES -DDISABLE_SAVELOADCHOOSER_GRID \
            -DDISABLE_LAUNCHERDISPLAY_GRID -DDISABLE_TOWNS_DUAL_LAYER_MODE \
            -DDISABLE_PC98_RHYTHM_CHANNEL -DDISABLE_NES_APU -DDISABLE_MAME_OPL -DREDUCE_MEMORY_USAGE -DGW_SCUMM_SLIM
INCS     := -Iconfig -I$(SVM) -I$(SVM)/engines -Isrc
CXXSTD   := -std=c++11 -fno-exceptions -fcheck-new -fno-operator-names
WARN     := -Wno-multichar -Wno-reorder -Wno-unknown-pragmas -Wno-unused-parameter

.PHONY: all host core clean
all: core

# patches/*.patch are applied to the submodule's working tree before anything is compiled
# (GW_SCUMM_SLIM: engine buffers cut down for the console); applying twice is a no-op
PATCHED  := build/.svm_patched
$(PATCHED): $(wildcard patches/*.patch)
	@mkdir -p $(dir $@)
	@for p in $^; do \
	  if git -C $(SVM) apply --reverse --check ../$$p 2>/dev/null; then echo "  (already applied) $$p"; \
	  else git -C $(SVM) apply ../$$p && echo "  PATCH $$p" || exit 1; fi; done
	@touch $@

# ---------------------------------------------------------------- host
HB       := build/host
HOST_OBJ := $(SVM_SRC:%=$(HB)/svm/%.o) $(OWN_CXX:%=$(HB)/%.o) $(CODECS:%=$(HB)/%.o) $(HB)/src/platform_host.c.o $(HB)/src/osk.c.o

.PHONY: host-gwheap
host: $(HB)/scummvm_host

$(HB)/svm/%.cpp.o: $(SVM)/%.cpp Makefile | $(PATCHED)
	@mkdir -p $(dir $@)
	@g++ -c -O1 -g $(CXXSTD) $(WARN) $(DEFS) $(INCS) $< -o $@

$(HB)/svm/%.c.o: $(SVM)/%.c Makefile | $(PATCHED)
	@mkdir -p $(dir $@)
	@gcc -c -O1 -g $(DEFS) $(INCS) $< -o $@

$(HB)/src/%.cpp.o: src/%.cpp src/platform.h
	@mkdir -p $(dir $@)
	g++ -c -O1 -g $(CXXSTD) $(WARN) $(DEFS) $(INCS) $< -o $@

$(HB)/src/%.c.o: src/%.c src/platform.h
	@mkdir -p $(dir $@)
	gcc -c -O1 -g -Wall $< -o $@

$(HB)/src/codecs/%.c.o: src/codecs/%.c src/codecs/gwc_priv.h src/gw_codecs.h
	@mkdir -p $(dir $@)
	gcc -c -O2 -g -w $< -o $@

$(HB)/scummvm_host: $(HOST_OBJ)
	@g++ $^ -o $@ && echo "  LINK $@"

# the same with the console's heap (src/heap.c) instead of the PC's malloc: GW_HEAP_* in
# tools/host/gwheap.c set its sizes -- shows where a game runs out of memory
host-gwheap: $(HB)/scummvm_host_gwheap
$(HB)/gwheap.o: tools/host/gwheap.c src/heap.c
	gcc -c -O1 -g -Isrc $< -o $@
$(HB)/tlsf.o: src/third_party/tlsf/tlsf.c
	gcc -c -O1 -g -DNDEBUG $< -o $@
$(HB)/scummvm_host_gwheap: $(HOST_OBJ) $(HB)/gwheap.o $(HB)/tlsf.o
	@g++ $^ -o $@ && echo "  LINK $@"

# ---------------------------------------------------------------- the core
ifdef GCC_PATH
CROSS    := $(GCC_PATH)/arm-none-eabi-
else
CROSS    := arm-none-eabi-
endif
AB       := build/arm
OUT      := out/scummvm.bin
BUILD_ID := 0x4C554341                    # "LUCA": tells this core's .bin apart in /cores
MCU      := -mcpu=cortex-m7 -mtune=cortex-m7 -mthumb -mno-unaligned-access -mfpu=fpv5-d16 -mfloat-abi=hard
OPT      := -Os -ffunction-sections -fdata-sections -fno-common
ARM_CXX  := $(MCU) $(OPT) $(CXXSTD) -fno-threadsafe-statics $(WARN) $(DEFS) $(INCS)

SDKDEFS  := -DUSE_HAL_DRIVER -DSTM32H7B0xx -DSD_CARD=1 -DGNW_DISABLE_COMPRESSION -DIS_LITTLE_ENDIAN \
            -DCOVERFLOW=0 -DCHEAT_CODES=0 -DGNW_CORE_ENTRY_TARGET=app_main_scummvm -DSVM_BUILD_ID=$(BUILD_ID)
SDKINC   := $(addprefix -Isdk/include/,Core/Inc Core/Inc/retro-go Core/Inc/porting Core/Src/porting/lib \
            Core/Src/porting/lib/FatFs Drivers/STM32H7xx_HAL_Driver/Inc Drivers/STM32H7xx_HAL_Driver/Inc/Legacy \
            Drivers/CMSIS/Device/ST/STM32H7xx/Include Drivers/CMSIS/Include) -Isdk/src -Isrc
ARM_C    := $(MCU) $(OPT) -Wall $(SDKDEFS) $(SDKINC)

# firmware calls of boot.c / platform_core.c go to the bridge; the C library stays ours (newlib)
LIBC_NAMES := memchr memcmp strchr strcmp strlen strncmp strncpy strrchr strstr strcpy strcat strtol strtod \
            isalnum isalpha isspace isupper islower isxdigit tolower toupper abort qsort pow malloc free \
            realloc rand calloc sprintf vsprintf snprintf vsnprintf sscanf atoi strcasecmp strncasecmp \
            strtoul strncat setjmp longjmp __assert_func exit cosf sqrtf log10 getenv
REDEF    := $(AB)/redefine_syms.txt

ARM_SVM  := $(SVM_SRC:%=$(AB)/svm/%.o) $(OWN_CXX:%=$(AB)/%.o) $(CODECS:%=$(AB)/%.o)
ARM_OWN  := $(AB)/boot.o $(AB)/platform_core.o $(AB)/osk.o $(AB)/heap.o $(AB)/tlsf.o $(AB)/gw_core_bridge.o $(AB)/gw_core_entry.o

core: $(OUT)

$(REDEF): sdk/src/gw_core_bridge_redefine_syms.txt Makefile
	@mkdir -p $(dir $@)
	@grep -v '^#' $< | grep -v '^\s*$$' | awk 'BEGIN{split("$(LIBC_NAMES)",x," "); for(i in x) skip[x[i]]=1} !($$1 in skip)' > $@

$(AB)/svm/%.cpp.o: $(SVM)/%.cpp Makefile | $(PATCHED)
	@mkdir -p $(dir $@)
	@echo "  CXX  $<"
	@$(CROSS)g++ -c $(ARM_CXX) $< -o $@

$(AB)/svm/%.c.o: $(SVM)/%.c Makefile | $(PATCHED)
	@mkdir -p $(dir $@)
	@$(CROSS)gcc -c $(MCU) $(OPT) $(DEFS) $(INCS) $< -o $@

$(AB)/src/%.cpp.o: src/%.cpp src/platform.h Makefile
	@mkdir -p $(dir $@)
	$(CROSS)g++ -c $(ARM_CXX) $< -o $@

# the audio decoders (src/gw_codecs.h): -O2, they run in the mixer
$(AB)/src/codecs/%.c.o: src/codecs/%.c src/codecs/gwc_priv.h src/gw_codecs.h
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@$(CROSS)gcc -c $(MCU) -O2 -ffunction-sections -fdata-sections -w $< -o $@

$(AB)/boot.o $(AB)/platform_core.o: $(AB)/%.o: src/%.c src/platform.h src/svm_xip.h $(REDEF)
	@mkdir -p $(dir $@)
	$(CROSS)gcc -c $(ARM_C) $< -o $@
	$(CROSS)objcopy --redefine-syms=$(REDEF) $@

$(AB)/osk.o: src/osk.c src/osk.h src/font_cp437.h
	@mkdir -p $(dir $@)
	$(CROSS)gcc -c $(ARM_C) $< -o $@

$(AB)/heap.o: src/heap.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc -c $(MCU) -O2 -Wall -fno-builtin -Isrc $< -o $@   # -fno-builtin: else malloc+memset becomes a call to calloc

$(AB)/tlsf.o: src/third_party/tlsf/tlsf.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc -c $(MCU) -O2 -DNDEBUG $< -o $@

$(AB)/gw_core_bridge.c: sdk/src/gw_core_bridge.c tools/bridge_for_newlib.py
	@mkdir -p $(dir $@)
	python3 tools/bridge_for_newlib.py $< $@

$(AB)/gw_core_bridge.o: $(AB)/gw_core_bridge.c
	$(CROSS)gcc -c $(ARM_C) -DGW_CORE_BRIDGE_DISABLE_SDK_MEMOPS -DGW_CORE_BRIDGE_DISABLE_SDK_MEMCPY \
	    -DGW_CORE_BRIDGE_DISABLE_SDK_MEMMOVE -DGW_CORE_BRIDGE_DISABLE_SDK_MEMSET -DGW_CORE_BRIDGE_DISABLE_SDK_MALLOC $< -o $@

$(AB)/gw_core_entry.o: sdk/src/gw_core_entry.S
	@mkdir -p $(dir $@)
	$(CROSS)gcc -x assembler-with-cpp -c $(MCU) $(SDKDEFS) $< -o $@

$(AB)/scummvm.elf: $(ARM_OWN) $(ARM_SVM) ld/scummvm_core.ld ld/hot_objects.ld
	@echo "  LINK $@"
	@$(CROSS)g++ $(MCU) -nostartfiles -T ld/scummvm_core.ld -Lsdk/ld -Lld \
	    -Wl,--gc-sections -Wl,--emit-relocs -Wl,--no-check-sections -Wl,-Map=$(AB)/scummvm.map \
	    $(ARM_OWN) $(ARM_SVM) -Wl,--start-group -lsupc++ -lc -lm -lgcc -Wl,--end-group -o $@

$(OUT): $(AB)/scummvm.elf tools/xip_pack.py assets/header.png assets/pad.png
	@mkdir -p $(dir $@)
	$(CROSS)objcopy -O binary --only-section=.core_entry --only-section=.data $< $(AB)/ram.bin
	python3 sdk/tools/pack_core.py --elf $< --bin $(AB)/ram.bin \
	    --system-name "ScummVM" --dirname scummvm --extensions "scummvm" \
	    --header-logo assets/header.png --pad-logo assets/pad.png --logo-invert \
	    --core-name "ScummVM (LucasArts)" --version 0.1.0 \
	    --nm $(CROSS)nm --objcopy $(CROSS)objcopy --out $(AB)/packed.bin
	python3 tools/xip_pack.py --elf $< --core $(AB)/packed.bin --build-id $(BUILD_ID) --out $@

clean:
	rm -rf build out
