/*
 * HOME > Launch: the Wii U Homebrew Launcher and other Wii U homebrew (through Aroma's RPXLoader), the Wii U Menu,
 * and on the vWii side the Wii Menu (Priiloader runs first when it is installed), the Homebrew Channel and a
 * GodModeWii channel, each straight through CMPT or, for channels, through hbl2hbc (FIX94), which skips the
 * screen and controller prompts. Aroma's plugin menu opens from here too.
 *
 * The launch itself happens in main() once the drives are let go (launch_now).
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <coreinit/dynload.h>
#include <nn/cmpt/cmpt.h>
#include <sysapp/launch.h>

#include "launch.h"
#include "ui.h"
#include "vfs.h"

#define HBL2HBC_TXT   "sd:/hbl2hbc.txt"
#define HBL2HBC_SAVED "sd:/wiiu/apps/GodModeU/hbl2hbc.txt.saved"   /* the user's own, put back at the next start */

enum { L_NONE, L_RPX, L_WIIU_MENU, L_VWII_MENU, L_VWII_TITLE };
static int kind = L_NONE;
static char rpx[VFS_PATH_MAX];          /* relative to the SD card's top, as RPXLoader takes it */
static uint32_t vwii_low;               /* 00010001-xxxxxxxx */

static bool first_file(const char *const *list, int n, char *out, size_t sz)
{
    for (int i = 0; i < n; i++) {
        char p[VFS_PATH_MAX];
        bool dir;
        snprintf(p, sizeof(p), "sd:/%s", list[i]);
        if (vfs_stat(p, &dir, NULL) && !dir) {
            snprintf(out, sz, "%s", list[i]);
            return true;
        }
    }
    return false;
}

static bool find_hbl(char *out, size_t sz)
{
    static const char *const at[] = {
        "wiiu/apps/homebrew_launcher/homebrew_launcher.wuhb", "wiiu/apps/homebrew_launcher.wuhb",
        "wiiu/apps/homebrew_launcher/homebrew_launcher.rpx", "wiiu/apps/homebrew_launcher.rpx",
    };
    return first_file(at, 4, out, sz);
}

static bool find_hbl2hbc(char *out, size_t sz)
{
    static const char *const at[] = {
        "wiiu/apps/hbl2hbc/hbl2hbc.wuhb", "wiiu/apps/hbl2hbc.wuhb", "wiiu/apps/hbl2hbc/hbl2hbc.rpx", "wiiu/apps/hbl2hbc.rpx",
    };
    return first_file(at, 4, out, sz);
}

static bool vwii_title(uint32_t low)
{
    char p[64];
    bool dir;
    snprintf(p, sizeof(p), "nand:/title/00010001/%08x/content", (unsigned)low);
    return vfs_stat(p, &dir, NULL) && dir;
}

static bool has_word(const char *s, const char *w)
{
    for (size_t n = strlen(w); *s; s++)
        if (!strncasecmp(s, w, n)) return true;
    return false;
}

/* A channel whose banner's English name holds `what` (its 00000000.app: "IMET" at 0x40, the names after it). */
static uint32_t vwii_channel_named(const char *what)
{
    vfs_entry *e = NULL;
    int n = 0;
    uint32_t found = 0;
    if (!vfs_list("nand:/title/00010001", &e, &n)) return 0;
    for (int i = 0; i < n && !found; i++) {
        char p[VFS_PATH_MAX];
        snprintf(p, sizeof(p), "nand:/title/00010001/%s/content/00000000.app", e[i].name);
        vfs_file f;
        unsigned char b[0x104];
        if (!vfs_open_read(&f, p)) continue;
        bool ok = vfs_read(&f, b, sizeof(b)) == (s32)sizeof(b);
        vfs_close(&f);
        if (!ok || memcmp(b + 0x40, "IMET", 4)) continue;
        char name[43];
        for (int k = 0; k < 42; k++) {          /* English, UTF-16BE, after the Japanese name */
            unsigned c = b[0xB0 + 2 * k] << 8 | b[0xB1 + 2 * k];
            name[k] = c && c < 0x80 ? (char)c : c ? '?' : 0;
            if (!c) break;
        }
        name[42] = 0;
        if (has_word(name, what)) found = (uint32_t)strtoul(e[i].name, NULL, 16);
    }
    free(e);
    return found;
}

static bool priiloader_there(void)
{
    vfs_entry *e = NULL;
    int n = 0;
    bool yes = false;
    if (!vfs_list("nand:/title/00000001/00000002/content", &e, &n)) return false;
    for (int i = 0; i < n; i++)                 /* Priiloader moves the Wii Menu's own .app to 1xxxxxxx.app */
        if (strlen(e[i].name) == 12 && e[i].name[0] == '1' && !strcasecmp(e[i].name + 8, ".app")) yes = true;
    free(e);
    return yes;
}

/* hbl2hbc boots the first channel in its list straight away when there is one line. */
static bool hbl2hbc_target(uint32_t low, const char *name)
{
    if (vfs_exists(HBL2HBC_TXT) && !vfs_exists(HBL2HBC_SAVED)) rename(HBL2HBC_TXT, HBL2HBC_SAVED);
    FILE *f = fopen(HBL2HBC_TXT, "wb");
    if (!f) return false;
    char id[5] = { (char)(low >> 24), (char)(low >> 16), (char)(low >> 8), (char)low, 0 };
    fprintf(f, "%s=%s\n", id, name);
    return fclose(f) == 0;
}

void launch_restore(void)
{
    if (!vfs_exists(HBL2HBC_SAVED)) return;
    remove(HBL2HBC_TXT);
    rename(HBL2HBC_SAVED, HBL2HBC_TXT);
}

bool launch_program(const char *sd_path)
{
    if (strncasecmp(sd_path, "sd:/", 4)) return false;
    kind = L_RPX;
    snprintf(rpx, sizeof(rpx), "%s", sd_path + 4);
    return true;
}

bool launch_menu(void)
{
    char hbl[VFS_PATH_MAX], h2h[VFS_PATH_MAX];
    bool have_hbl = find_hbl(hbl, sizeof(hbl)), have_h2h = find_hbl2hbc(h2h, sizeof(h2h));
    uint32_t hbc = vwii_title(0x4C554C5A) ? 0x4C554C5A : vwii_title(0x4F484243) ? 0x4F484243 : 0;
    uint32_t gmw = vwii_channel_named("GodModeWii");
    bool prii = priiloader_there();

    enum { I_HBL, I_WIIU, I_WIIMENU, I_HBC, I_HBC_H, I_GMW, I_GMW_H, I_AROMA };
    const char *items[8];
    int code[8], n = 0;
#define ITEM(t, c) do { items[n] = (t); code[n++] = (c); } while (0)
    if (have_hbl) ITEM("Wii U Homebrew Launcher", I_HBL);
    ITEM("Wii U Menu", I_WIIU);
    ITEM(prii ? "vWii: Priiloader, then the Wii Menu" : "vWii: Wii Menu", I_WIIMENU);
    if (hbc) ITEM("vWii: Homebrew Channel", I_HBC);
    if (hbc && have_h2h) ITEM("vWii: Homebrew Channel (through hbl2hbc)", I_HBC_H);
    if (gmw) ITEM("vWii: GodModeWii", I_GMW);
    if (gmw && have_h2h) ITEM("vWii: GodModeWii (through hbl2hbc)", I_GMW_H);
    ITEM("Aroma plugin menu", I_AROMA);
#undef ITEM
    int r = ui_menu_ex("LAUNCH", items, NULL, n, 0, "A Start   B Back");
    if (r < 0) return false;
    switch (code[r]) {
    case I_AROMA:
        ui_plugin_menu();
        return false;
    case I_HBL:
        kind = L_RPX;
        snprintf(rpx, sizeof(rpx), "%s", hbl);
        return true;
    case I_WIIU:
        kind = L_WIIU_MENU;
        return true;
    case I_WIIMENU:
        kind = L_VWII_MENU;
        return true;
    case I_HBC:
    case I_GMW:
        kind = L_VWII_TITLE;
        vwii_low = code[r] == I_HBC ? hbc : gmw;
        return true;
    default:
        if (!hbl2hbc_target(code[r] == I_HBC_H ? hbc : gmw, code[r] == I_HBC_H ? "Homebrew Channel" : "GodModeWii")) {
            ui_message("LAUNCH", C_RED, "Couldn't write sd:/hbl2hbc.txt for hbl2hbc.");
            return false;
        }
        kind = L_RPX;
        snprintf(rpx, sizeof(rpx), "%s", h2h);
        return true;
    }
}

/* RPXLoader's own export (the homebrew_rpx_loader module Aroma loads): 0 when the app is on its way. */
static bool rpx_launch(const char *path)
{
    OSDynLoad_Module m;
    int (*fn)(const char *) = NULL;
    if (OSDynLoad_Acquire("homebrew_rpx_loader", &m) != OS_DYNLOAD_OK) return false;
    bool ok = OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "RL_LaunchHomebrew", (void **)&fn) == OS_DYNLOAD_OK &&
              fn && fn(path) == 0;
    return ok;
}

static bool cmpt_launch(bool menu, uint64_t tid)
{
    uint32_t size = 0;
    if (CMPTGetDataSize(&size) < 0 || !size) return false;
    void *buf = memalign(0x40, size);
    if (!buf) return false;
    CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_BOTH);
    int32_t r = menu ? CMPTLaunchMenu(buf, size) : CMPTLaunchTitle(buf, size, tid);
    if (r < 0) free(buf);                       /* on success it is the vWii's now */
    return r >= 0;
}

bool launch_now(void)
{
    switch (kind) {
    case L_RPX:       return rpx_launch(rpx);
    case L_WIIU_MENU: SYSLaunchMenu(); return true;
    case L_VWII_MENU: return cmpt_launch(true, 0);
    case L_VWII_TITLE: return cmpt_launch(false, 0x0001000100000000ULL | vwii_low);
    default:          return false;
    }
}
