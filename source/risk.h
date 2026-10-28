#ifndef RISK_H
#define RISK_H

#include <stdbool.h>
#include "gmu_types.h"

/* Anything that changes the protected storage (the Wii U's internal MLC and both
 * NANDs) can brick the console, so the first attempt on each drive shows a warning
 * and asks for the Konami code. Each drive asks once per session. There is no menu
 * to visit first: the prompt appears when you try the risky thing (delete, paste,
 * rename, ...). */

bool risk_any_unlocked(void);
bool risk_dev_unlocked(int dev);
/* Unlocked and, for the Wii U NAND, its warning accepted: what a write needs. */
bool risk_dev_writable(int dev);
bool risk_forced(void);
void risk_lock(void);
/* Warning + Konami code if this protected drive is not unlocked yet. */
bool risk_ensure_unlocked(int dev);

/* Gate for any modifying operation on `path`: asks for the Konami code when
 * needed. Returns false if the user cancelled. */
bool risk_authorize(const char *path, const char *what);

/* Startup-only helpers (see main.c). */
void risk_load_forced(void);
/* Listens for the Konami code until `min_ms` after `began_ms`; true if it went in. */
bool risk_startup_poll(u32 min_ms, u64 began_ms);
bool risk_toggle_forced(void);

#endif
