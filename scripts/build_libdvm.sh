#!/usr/bin/env bash
# Build devkitPro's libdvm with its FatFs into ./portlibs/dvm, with exFAT and GPT turned on. GodModeU
# mounts FAT32 / exFAT USB drives with it, reading their sectors through Mocha (source/usbfat.c),
# because the Wii U's own FAT driver won't mount USB drives. The same pins GodModeWii's exFAT build uses.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PFX="$ROOT/portlibs/dvm"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
CC="$DEVKITPPC/bin/powerpc-eabi-gcc"
AR="$DEVKITPPC/bin/powerpc-eabi-ar"
[ -d "$ROOT/portlibs/mocha/include" ] || { echo "Run scripts/build_libmocha.sh first" >&2; exit 1; }

git clone --quiet https://github.com/devkitPro/libdvm.git "$WORK/libdvm"
git -C "$WORK/libdvm" checkout --quiet 3006b93714
git clone --quiet https://github.com/devkitPro/fatfs-mod.git "$WORK/fatfs"
git -C "$WORK/fatfs" checkout --quiet 6d3e8f8

CONF="$WORK/fatfs/source/ffconf.h"
for opt in FF_FS_EXFAT FF_LBA64; do
    sed -i -E "s/^(#define $opt[[:space:]]+)0$/\11/" "$CONF"
    grep -Eq "^#define $opt[[:space:]]+1$" "$CONF" || { echo "$opt not found in ffconf.h" >&2; exit 1; }
done

# libdvm takes <disc_io.h> on platforms it doesn't know; libmocha's header has the same DISC_INTERFACE.
mkdir -p "$WORK/shim" "$WORK/obj"
echo '#include <mocha/disc_interface.h>' > "$WORK/shim/disc_io.h"

CFLAGS="-O2 -Wall -mcpu=750 -meabi -mhard-float -ffunction-sections -fdata-sections -D__WIIU__ -D__WUT__"
CFLAGS="$CFLAGS -DLIBDVM_BUFFER_ALIGN=64 -I$DEVKITPRO/wut/include -I$ROOT/portlibs/mocha/include"
CFLAGS="$CFLAGS -I$WORK/shim -I$WORK/libdvm/include -I$WORK/fatfs/source"
# fat_wrappers.c and dvm_libogc.c are the libogc entry points (fatInitDefault, dvmInit); GodModeU
# creates and mounts its discs itself.
for src in "$WORK"/libdvm/source/{dvm_disc,dvm_cache,dvm_volume,dvm_prober,fat_driver}.c \
           "$WORK"/fatfs/source/{ff,ffunicode}.c; do
    "$CC" $CFLAGS -c "$src" -o "$WORK/obj/$(basename "$src" .c).o"
done

rm -rf "$PFX"
mkdir -p "$PFX/lib" "$PFX/include"
"$AR" rcs "$PFX/lib/libdvm.a" "$WORK"/obj/*.o
cp "$WORK/libdvm/include/dvm.h" "$WORK/shim/disc_io.h" "$PFX/include/"

echo "Built libdvm with exFAT in $PFX"
