#ifndef GMU_SCREEN_H
#define GMU_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

/* The picture GodModeU draws is a 640 x 480 YUY2 framebuffer, the Wii's own layout, so the text
 * console and the viewers shared with GodModeWii draw into it unchanged. A thread on its own core
 * shows it on both screens about 60 times a second: on the TV scaled by 1.5 (960 x 720, centered),
 * on the GamePad 1:1 (centered in 854 x 480). Only the rows that changed are converted again. */

#define SCREEN_W 640
#define SCREEN_H 480
/* where the 640 x 480 picture sits on the GamePad's 854 x 480 screen */
#define SCREEN_DRC_X0 107

/* OSScreen in MEM1 (taken back by the system while the app is in the background), the present
 * thread. False if the screen memory could not be had. */
bool screen_init(void);
void screen_shutdown(void);

/* The framebuffer everything draws into. */
uint8_t *screen_xfb(void);
/* Shows another YUY2 buffer of the same size instead (video frames); NULL goes back to the xfb. */
void screen_show(const uint8_t *yuy2);
/* The colour around the picture (the theme's background, as a YUY2 pixel pair). */
void screen_set_border(uint32_t yuy2_pair);
/* Fills the whole framebuffer with one YUY2 pixel pair. */
void screen_clear(uint32_t yuy2_pair);
/* Waits for the next frame to be shown (VIDEO_WaitVSync). */
void screen_vsync(void);
/* Shows the framebuffer now, from the calling thread (before a long blocking call). */
void screen_flush(void);

/* Lets Aroma's plugin menu (L + Down + Minus) open over the app, then redraws. */
void screen_plugin_menu(void);

/* GX2 on both screens for the system keyboard, which can't draw through OSScreen: begin holds the picture
 * and sets GX2 up (false if it couldn't), each frame clears, calls draw_tv / draw_drc and shows them, and
 * end gives the screens back to the picture. Only from the thread that called screen_init(). */
bool screen_gx2_begin(void);
void screen_gx2_frame(void (*draw_tv)(void), void (*draw_drc)(void));
void screen_gx2_end(void);

#endif
