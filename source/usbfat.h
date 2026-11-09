#ifndef USBFAT_H
#define USBFAT_H

#include <stdbool.h>
#include <coreinit/filesystem_fsa.h>
#include "gmu_types.h"

enum { USBFAT_NONE, USBFAT_FAT, USBFAT_EXFAT, USBFAT_EXT2, USBFAT_EXT3, USBFAT_EXT4 };

/* Mounts /dev/usbNN (slot from 0) as the devoptab `name` if it holds a FAT32, exFAT or ext2/3/4 volume
 * (ext read-only), and says which. The client must be unlocked by Mocha. Never call it for a slot Cafe OS
 * mounted as WFS. */
int usbfat_mount(FSAClientHandle client, int slot, const char *name);
void usbfat_unmount(const char *name);

/* The other partitions of a drive: primary MBR / GPT partitions holding FAT32, exFAT or NTFS, in table order. */
enum { PART_FAT, PART_EXFAT, PART_NTFS };
#define USBFAT_MAX_PARTS 16
typedef struct {
    u32 start;
    int kind;
} usbfat_part;
int usbfat_list_parts(FSAClientHandle client, int slot, usbfat_part *out, int max);
/* Mounts the FAT32 / exFAT partition starting at sector `start` of drive `slot` as the devoptab `name`. */
bool usbfat_mount_part(FSAClientHandle client, int slot, u32 start, const char *name);

#endif
