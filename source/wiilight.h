#ifndef WIILIGHT_H
#define WIILIGHT_H

/* GodModeWii flashes the Wii's disc slot light. The Wii U has no light a homebrew app can drive, so
 * these do nothing here (the calls stay so the shared code reads the same). */

#include <stdbool.h>

enum { WL_BUSH_WARBLER = 0, WL_DISC_OPEN = 2, WL_DISC_CLOSE = 3, WL_BLINK3 = 4 };

static inline void wiilight_init(void) {}
static inline void wiilight_load_setting(void) {}
static inline void wiilight_shutdown(void) {}
static inline void wiilight_off(void) {}
static inline void wiilight_glow(int level) { (void)level; }
static inline void wiilight_pulse(void) {}
static inline void wiilight_pattern(int id) { (void)id; }
static inline void wiilight_breathe(bool on) { (void)on; }
static inline bool wiilight_enabled(void) { return false; }
static inline void wiilight_set_enabled(bool on) { (void)on; }

#endif
