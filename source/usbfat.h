#ifndef USBFAT_H
#define USBFAT_H

#include <stdbool.h>
#include <coreinit/filesystem_fsa.h>

/* Mounts /dev/usbNN (slot from 0) as the devoptab `name` if it holds a FAT32 or exFAT volume.
 * The client must be unlocked by Mocha. Never call it for a slot Cafe OS mounted as WFS. */
bool usbfat_mount(FSAClientHandle client, int slot, const char *name, bool *exfat);
void usbfat_unmount(const char *name);

#endif
