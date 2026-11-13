#ifndef UPDATE_H
#define UPDATE_H

/* Self-update through libwiiuupdate: a background check of GitHub releases, and a dialog when a
 * newer GodModeU is out. */
void update_start(void);

/* Call from the main loop. 0: nothing happened, 1: a dialog was shown (redraw the screen),
 * 2: an update was installed and the program should leave to the Wii U Menu. */
int update_poll(void);

/* Stops any check or download; call before the network goes down and before main returns. */
void update_shutdown(void);

#endif
