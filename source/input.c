/*
 * Input for the Wii U through libwiiuinput (github.com/quatric/libwiiuinput), the Wii U port of the
 * libwiiinput that GodModeWii uses: the GamePad, Wii Remotes (with a Nunchuk or Classic Controller),
 * Wii U Pro Controllers, the GameCube controller adapter, USB game controllers, USB keyboards and
 * mice all arrive as WPAD_BUTTON_* bits:
 *   D-Pad          arrows           A  open / confirm      B  back / cancel
 *   X (2)          actions menu     Y (1)  mark and move down
 *   + / R / ZR     copy             - / L / ZL  cut        HOME  system menu
 * The left stick moves like the D-Pad. The GamePad's touch screen and the Wii Remote pointer drive the
 * cursor (pointer.c). Aroma's plugin menu combo (L + Down + Minus) opens the plugin menu over the app.
 */
#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <whb/proc.h>
#include <vpad/input.h>
#include <padscore/wpad.h>
#include <input_wiiu.h>

#include "input.h"
#include "pointer.h"
#include "ui.h"
#include "sfx.h"
#include "alarm.h"
#include "app.h"

void input_init(void)
{
    /* the pointer, touch screen and mouse drive the viewers' cursor */
    INPUT_InitEx(0);
    ptr_begin();
    /* a tap on the GamePad is A where the finger is: it picks a row, presses a button, draws */
    INPUT_SetTouchButton(WPAD_BUTTON_A);

    INPUT_KeyboardMapSet(0x4C, 0);                  /* Delete: unused here */
    INPUT_KeyboardMapSet(0x06, WPAD_BUTTON_PLUS);   /* C: copy */
    INPUT_KeyboardMapSet(0x19, WPAD_BUTTON_MINUS);  /* V: cut  */
}

void input_shutdown(void)
{
    INPUT_Shutdown();
}

#define DPAD (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT)

/* ---------------------------------------------------------------- left stick as a D-Pad */

static u32 stick_now, stick_before;

static void stick_update(void)
{
    float x = 0, y = 0;
    INPUT_Stick(INPUT_STICK_LEFT, &x, &y, NULL);
    stick_before = stick_now;
    u32 d = 0;
    /* a little hysteresis, so a stick resting near the edge doesn't flicker */
    float on = 0.6f, off = 0.45f;
    if (x < -((stick_now & WPAD_BUTTON_LEFT) ? off : on)) d |= WPAD_BUTTON_LEFT;
    if (x > ((stick_now & WPAD_BUTTON_RIGHT) ? off : on)) d |= WPAD_BUTTON_RIGHT;
    if (y > ((stick_now & WPAD_BUTTON_UP) ? off : on)) d |= WPAD_BUTTON_UP;
    if (y < -((stick_now & WPAD_BUTTON_DOWN) ? off : on)) d |= WPAD_BUTTON_DOWN;
    stick_now = d;
}

static u32 held_all(void) { return INPUT_ButtonsHeld() | stick_now; }

/* ---------------------------------------------------------------- Aroma's plugin menu combo */

/* As WiiUPluginLoaderBackend's VPADRead / WPADRead hooks see it, per controller. */
#define AROMA_VPAD    (VPAD_BUTTON_L | VPAD_BUTTON_DOWN | VPAD_BUTTON_MINUS)
#define AROMA_WIIMOTE (WPAD_BUTTON_B | WPAD_BUTTON_DOWN | WPAD_BUTTON_MINUS)
#define AROMA_CLASSIC (WPAD_CLASSIC_BUTTON_L | WPAD_CLASSIC_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_MINUS)
#define AROMA_PRO     (WPAD_PRO_TRIGGER_L | WPAD_PRO_BUTTON_DOWN | WPAD_PRO_BUTTON_MINUS)

static u32 gamepad_prev;
static bool combo_down;

static bool aroma_combo(void)
{
    bool combo = false;
    input_device d;
    for (int i = 0; i < INPUT_DeviceCount(); i++) {
        if (!INPUT_DeviceGet(i, &d))
            continue;
        if (d.kind == INPUT_DEV_GAMEPAD) {
            /* Aroma zeroes the GamePad sample where the combo completes, so two of its buttons held
             * just before an empty sample count too */
            u32 raw = d.raw & 0xFFFFF, prev = gamepad_prev;
            gamepad_prev = raw;
            combo |= raw == AROMA_VPAD || (!raw && !(prev & ~AROMA_VPAD) && __builtin_popcount(prev) == 2);
        } else if (d.kind == INPUT_DEV_PRO) {
            combo |= d.raw == AROMA_PRO;
        } else if (d.kind == INPUT_DEV_WIIMOTE) {
            combo |= d.raw_ext ? (d.raw_ext & 0xFFFF) == AROMA_CLASSIC : (d.raw & 0xFFFF) == AROMA_WIIMOTE;
        }
    }
    return combo;
}

static u32 detect(u8 DownOrHeld, bool wait)
{
    ptr_overlay_update();                           /* an arrow taken away by a redraw is back before the next frame */
    if (wait)
        VIDEO_WaitVSync();
    if (!WHBProcIsRunning())
        g_quit_request = true;

    INPUT_Update();
    stick_update();
    bool combo = aroma_combo();
    if (combo && !combo_down) {
        combo_down = true;
        ui_plugin_menu();
        INPUT_Update();
        return 0;
    }
    combo_down = combo;
    /* the combo is Aroma's, not a file manager command */
    if (combo)
        return 0;
    ui_hint_update();
    ptr_overlay_update();
    alarm_poll();
    if (DownOrHeld != IN_DOWN)
        return held_all();
    u32 down = INPUT_ButtonsDown() | (stick_now & ~stick_before);
    /* a click on a footer hint ("B Stop") is that button */
    u32 hint = ui_hint_under();
    if (hint && ((down & WPAD_BUTTON_A) || (INPUT_MouseButtonsPressed() & INPUT_MOUSE_LEFT)))
        down = (down & ~WPAD_BUTTON_A) | hint;
    return down;
}

u32 DetectInput(u8 DownOrHeld)
{
    return detect(DownOrHeld, true);
}

u32 input_poll_nowait(void)
{
    return detect(IN_DOWN, false);
}

/* For the start-up code reader: no cursor, hint or alarm work. */
u32 input_poll_raw(void)
{
    INPUT_Update();
    return INPUT_ButtonsDown();
}

u32 input_pressed(void)
{
    static u32 repeat_mask;
    static int hold_frames;

    u32 down = DetectInput(IN_DOWN);
    u32 held = held_all() & DPAD;
    if (down & WPAD_BUTTON_A) sfx_play(SFX_CLICK);
    else if (down & WPAD_BUTTON_B) sfx_play(SFX_LONGTAP);
    else if (down & WPAD_BUTTON_LEFT) sfx_play(SFX_PAGEUP);
    else if (down & WPAD_BUTTON_RIGHT) sfx_play(SFX_PAGEDOWN);
    else if (down & (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN)) sfx_play(SFX_UPDOWN);   /* TiVo mode only */

    if (!held || (down & DPAD)) {
        hold_frames = 0;
        repeat_mask = held;
        return down;
    }

    hold_frames++;
    if (hold_frames > 22 && (hold_frames % 3) == 0) {
        down |= held & repeat_mask;
        if (down & (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN)) sfx_play(SFX_UPDOWN);   /* a tick a step, like TiVo */
    }
    return down;
}

bool input_cancel_held(void)
{
    INPUT_Update();
    if (!WHBProcIsRunning())
        g_quit_request = true;
    return (INPUT_ButtonsHeld() & WPAD_BUTTON_B) != 0;
}

void input_flush(void)
{
    for (int i = 0; i < 3; i++)
        detect(IN_DOWN, true);
}
