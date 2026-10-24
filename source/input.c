/*
 * Input for the Wii U: the GamePad (VPAD) and any Wii Remote or Classic
 * Controller or Wii U Pro Controller paired with the console (KPAD). Both are folded into BTN_* bits.
 * The Classic Controller mapping follows GlowWii's (see README).
 */
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>
#include <whb/proc.h>

#include "input.h"
#include "ui.h"
#include "app.h"

#define DPAD (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)
#define WIIMOTE_BUTTONS (BTN_LEFT | BTN_RIGHT | BTN_DOWN | BTN_UP | BTN_PLUS | BTN_2 | BTN_1 | \
                         BTN_B | BTN_A | BTN_MINUS | BTN_HOME)

static bool vpad_ready;
static bool kpad_ready;

static u32 map_vpad(u32 b)
{
    u32 out = 0;
    if (b & VPAD_BUTTON_UP)    out |= BTN_UP;
    if (b & VPAD_BUTTON_DOWN)  out |= BTN_DOWN;
    if (b & VPAD_BUTTON_LEFT)  out |= BTN_LEFT;
    if (b & VPAD_BUTTON_RIGHT) out |= BTN_RIGHT;
    if (b & VPAD_BUTTON_A)     out |= BTN_A;
    if (b & VPAD_BUTTON_B)     out |= BTN_B;
    if (b & VPAD_BUTTON_X)     out |= BTN_2;
    if (b & VPAD_BUTTON_Y)     out |= BTN_1;
    if (b & (VPAD_BUTTON_PLUS | VPAD_BUTTON_R | VPAD_BUTTON_ZR))     out |= BTN_PLUS;
    if (b & (VPAD_BUTTON_MINUS | VPAD_BUTTON_L | VPAD_BUTTON_ZL))    out |= BTN_MINUS;
    if (b & VPAD_BUTTON_HOME)  out |= BTN_HOME;
    return out;
}

static u32 map_classic(u32 b)
{
    u32 out = 0;
    if (b & WPAD_CLASSIC_BUTTON_UP)    out |= BTN_UP;
    if (b & WPAD_CLASSIC_BUTTON_DOWN)  out |= BTN_DOWN;
    if (b & WPAD_CLASSIC_BUTTON_LEFT)  out |= BTN_LEFT;
    if (b & WPAD_CLASSIC_BUTTON_RIGHT) out |= BTN_RIGHT;
    if (b & WPAD_CLASSIC_BUTTON_A)     out |= BTN_A;
    if (b & WPAD_CLASSIC_BUTTON_B)     out |= BTN_B;
    if (b & WPAD_CLASSIC_BUTTON_X)     out |= BTN_2;
    if (b & WPAD_CLASSIC_BUTTON_Y)     out |= BTN_1;
    if (b & (WPAD_CLASSIC_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_R | WPAD_CLASSIC_BUTTON_ZR))  out |= BTN_PLUS;
    if (b & (WPAD_CLASSIC_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_L | WPAD_CLASSIC_BUTTON_ZL)) out |= BTN_MINUS;
    if (b & WPAD_CLASSIC_BUTTON_HOME)  out |= BTN_HOME;
    return out;
}

static u32 map_pro(u32 b)
{
    u32 out = 0;
    if (b & WPAD_PRO_BUTTON_UP)    out |= BTN_UP;
    if (b & WPAD_PRO_BUTTON_DOWN)  out |= BTN_DOWN;
    if (b & WPAD_PRO_BUTTON_LEFT)  out |= BTN_LEFT;
    if (b & WPAD_PRO_BUTTON_RIGHT) out |= BTN_RIGHT;
    if (b & WPAD_PRO_BUTTON_A)     out |= BTN_A;
    if (b & WPAD_PRO_BUTTON_B)     out |= BTN_B;
    if (b & WPAD_PRO_BUTTON_X)     out |= BTN_2;
    if (b & WPAD_PRO_BUTTON_Y)     out |= BTN_1;
    if (b & (WPAD_PRO_BUTTON_PLUS | WPAD_PRO_BUTTON_R | WPAD_PRO_BUTTON_ZR))  out |= BTN_PLUS;
    if (b & (WPAD_PRO_BUTTON_MINUS | WPAD_PRO_BUTTON_L | WPAD_PRO_BUTTON_ZL)) out |= BTN_MINUS;
    if (b & WPAD_PRO_BUTTON_HOME)  out |= BTN_HOME;
    return out;
}

/* Every connected controller, edge (down) or level (held). */
static u32 read_pads(bool down)
{
    u32 out = 0;
    if (vpad_ready) {
        VPADStatus st;
        VPADReadError err = VPAD_READ_SUCCESS;
        VPADRead(VPAD_CHAN_0, &st, 1, &err);
        if (err == VPAD_READ_SUCCESS)
            out |= map_vpad(down ? st.trigger : st.hold);
    }
    if (kpad_ready) {
        for (int chan = WPAD_CHAN_0; chan <= WPAD_CHAN_3; chan++) {
            KPADStatus ks;
            if (KPADRead(chan, &ks, 1) != KPAD_ERROR_OK)
                continue;
            if (ks.extensionType == WPAD_EXT_PRO_CONTROLLER) {
                /* the Pro Controller reports through its own bits, not the Wii Remote's */
                out |= map_pro(down ? ks.pro.trigger : ks.pro.hold);
                continue;
            }
            out |= (down ? ks.trigger : ks.hold) & WIIMOTE_BUTTONS;
            if (ks.extensionType == WPAD_EXT_CLASSIC)
                out |= map_classic(down ? ks.classic.trigger : ks.classic.hold);
        }
    }
    return out;
}

void input_init(void)
{
    VPADInit();
    vpad_ready = true;
    KPADInit();
    kpad_ready = true;
}

void input_shutdown(void)
{
    vpad_ready = false;
    kpad_ready = false;
}

/* Shows what was drawn, waits a frame, and reads the controllers. */
static u32 detect(bool down, bool wait)
{
    ui_present();
    if (wait)
        OSSleepTicks(OSMillisecondsToTicks(16));
    if (!WHBProcIsRunning())
        g_quit_request = true;
    return read_pads(down);
}

u32 DetectInput(u8 down_or_held)
{
    return detect(down_or_held == IN_DOWN, true);
}

u32 input_poll_nowait(void)
{
    return detect(true, false);
}

u32 input_pressed(void)
{
    static u32 repeat_mask;
    static int hold_frames;

    u32 down = DetectInput(IN_DOWN);
    u32 held = read_pads(false) & DPAD;

    /* DetectInput waits a frame; the held state is read right after it. */
    if (!held || (down & DPAD)) {
        hold_frames = 0;
        repeat_mask = held;
        return down;
    }

    hold_frames++;
    if (hold_frames > 22 && (hold_frames % 3) == 0)
        down |= held & repeat_mask;
    return down;
}

bool input_cancel_held(void)
{
    return (read_pads(false) & BTN_B) != 0;
}
