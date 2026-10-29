#!/usr/bin/env bash
# Build extremscorner's libntfs (NTFS-3G 2026.9.28, GPL-2.0) into ./portlibs/ntfs for wut. GodModeU mounts
# NTFS USB drives with it, reading their sectors through Mocha (source/usbntfs.c). It's written for libogc2,
# so its disc calls are adapted here and a small shim stands in for the libogc headers it uses.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PFX="$ROOT/portlibs/ntfs"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
CC="$DEVKITPPC/bin/powerpc-eabi-gcc"
AR="$DEVKITPPC/bin/powerpc-eabi-ar"
[ -d "$ROOT/portlibs/mocha/include" ] || { echo "Run scripts/build_libmocha.sh first" >&2; exit 1; }

git clone --quiet https://github.com/extremscorner/libntfs.git "$WORK/libntfs"
git -C "$WORK/libntfs" checkout --quiet f73a4d1
SRC="$WORK/libntfs/source"

# libogc2 passes the interface to every disc call and has flush(); libmocha's DISC_INTERFACE does neither,
# and its raw FSA writes finish before returning, so there's nothing to flush.
sed -i -E 's/->(readSectors|writeSectors)\(((fd|cache)->)?(interface|disc), ?/->\1(/' "$SRC"/*.c
sed -i -E 's/->(startup|isInserted|shutdown)\(interface\)/->\1()/' "$SRC"/*.c
sed -i -E 's/[a-z]+->(interface|disc)->flush\([a-z]+->(interface|disc)\)/true/' "$SRC"/*.c
if grep -nE '->(readSectors|writeSectors|startup|isInserted|shutdown|flush)\(((fd|cache)->)?(interface|disc) ?[,)]|->flush\(' "$SRC"/*.c; then
    echo "libogc2-style disc calls left over" >&2
    exit 1
fi
# No built-in interfaces on the Wii U: GodModeU hands it its own.
sed -i '/^#elif defined(__gamecube__)/,/^#endif/ s/^#endif/#else\nconst INTERFACE_ID ntfs_disc_interfaces[] = { { NULL, NULL } };\n#endif/' "$SRC"/ntfsinternal.c

mkdir -p "$WORK/shim/ogc" "$WORK/obj"
cat > "$WORK/shim/gccore.h" <<'EOF'
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <coreinit/mutex.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
/* BOOL, TRUE and FALSE come from wut_types.h. */
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
/* libogc's mutex calls, on Cafe OS mutexes. */
typedef OSMutex *mutex_t;
static inline int LWP_MutexInit(mutex_t *m, bool recursive)
{
    (void)recursive;
    *m = malloc(sizeof(OSMutex));
    if (!*m) return -1;
    OSInitMutex(*m);
    return 0;
}
static inline int LWP_MutexLock(mutex_t m) { OSLockMutex(m); return 0; }
static inline int LWP_MutexUnlock(mutex_t m) { OSUnlockMutex(m); return 0; }
static inline int LWP_MutexDestroy(mutex_t m) { free(m); return 0; }
#define SYS_IsDMAAddress(p, a) ((((uintptr_t)(p)) & ((a) - 1)) == 0)
/* Only for WSL device files, which a console never makes. */
#define makedev(ma, mi) ((((ma) & 0xfff) << 8) | ((mi) & 0xff))
#define major(d) (((d) >> 8) & 0xfff)
#define minor(d) ((d) & 0xff)
EOF
echo '#include <gccore.h>' > "$WORK/shim/gctypes.h"
printf '#pragma once\n#include <gccore.h>\n#include <mocha/disc_interface.h>\n' > "$WORK/shim/ogc/disc_io.h"

CFLAGS="-O2 -Wall -Wno-unused -DGEKKO -D__WIIU__ -D__WUT__ -DHAVE_CONFIG_H -mcpu=750 -meabi -mhard-float"
CFLAGS="$CFLAGS -ffunction-sections -fdata-sections -include $WORK/shim/gccore.h -I$WORK/shim"
CFLAGS="$CFLAGS -I$WORK/libntfs/include -I$SRC -I$DEVKITPRO/wut/include -I$ROOT/portlibs/mocha/include"
for src in "$SRC"/*.c; do
    "$CC" $CFLAGS -c "$src" -o "$WORK/obj/$(basename "$src" .c).o"
done

rm -rf "$PFX"
mkdir -p "$PFX/lib" "$PFX/include"
"$AR" rcs "$PFX/lib/libntfs.a" "$WORK"/obj/*.o
cp "$WORK/libntfs/include/ntfs.h" "$PFX/include/"
cp -r "$WORK/shim/gccore.h" "$WORK/shim/gctypes.h" "$WORK/shim/ogc" "$PFX/include/"

echo "Built libntfs in $PFX"
