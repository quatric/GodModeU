#ifndef USBNTFS_H
#define USBNTFS_H

#include <stdbool.h>
#include <coreinit/filesystem_fsa.h>

enum { USBNTFS_NONE, USBNTFS_MOUNTED, USBNTFS_DIRTY, USBNTFS_FAILED };

/* Mounts /dev/usbNN (slot from 0) as the devoptab `name` if it holds an NTFS volume. The client must be
 * unlocked by Mocha. USBNTFS_DIRTY: Windows didn't shut down fully (or hibernated), so it's left alone. */
int usbntfs_mount(FSAClientHandle client, int slot, const char *name);
void usbntfs_unmount(int slot, const char *name);
/* Mounts the NTFS partition starting at sector `start` of drive `drive` as `name`. `slot` is the free
 * entry that carries the connection (and is passed to usbntfs_unmount later); it may differ from `drive`. */
int usbntfs_mount_part(FSAClientHandle client, int drive, int slot, unsigned start, const char *name);

#endif
