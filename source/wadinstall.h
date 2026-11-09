#ifndef WADINSTALL_H
#define WADINSTALL_H

/* GodModeWii installs WADs through IOS's ES and signs data.bin files with the Wii's own keys. Neither is
 * reachable from a Cafe OS app, so on the Wii U these say why and do nothing (wiiu/gx_stubs.c): WADs open
 * as archives (decrypted with the common keys) and saves export as folders of plain files. */

#include <stdbool.h>
#include <stddef.h>
#include <gccore.h>
#include "wad2bin.h"

int wad_install_paths(char **paths, int n);
int wad_install_sd(char **paths, int n);
bool wad_console_keys(w2b_keys_t *k, char *why, size_t wn);
int wad_backup_save(u64 tid, char *where, size_t wsz, u32 *files_before);

#endif
