/*
 * DLC WAD -> small WADs, the way DarkMatterCore's dlc-wad-splitter cuts them
 * (https://github.com/DarkMatterCore/dlc-wad-splitter): each one holds the ticket, the TMD as it is
 * (still signed), content 0 and the next few contents. The work is w2b_split (wad2bin.c); this is the
 * file browser's action. See wadinstall.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include "wadinstall.h"
#include "vfs.h"
#include "ui.h"
#include "ftype.h"
#include "input.h"

#define CHUNK 0x4000

static int32_t vsrc_read(void *ctx, uint64_t off, void *buf, uint32_t len)
{
    vfs_file *f = ctx;
    static u8 *bounce;
    if (!bounce && !(bounce = memalign(32, CHUNK))) return -1;
    uint32_t done = 0;
    if (!vfs_seek(f, off)) return -1;
    while (done < len) {
        uint32_t n = len - done > CHUNK ? CHUNK : len - done;
        s32 r = vfs_read(f, bounce, n);
        if (r <= 0) break;
        memcpy((u8 *)buf + done, bounce, (size_t)r);
        done += (uint32_t)r;
        if ((uint32_t)r < n) break;
    }
    return (int32_t)done;
}
static void vsrc_close(void *ctx) { (void)ctx; }

static bool mkdirs(const char *dir)
{
    bool is_dir;
    if (vfs_stat(dir, &is_dir, NULL)) return is_dir;
    char parent[VFS_PATH_MAX];
    if (!vfs_parent(dir, parent, sizeof(parent))) return false;
    if (!vfs_is_root(parent) && !mkdirs(parent)) return false;
    return vfs_mkdir(dir) || (vfs_stat(dir, &is_dir, NULL) && is_dir);
}

static bool vio_mkdirs(void *ctx, const char *dir) { (void)ctx; return mkdirs(dir); }
static void *vio_open(void *ctx, const char *path)
{
    (void)ctx;
    vfs_file *f = calloc(1, sizeof(*f));
    if (f && !vfs_open_write(f, path)) {
        free(f);
        f = NULL;
    }
    if (f) vfs_unbuffered(f);
    return f;
}
static bool vio_write(void *ctx, void *fh, const void *p, uint32_t n)
{
    static u8 *wb;
    if (!wb && !(wb = memalign(32, CHUNK))) return false;
    (void)ctx;
    for (uint32_t done = 0; done < n;) {
        uint32_t k = n - done > CHUNK ? CHUNK : n - done;
        memcpy(wb, (const u8 *)p + done, k);
        if (vfs_write(fh, wb, k) != (s32)k) return false;
        done += k;
    }
    return true;
}
static bool vio_close(void *ctx, void *fh)
{
    (void)ctx;
    bool ok = vfs_close_checked(fh);
    free(fh);
    return ok;
}
static void vio_remove(void *ctx, const char *path) { (void)ctx; vfs_remove(path, false); }

static void progress(const char *name, const char *what, u64 done, u64 total)
{
    ui_splash(name);
    int r = ui_rows() / 2 + 2, cols = ui_cols();
    ui_field(r, 0, cols - 1, "%*s%s", (cols - (int)strlen(what)) / 2, "", what);
    int pct = total ? (int)(done * 100 / total) : 0;
    ui_bar(r + 1, (cols - 50) / 2, 50, pct, C_GREEN);
}

static bool split_progress(void *ctx, const char *what, uint64_t done, uint64_t total)
{
    static u64 last;
    if (input_cancel_held()) return false;
    u64 now = ticks_to_millisecs(gettime());
    if (now - last < 200 && done != total) return true;
    last = now;
    char w[80];
    snprintf(w, sizeof(w), "%s...  (hold B to cancel)", what);
    progress(ctx, w, done, total);
    return true;
}

bool wad_is_dlc(const char *path)
{
    vfs_file f;
    uint64_t tid = 0;
    bool ok = false;
    if (ftype_detect(path) == FT_WAD && vfs_open_read(&f, path)) {
        src_t s = { vsrc_read, vsrc_close, &f, f.size };
        ok = w2b_peek(&s, &tid) && (tid >> 32) == 0x00010005;
        vfs_close(&f);
    }
    return ok;
}

static int target_device(void)
{
    for (int d = DEV_SD; d <= DEV_USB7; d++)
        if (g_devs[d].mounted && !vfs_is_readonly(g_devs[d].root)) return d;
    return -1;
}

int wad_split_paths(char **paths, int n)
{
    int dlcs = 0;
    for (int i = 0; i < n; i++)
        if (wad_is_dlc(paths[i])) dlcs++;
    if (!dlcs) {
        ui_message("SPLIT DLC WAD", C_YELLOW, "Only DLC WADs (title type 00010005) can be split.");
        return 0;
    }
    int dev = target_device();
    if (dev < 0) {
        ui_message("SPLIT DLC WAD", C_RED, "Insert a writable SD card (or USB drive) for the split WADs.");
        return 0;
    }
    static const int sizes[] = { 1, 5, 10, 20, 50 };
    const char *items[] = { "1 content per WAD", "5 contents per WAD", "10 contents per WAD", "20 contents per WAD",
                            "50 contents per WAD" };
    char q[600];
    snprintf(q, sizeof(q),
             "Split %d DLC WAD%s into small WADs, the way dlc-wad-splitter does it?\n\n"
             "Each small WAD holds the whole TMD (still signed), content 0 and the next few contents, so it "
             "installs on its own (to the vWii's NAND, or to the SD card with wad2bin) next to the others. The "
             "ticket is fakesigned if it was bound to a console. They go to %sGodModeU/wad-split/.",
             dlcs, dlcs == 1 ? "" : "s", g_devs[dev].root);
    if (!ui_confirm("SPLIT DLC WAD", C_CYAN, q)) return 0;
    int r = ui_menu("CONTENTS PER SMALL WAD", items, NULL, 5, 0);
    if (r < 0) return 0;

    char dir[VFS_PATH_MAX];
    snprintf(dir, sizeof(dir), "%sGodModeU/wad-split", g_devs[dev].root);
    int ok = 0, done = 0;
    char report[1400] = "", line[VFS_PATH_MAX + 200] = "";
    size_t rl = 0;
    for (int i = 0; i < n; i++) {
        if (!wad_is_dlc(paths[i])) continue;
        const char *name = vfs_basename(paths[i]);
        w2b_result_t res;
        memset(&res, 0, sizeof(res));
        int made = -1;
        vfs_file f;
        if (vfs_open_read(&f, paths[i])) {
            vfs_unbuffered(&f);
            src_t s = { vsrc_read, vsrc_close, &f, f.size };
            uint64_t tid = 0;
            w2b_peek(&s, &tid);
            char base[24];
            snprintf(base, sizeof(base), "%016llx", (unsigned long long)tid);
            w2b_io_t io = { (void *)name, vio_mkdirs, vio_open, vio_write, vio_close, vio_remove, split_progress };
            progress(name, "Looking for the contents...", 0, 1);
            made = w2b_split(&s, dir, base, sizes[r], &io, &res);
            vfs_close(&f);
        } else
            snprintf(res.err, sizeof(res.err), "Can't open the file");
        done++;
        if (made > 0) {
            ok++;
            snprintf(line, sizeof(line), "%s: %d small WAD%s in %s%s", name, made, made == 1 ? "" : "s", dir,
                     res.missing ? " (contents missing from it were left out)" : "");
        } else
            snprintf(line, sizeof(line), "%s: %s", name, res.err);
        if (rl < sizeof(report) - 1)
            rl += snprintf(report + rl, sizeof(report) - rl, "%s %s\n", made > 0 ? "OK  " : "FAIL", line);
        if (rl >= sizeof(report)) rl = sizeof(report) - 1;
    }
    vfs_update_space(dev);
    if (done == 1)
        ui_message(ok ? "DLC WAD SPLIT" : "SPLIT FAILED", ok ? C_GREEN : C_RED, line);
    else {
        char title[48];
        snprintf(title, sizeof(title), "SPLIT %d OF %d DLC WADS", ok, done);
        ui_message(title, ok == done ? C_GREEN : C_YELLOW, report);
    }
    return ok;
}
