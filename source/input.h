#ifndef INPUT_H
#define INPUT_H

#include "gmu_types.h"

/* Every controller is folded into the BTN_* bits below, so the UI sees one scheme:
 *   D-Pad          arrows           A  open / confirm      B  back / cancel
 *   X              actions menu     Y  mark and move down  + / R / ZR  copy
 *   - / L / ZL     cut              HOME  system menu
 * Values match the Wii Remote's WPAD_BUTTON_* bits. */
#define BTN_LEFT   0x0001
#define BTN_RIGHT  0x0002
#define BTN_DOWN   0x0004
#define BTN_UP     0x0008
#define BTN_PLUS   0x0010
#define BTN_2      0x0100   /* actions menu */
#define BTN_1      0x0200   /* mark */
#define BTN_B      0x0400
#define BTN_A      0x0800
#define BTN_MINUS  0x1000
#define BTN_HOME   0x8000

#define IN_DOWN 0
#define IN_HELD 1

void input_init(void);
void input_shutdown(void);

/* Presents the screen, waits one frame, and returns the buttons that were pressed
 * (IN_DOWN) or are held (IN_HELD). */
u32 DetectInput(u8 down_or_held);

/* Edge-triggered presses plus auto-repeat on the D-Pad, for scrolling. */
u32 input_pressed(void);

/* Like DetectInput(IN_DOWN) but without waiting for a frame. */
u32 input_poll_nowait(void);

/* Non-blocking check used by long operations to allow cancelling. */
bool input_cancel_held(void);

#endif
