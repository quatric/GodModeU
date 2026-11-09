#ifndef WIIU_INSTALL_H
#define WIIU_INSTALL_H

#include <stdbool.h>

/* Installing Wii U titles (wiiu_install.c, wud.c):
 *
 *  WUP / NUS folders (title.tmd, title.tik, title.cert and the .app / .h3 contents), installed through
 *  the system's MCP installer the way WUP Installer GX2 does it, to the internal storage (MLC) or a
 *  Wii U USB drive. Unsigned titles need signature patches (Aroma with Mocha / the sigpatches plugin).
 *
 *  WUD / WUX disc images (wudump's game.wud / game.wux with its game.key): the game partition (GM) is
 *  written out as a WUP folder (as wud2app does) and that folder is installed like any other. The Wii U
 *  common key is read from sd:/wiiu/apps/GodModeU/common.key (16 bytes, or 32 hex digits) or from an
 *  OTP dump (otp.bin) there or in sd:/. */

/* A folder holding title.tmd and title.tik (and so a title to install). */
bool wup_is_folder(const char *path);
/* Asks where to (MLC / USB), then installs each folder; shows the result. */
void wup_install_paths(char **paths, int n);

/* .wud / .wux by name */
bool wud_is_name(const char *name);
/* What to do with a disc image: install it, extract the WUP to a drive, look at its partitions. */
void wud_menu(const char *path);

/* Help pages (SYSTEM menu). */
void nand_backup_help(void);
void wup_help(void);

#endif
