#ifndef LAUNCH_H
#define LAUNCH_H

#include <stdbool.h>

/* HOME > Launch. True: something was picked; leave the app with EXIT_LAUNCH, and main calls launch_now. */
bool launch_menu(void);
/* A Wii U program on the SD card (sd:/...wuhb or .rpx), started through RPXLoader as launch_menu's are. */
bool launch_program(const char *sd_path);
/* Once the drives are let go, ProcUI still running. False when it couldn't start (the Wii U Menu then). */
bool launch_now(void);
/* At start-up: the user's own sd:/hbl2hbc.txt back, if a launch through hbl2hbc replaced it. */
void launch_restore(void);

#endif
