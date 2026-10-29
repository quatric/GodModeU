#ifndef USBFAT_H
#define USBFAT_H

#include <stdbool.h>
#include <coreinit/filesystem_fsa.h>

enum { USBFAT_NONE, USBFAT_FAT, USBFAT_EXFAT, USBFAT_EXT2, USBFAT_EXT3, USBFAT_EXT4 };

/* Mounts /dev/usbNN (slot from 0) as the devoptab `name` if it holds a FAT32, exFAT or ext2/3/4 volume
 * (ext read-only), and says which. The client must be unlocked by Mocha. Never call it for a slot Cafe OS
 * mounted as WFS. */
int usbfat_mount(FSAClientHandle client, int slot, const char *name);
void usbfat_unmount(const char *name);

#endif
