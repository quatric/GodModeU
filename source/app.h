#ifndef APP_H
#define APP_H

#include <stdbool.h>

#define GMW_VERSION "0.4.0"

enum { EXIT_LOADER, EXIT_MENU };

extern volatile bool g_quit_request;

/* Runs the interface until the user leaves; returns an EXIT_* code. */
int app_run(void);

/* Outside the Homebrew Launcher every exit goes to the Wii U Menu. */
bool launched_from_hbl(void);

#endif
