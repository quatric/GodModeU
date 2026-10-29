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
    /* The left stick moves like the D-Pad. */
    if (b & (VPAD_BUTTON_UP | VPAD_STICK_L_EMULATION_UP))       out |= BTN_UP;
    if (b & (VPAD_BUTTON_DOWN | VPAD_STICK_L_EMULATION_DOWN))   out |= BTN_DOWN;
    if (b & (VPAD_BUTTON_LEFT | VPAD_STICK_L_EMULATION_LEFT))   out |= BTN_LEFT;
    if (b & (VPAD_BUTTON_RIGHT | VPAD_STICK_L_EMULATION_RIGHT)) out |= BTN_RIGHT;
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
    if (b & (WPAD_CLASSIC_BUTTON_UP | WPAD_CLASSIC_STICK_L_EMULATION_UP))       out |= BTN_UP;
    if (b & (WPAD_CLASSIC_BUTTON_DOWN | WPAD_CLASSIC_STICK_L_EMULATION_DOWN))   out |= BTN_DOWN;
    if (b & (WPAD_CLASSIC_BUTTON_LEFT | WPAD_CLASSIC_STICK_L_EMULATION_LEFT))   out |= BTN_LEFT;
    if (b & (WPAD_CLASSIC_BUTTON_RIGHT | WPAD_CLASSIC_STICK_L_EMULATION_RIGHT)) out |= BTN_RIGHT;
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
    if (b & (WPAD_PRO_BUTTON_UP | WPAD_PRO_STICK_L_EMULATION_UP))       out |= BTN_UP;
    if (b & (WPAD_PRO_BUTTON_DOWN | WPAD_PRO_STICK_L_EMULATION_DOWN))   out |= BTN_DOWN;
    if (b & (WPAD_PRO_BUTTON_LEFT | WPAD_PRO_STICK_L_EMULATION_LEFT))   out |= BTN_LEFT;
    if (b & (WPAD_PRO_BUTTON_RIGHT | WPAD_PRO_STICK_L_EMULATION_RIGHT)) out |= BTN_RIGHT;
    if (b & WPAD_PRO_BUTTON_A)     out |= BTN_A;
    if (b & WPAD_PRO_BUTTON_B)     out |= BTN_B;
    if (b & WPAD_PRO_BUTTON_X)     out |= BTN_2;
    if (b & WPAD_PRO_BUTTON_Y)     out |= BTN_1;
    if (b & (WPAD_PRO_BUTTON_PLUS | WPAD_PRO_BUTTON_R | WPAD_PRO_BUTTON_ZR))  out |= BTN_PLUS;
    if (b & (WPAD_PRO_BUTTON_MINUS | WPAD_PRO_BUTTON_L | WPAD_PRO_BUTTON_ZL)) out |= BTN_MINUS;
    if (b & WPAD_PRO_BUTTON_HOME)  out |= BTN_HOME;
    return out;
}

static u32 map_kpad(const KPADStatus *ks, bool down)
{
    /* the Pro Controller reports through its own bits, not the Wii Remote's */
    if (ks->extensionType == WPAD_EXT_PRO_CONTROLLER)
        return map_pro(down ? ks->pro.trigger : ks->pro.hold);
    u32 out = (down ? ks->trigger : ks->hold) & WIIMOTE_BUTTONS;
    if (ks->extensionType == WPAD_EXT_CLASSIC)
        out |= map_classic(down ? ks->classic.trigger : ks->classic.hold);
    return out;
}

/* A read right after another finds no new sample, so the last held buttons are kept for it. */
static u32 vpad_hold, kpad_hold[WPAD_CHAN_3 + 1];

/* Aroma's plugin menu combo on each controller (WiiUPluginLoaderBackend's VPADRead/WPADRead hooks). */
#define AROMA_VPAD    (VPAD_BUTTON_L | VPAD_BUTTON_DOWN | VPAD_BUTTON_MINUS)
#define AROMA_WIIMOTE (WPAD_BUTTON_B | WPAD_BUTTON_DOWN | WPAD_BUTTON_MINUS)
#define AROMA_CLASSIC (WPAD_CLASSIC_BUTTON_L | WPAD_CLASSIC_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_MINUS)
#define AROMA_PRO     (WPAD_PRO_TRIGGER_L | WPAD_PRO_BUTTON_DOWN | WPAD_PRO_BUTTON_MINUS)
static u32 vpad_raw_prev;
/* Per controller, kept like the held buttons when a read finds no new sample. */
static bool vpad_combo_on, kpad_combo_on[WPAD_CHAN_3 + 1];
static bool combo_down, plugin_menu_wanted;

static bool kpad_combo(const KPADStatus *ks)
{
    if (ks->extensionType == WPAD_EXT_PRO_CONTROLLER)
        return ks->pro.hold == AROMA_PRO;
    if (ks->extensionType == WPAD_EXT_CLASSIC)
        return ks->classic.hold == AROMA_CLASSIC;
    return ks->hold == AROMA_WIIMOTE;
}

/* Aroma zeroes the GamePad sample where the combo first completes, so a held-then-vanished
 * part of it counts too. */
static bool vpad_combo(const VPADStatus *st)
{
    u32 raw = st->hold & 0xFFFFF, prev = vpad_raw_prev;
    vpad_raw_prev = raw;
    if (raw == AROMA_VPAD)
        return true;
    return !raw && !st->release && !(prev & ~AROMA_VPAD) && __builtin_popcount(prev) >= 2;
}

/* Every connected controller, edge (down) or level (held). */
static u32 read_pads(bool down)
{
    u32 out = 0;
    if (vpad_ready) {
        VPADStatus st;
        VPADReadError err = VPAD_READ_SUCCESS;
        VPADRead(VPAD_CHAN_0, &st, 1, &err);
        if (err == VPAD_READ_SUCCESS) {
            vpad_combo_on = vpad_combo(&st);
            vpad_hold = map_vpad(st.hold);
            if (down)
                out |= map_vpad(st.trigger);
        } else if (err != VPAD_READ_NO_SAMPLES) {
            vpad_hold = 0;
            vpad_combo_on = false;
        }
        if (!down)
            out |= vpad_hold;
    }
    if (kpad_ready) {
        for (int chan = WPAD_CHAN_0; chan <= WPAD_CHAN_3; chan++) {
            KPADStatus ks;
            KPADError err = KPAD_ERROR_OK;
            /* ks is only set when a sample was written. */
            if (KPADReadEx(chan, &ks, 1, &err) > 0 && err == KPAD_ERROR_OK) {
                kpad_combo_on[chan] = kpad_combo(&ks);
                kpad_hold[chan] = map_kpad(&ks, false);
                if (down)
                    out |= map_kpad(&ks, true);
            } else if (err != KPAD_ERROR_NO_SAMPLES && err != KPAD_ERROR_OK) {
                kpad_hold[chan] = 0;
                kpad_combo_on[chan] = false;
            }
            if (!down)
                out |= kpad_hold[chan];
        }
    }
    bool combo = vpad_combo_on;
    for (int chan = WPAD_CHAN_0; chan <= WPAD_CHAN_3; chan++)
        combo |= kpad_combo_on[chan];
    if (combo && !combo_down)
        plugin_menu_wanted = true;
    combo_down = combo;
    /* The combo is Aroma's, not a file manager command. */
    return combo ? 0 : out;
}

void input_init(void)
{
    VPADInit();
    vpad_ready = true;
    KPADInit();
    /* Pro Controllers only stay connected to apps that opt in. */
    WPADEnableURCC(TRUE);
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
    u32 pads = read_pads(down);
    if (plugin_menu_wanted) {
        plugin_menu_wanted = false;
        ui_plugin_menu();
    }
    return pads;
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
