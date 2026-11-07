#!/usr/bin/env bash
# Refresh the vendored firmware SDK in sdk/ from a Game & Watch Retro-Go SD checkout of the
# external_cores branch (use sylverb's own branch, not a fork that may carry unmerged work):
#
#   scripts/sync_sdk.sh /path/to/game-and-watch-retro-go-sd
#
# The checkout needs its FatFs submodule and the ST drivers ("make download_sdk" there).
# It copies what the Makefile uses -- the headers the core bridge and src/platform_core.c pull in
# (found with gcc -M), the bridge sources, the linker scripts and pack_core.py -- and writes
# SDK_VERSION. The ST
# Drivers/ headers are not sylverb's code: unless the drivers themselves are being updated,
# keep the committed ones afterwards with `git checkout -- sdk/include/Drivers`.
set -euo pipefail
[[ $# -eq 1 ]] || { echo "Usage: $0 /path/to/game-and-watch-retro-go-sd" >&2; exit 1; }
FW=$(cd "$1" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK="$ROOT/sdk"
CC_DIR="$FW/Core/Src/porting/core_common"
[[ -f "$CC_DIR/gw_core_bridge.c" && -f "$FW/tools/pack_core.py" ]] || { echo "not an external_cores checkout: $FW" >&2; exit 1; }
[[ -f "$FW/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal.h" ]] || { echo "no ST drivers: run 'make download_sdk' in $FW" >&2; exit 1; }
[[ -f "$FW/Core/Src/porting/lib/FatFs/ff.h" ]] || { echo "no FatFs: git submodule update --init Core/Src/porting/lib/FatFs" >&2; exit 1; }
GCC="${GCC_PATH:+$GCC_PATH/}arm-none-eabi-gcc"

INC=()
for d in Core/Inc Core/Inc/retro-go Core/Inc/porting Core/Src/porting/core_common Core/Src/porting/lib \
         Core/Src/porting/lib/FatFs Drivers/STM32H7xx_HAL_Driver/Inc Drivers/STM32H7xx_HAL_Driver/Inc/Legacy \
         Drivers/CMSIS/Device/ST/STM32H7xx/Include Drivers/CMSIS/Include; do INC+=(-I"$FW/$d"); done
DEFS=(-DUSE_HAL_DRIVER -DSTM32H7B0xx -DSD_CARD=1 -DGNW_DISABLE_COMPRESSION -DIS_LITTLE_ENDIAN
      -DCOVERFLOW=0 -DCHEAT_CODES=0 -DGNW_CORE_ENTRY_TARGET=app_main_scummvm)
MCU=(-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)

rm -rf "$SDK/include" "$SDK/src" "$SDK/ld" "$SDK/tools"
mkdir -p "$SDK/include" "$SDK/src" "$SDK/ld" "$SDK/tools"
for src in "$CC_DIR/gw_core_bridge.c" "$CC_DIR/gw_core_i18n.c" "$ROOT/src/platform_core.c" "$ROOT/src/boot.c"; do
  "$GCC" -M "${DEFS[@]}" "${INC[@]}" -I"$ROOT/src" "${MCU[@]}" "$src"
done | tr ' ' '\n' | grep -E '\.(h|hpp)$' | sort -u | while IFS= read -r h; do
  case "$h" in
    "$FW"/Core/Src/porting/core_common/*) ;;          # the bridge headers go to sdk/src
    "$FW"/*) rel="${h#$FW/}"; mkdir -p "$SDK/include/$(dirname "$rel")"; cp "$h" "$SDK/include/$rel" ;;
  esac
done
for f in gw_core_bridge.c gw_core_bridge.h gw_core_entry.S gw_core_bridge_redefine_syms.txt \
         gw_core_i18n.c gw_core_i18n.h; do
  cp "$CC_DIR/$f" "$SDK/src/"
done
cp "$FW"/ld/gnw_*.ld "$SDK/ld/"
cp "$FW/tools/pack_core.py" "$SDK/tools/"

ABI=$(grep -E '#define\s+GW_FIRMWARE_ABI_VERSION' "$SDK/include/Core/Inc/retro-go/gw_firmware_abi.h" | awk '{print $3}' | tr -d 'u')
SRC="$FW"
if git -C "$FW" rev-parse --git-dir >/dev/null 2>&1; then
  URL=$(git -C "$FW" remote get-url origin 2>/dev/null | sed -E 's#^https://github.com/##; s#^git@github.com:##; s#\.git$##')
  SRC="${URL:-$FW}, branch $(git -C "$FW" rev-parse --abbrev-ref HEAD), commit $(git -C "$FW" rev-parse --short=8 HEAD)"
fi
cat > "$ROOT/SDK_VERSION" <<META
# Snapshot of the firmware SDK in sdk/ (written by scripts/sync_sdk.sh).
FIRMWARE_ABI_VERSION=$ABI
SOURCE=$SRC
SYNC_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)
META
echo "sdk/ synced from $SRC (ABI v$ABI). Review git diff before committing."
