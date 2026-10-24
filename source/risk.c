#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <coreinit/time.h>

#include "risk.h"
#include "ui.h"
#include "input.h"
#include "vfs.h"

static bool g_unlocked;
static bool g_forced;       /* persistent "always unlocked" switch */

bool risk_unlocked(void) { return g_unlocked || g_forced; }
bool risk_forced(void) { return g_forced; }
void risk_lock(void) { g_unlocked = false; }

static const u32 KONAMI[10] = {
    BTN_UP, BTN_UP, BTN_DOWN, BTN_DOWN,
    BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT,
    BTN_B, BTN_A,
};
static const char *const KONAMI_GLYPH[10] = { "UP", "UP", "DN", "DN", "LT", "RT", "LT", "RT", " B", " A" };

/* Full-screen warning followed by the Konami code. `lines` are the warning
 * text, `title` the red banner. HOME cancels. */
static void draw_gate(const char *title, const char *const *lines, int n, int progress)
{
    int cols = ui_cols();
    ui_clear();
    ui_bg(C_RED);
    ui_color(C_WHITE, true);
    ui_field(0, 0, cols - 1, " !!!  %s  !!!", title);
    ui_reset();

    int r = 2;
    for (int i = 0; i < n; i++) {
        ui_color(i == 0 ? C_RED : C_WHITE, i == 0);
        ui_field(r++, 1, cols - 2, "%s", lines[i]);
    }
    r++;
    ui_color(C_YELLOW, true);
    ui_field(r++, 1, cols - 2, "If you understand and accept this, enter the code:");
    r++;

    int left = (cols - 10 * 5) / 2;
    ui_goto(r, left);
    for (int i = 0; i < 10; i++) {
        ui_color(i < progress ? C_GREEN : C_WHITE, i < progress);
        ui_printf("[%s] ", KONAMI_GLYPH[i]);
    }
    ui_reset();
    ui_color(C_CYAN, true);
    ui_field(r + 2, 1, cols - 2, "D-Pad, then B and A.  A wrong button starts over.");
    ui_field(r + 3, 1, cols - 2, "HOME: cancel");
    ui_reset();
}

static bool konami_gate(const char *title, const char *const *lines, int n)
{
    int progress = 0;
    draw_gate(title, lines, n, progress);
    const u32 mask = BTN_UP | BTN_DOWN | BTN_LEFT |
                     BTN_RIGHT | BTN_A | BTN_B | BTN_HOME;
    while (progress < 10) {
        u32 p = DetectInput(IN_DOWN) & mask;
        if (!p)
            continue;
        if (p & BTN_HOME)
            return false;
        if (p == KONAMI[progress])
            progress++;
        else
            progress = (p == KONAMI[0]) ? 1 : 0;
        draw_gate(title, lines, n, progress);
    }
    return true;
}

/* Asked the first time something tries to change protected storage. */
bool risk_ensure_unlocked(void)
{
    if (g_unlocked || g_forced)
        return true;
    static const char *const lines[] = {
        "You are about to change the Wii U's protected storage.",
        "",
        "* A wrong move here can PERMANENTLY BRICK your console.",
        "* Deleting system titles, tickets or the vWii NAND is fatal.",
        "* Make a full NAND or MLC backup BEFORE you continue.",
        "* The vWii NAND is just as fragile as a Wii's.",
        "* No warranty. You are solely responsible for what you do.",
        "* Not sure what a file does? Leave it alone.",
    };
    if (!konami_gate("RISKY OPTION - READ THIS", lines, 8))
        return false;
    g_unlocked = true;
    return true;
}

bool risk_authorize(const char *path, const char *what)
{
    (void)what;
    if (!vfs_is_protected(vfs_dev_of(path)))
        return true;
    /* One Konami code per session is enough; it asks the first time. */
    return risk_ensure_unlocked();
}

/* ---------------------------------------------------------------- forced mode */

/* The switch lives in a small file on the SD card. */
static bool force_real(const char *sub, char *out, size_t n)
{
    const vfs_dev *d = &g_devs[DEV_SD];
    return d->mounted && snprintf(out, n, "%s/wiiu/apps/GodModeU%s", d->real, sub) < (int)n;
}

void risk_load_forced(void)
{
    char p[VFS_PATH_MAX];
    struct stat st;
    g_forced = force_real("/force.cfg", p, sizeof(p)) && stat(p, &st) == 0;
}

static bool force_store(bool on)
{
    char p[VFS_PATH_MAX];
    if (!force_real("/force.cfg", p, sizeof(p)))
        return false;
    if (!on) {
        struct stat st;
        unlink(p);
        return stat(p, &st) != 0;
    }
    char dir[VFS_PATH_MAX];
    if (force_real("", dir, sizeof(dir))) {
        char up[VFS_PATH_MAX];
        snprintf(up, sizeof(up), "%s/wiiu", g_devs[DEV_SD].real);
        mkdir(up, 0777);
        snprintf(up, sizeof(up), "%s/wiiu/apps", g_devs[DEV_SD].real);
        mkdir(up, 0777);
        mkdir(dir, 0777);
    }
    FILE *f = fopen(p, "wb");
    if (!f)
        return false;
    bool ok = fputs("1\n", f) >= 0;
    return (fclose(f) == 0) && ok;
}

/* Keeps listening until at least `min_ms` after begin, then reports whether the code went in. */
bool risk_startup_poll(u32 min_ms, u64 began_ms)
{
    int progress = 0;
    bool hit = false;
    const u32 mask = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_A | BTN_B;
    while (OSTicksToMilliseconds(OSGetSystemTime()) - began_ms < min_ms) {
        u32 p = input_poll_nowait() & mask;     /* waits one frame */
        if (!p)
            continue;
        if (p == KONAMI[progress])
            progress++;
        else
            progress = (p == KONAMI[0]) ? 1 : 0;
        if (progress == 10) {
            hit = true;
            progress = 0;
        }
    }
    return hit;
}

/* Flips the persistent switch; returns false if it could not be saved. */
bool risk_toggle_forced(void)
{
    bool want = !g_forced;
    if (!force_store(want))
        return false;
    g_forced = want;
    return true;
}
