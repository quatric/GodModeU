#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <malloc.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <coreinit/filesystem_fsa.h>
#include <whb/sdcard.h>

#include "vfs.h"
#include "arcfs.h"

vfs_dev g_devs[DEV_COUNT] = {
    { "sd:",   "sd:/",   "SD Card",          "SD",   true,  false, false, 0, 0 },
    { "usb1:", "usb1:/", "USB Drive 1",      "USB1", false, false, false, 0, 0 },
    { "usb2:", "usb2:/", "USB Drive 2",      "USB2", false, false, false, 0, 0 },
    { "usb3:", "usb3:/", "USB Drive 3",      "USB3", false, false, false, 0, 0 },
    { "usb4:", "usb4:/", "USB Drive 4",      "USB4", false, false, false, 0, 0 },
    { "usb5:", "usb5:/", "USB Drive 5",      "USB5", false, false, false, 0, 0 },
    { "usb6:", "usb6:/", "USB Drive 6",      "USB6", false, false, false, 0, 0 },
    { "usb7:", "usb7:/", "USB Drive 7",      "USB7", false, false, false, 0, 0 },
    { "usb8:", "usb8:/", "USB Drive 8",      "USB8", false, false, false, 0, 0 },
    { "mlc:",  "mlc:/",  "Wii U Storage (MLC)", "MLC",  true,  false, false, 0, 0 },
    { "nand:", "nand:/", "vWii NAND (SLC)",  "NAND", true,  false, false, 0, 0 },
};

const char *g_write_err;
static FSAClientHandle fsa_client;
static bool fsa_ready;

/* ---------------------------------------------------------------- paths */

int vfs_dev_of(const char *path)
{
    for (int i = 0; i < DEV_COUNT; i++)
        if (strncmp(path, g_devs[i].prefix, strlen(g_devs[i].prefix)) == 0)
            return i;
    return -1;
}

bool vfs_is_protected(int dev)
{
    return dev == DEV_MLC || dev == DEV_NAND;
}

bool vfs_is_archive(const char *path)
{
    return arcfs_is_path(path);
}

bool vfs_is_readonly(const char *path)
{
    return arcfs_is_path(path);
}

const char *vfs_dev_root(int dev)
{
    return g_devs[dev].root;
}

bool vfs_is_root(const char *path)
{
    int d = vfs_dev_of(path);
    return d >= 0 && strcmp(path, vfs_dev_root(d)) == 0;
}

/* "sd:/games/x" -> "fs:/vol/sdcard01/games/x" */
static bool to_real(const char *path, char *out, size_t outsz)
{
    int d = vfs_dev_of(path);
    if (d < 0 || !g_devs[d].real[0])
        return false;
    const char *rel = path + strlen(g_devs[d].prefix);
    int n = snprintf(out, outsz, "%s%s", g_devs[d].real, rel);
    return n > 0 && (size_t)n < outsz;
}

/* The FSA calls want "/vol/..." without the devoptab prefix. */
static const char *fsa_path(const char *real)
{
    return strncmp(real, "fs:", 3) == 0 ? real + 3 : real;
}

bool vfs_parent(const char *path, char *out, size_t outsz)
{
    int d = vfs_dev_of(path);
    if (d < 0 || vfs_is_root(path))
        return false;
    snprintf(out, outsz, "%s", path);
    char *bar = strrchr(out, '|');
    if (bar) {
        char *rest = bar + 1;
        while (*rest == '/') rest++;
        if (!*rest) {                       /* top of an archive: back to the folder holding it */
            char tmp[VFS_PATH_MAX * 2];
            *bar = '\0';
            snprintf(tmp, sizeof(tmp), "%s", out);
            return vfs_parent(tmp, out, outsz);
        }
        char *sl = strrchr(rest, '/');
        if (sl && sl[1]) { *sl = '\0'; return true; }
        if (sl) {                           /* trailing slash */
            *sl = '\0';
            sl = strrchr(rest, '/');
            if (sl) { *sl = '\0'; return true; }
        }
        bar[1] = '/'; bar[2] = '\0';
        return true;
    }
    char *slash = strrchr(out, '/');
    if (!slash)
        return false;
    if (slash == strchr(out, '/'))      /* "sd:/name" -> "sd:/" */
        slash[1] = '\0';
    else
        *slash = '\0';
    return true;
}

const char *vfs_basename(const char *path)
{
    const char *s = strrchr(path, '/');
    return (s && s[1]) ? s + 1 : path;
}

bool vfs_join(char *out, size_t outsz, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    int n = snprintf(out, outsz, "%s%s%s", dir, (dl && dir[dl - 1] == '/') ? "" : "/", name);
    return n > 0 && (size_t)n < outsz;
}

bool vfs_is_within(const char *path, const char *dir)
{
    size_t dl = strlen(dir);
    while (dl > 1 && dir[dl - 1] == '/' && dir[dl - 2] != ':')
        dl--;
    if (strncasecmp(path, dir, dl) != 0)
        return false;
    return path[dl] == '\0' || path[dl] == '/' || dir[dl - 1] == '/';
}

/* ---------------------------------------------------------------- mounting */

static bool dir_exists(const char *real)
{
    DIR *d = opendir(real);
    if (!d)
        return false;
    closedir(d);
    return true;
}

static void set_real(int dev, const char *vol)
{
    /* "/vol/sdcard01" or "fs:/vol/sdcard01" -> "fs:/vol/sdcard01" */
    if (strncmp(vol, "fs:", 3) == 0)
        snprintf(g_devs[dev].real, sizeof(g_devs[dev].real), "%s", vol);
    else
        snprintf(g_devs[dev].real, sizeof(g_devs[dev].real), "fs:%s", vol);
    size_t l = strlen(g_devs[dev].real);
    while (l > 3 && g_devs[dev].real[l - 1] == '/')
        g_devs[dev].real[--l] = '\0';
}

static void mount_sd(void)
{
    vfs_dev *d = &g_devs[DEV_SD];
    d->mounted = false;
    d->real[0] = '\0';
    d->note[0] = '\0';
    const char *mp = WHBMountSdCard() ? WHBGetSdCardMountPath() : NULL;
    if (mp && *mp) {
        set_real(DEV_SD, mp);
        d->mounted = dir_exists(d->real);
    }
    if (!d->mounted)
        snprintf(d->note, sizeof(d->note), "no SD card (insert one, then Remount)");
}

/* Cafe OS mounts each USB volume as /vol/external01 .. external08. */
static void mount_usb(void)
{
    for (int i = 0; i < USB_SLOTS; i++) {
        vfs_dev *d = &g_devs[DEV_USB0 + i];
        char vol[32];
        snprintf(vol, sizeof(vol), "fs:/vol/external%02d", i + 1);
        snprintf(d->real, sizeof(d->real), "%s", vol);
        d->mounted = dir_exists(vol);
        d->note[0] = '\0';
        if (!d->mounted)
            d->real[0] = '\0';
    }
}

static void mount_internal(void)
{
    vfs_dev *mlc = &g_devs[DEV_MLC];
    mlc->real[0] = '\0';
    mlc->mounted = dir_exists("fs:/vol/storage_mlc01");
    if (mlc->mounted)
        snprintf(mlc->real, sizeof(mlc->real), "fs:/vol/storage_mlc01");
    else
        snprintf(mlc->note, sizeof(mlc->note), "not accessible");

    vfs_dev *nand = &g_devs[DEV_NAND];
    nand->real[0] = '\0';
    nand->mounted = dir_exists("fs:/vol/slc01");
    if (nand->mounted)
        snprintf(nand->real, sizeof(nand->real), "fs:/vol/slc01");
    else
        snprintf(nand->note, sizeof(nand->note), "needs system access (Aroma / Mocha)");
}

static void fsa_start(void)
{
    if (fsa_ready)
        return;
    FSAInit();
    fsa_client = FSAAddClient(NULL);
    fsa_ready = fsa_client != 0;
}

void vfs_update_space(int dev)
{
    vfs_dev *d = &g_devs[dev];
    d->space_known = false;
    if (!d->mounted || !fsa_ready)
        return;
    const char *fp = fsa_path(d->real);
    u64 freeb = 0;
    FSADeviceInfo info;
    if (FSAGetFreeSpaceSize(fsa_client, fp, &freeb) != (FSError)FS_STATUS_OK)
        return;
    if (FSAGetDeviceInfo(fsa_client, fp, &info) != (FSError)FS_STATUS_OK)
        return;
    d->total = (u64)info.deviceSizeInSectors * info.deviceSectorSize;
    d->free = freeb;
    d->space_known = d->total > 0;
}

static void mount_all(void)
{
    mount_sd();
    mount_usb();
    mount_internal();
    for (int i = 0; i < DEV_COUNT; i++)
        vfs_update_space(i);
}

void vfs_init(void)
{
    fsa_start();
    mount_all();
}

void vfs_remount(void)
{
    arcfs_flush();
    WHBUnmountSdCard();
    mount_all();
}

void vfs_shutdown(void)
{
    arcfs_flush();
    WHBUnmountSdCard();
    for (int i = 0; i < DEV_COUNT; i++)
        g_devs[i].mounted = false;
    if (fsa_ready) {
        FSADelClient(fsa_client);
        fsa_ready = false;
    }
}

/* ---------------------------------------------------------------- listing */

static int entry_cmp(const void *a, const void *b)
{
    const vfs_entry *x = a, *y = b;
    if (x->is_dir != y->is_dir)
        return x->is_dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

bool vfs_list(const char *path, vfs_entry **out, int *count)
{
    *out = NULL;
    *count = 0;
    int dev = vfs_dev_of(path);
    if (dev < 0 || !g_devs[dev].mounted)
        return false;

    if (arcfs_is_path(path)) {
        if (!arcfs_list(path, out, count))
            return false;
        if (*count > 1)
            qsort(*out, *count, sizeof(vfs_entry), entry_cmp);
        return true;
    }

    char real[VFS_PATH_MAX];
    if (!to_real(path, real, sizeof(real)))
        return false;
    DIR *dir = opendir(real);
    if (!dir)
        return false;

    int cap = 0, n = 0;
    vfs_entry *list = NULL;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            vfs_entry *nl = realloc(list, cap * sizeof(vfs_entry));
            if (!nl) {
                free(list);
                closedir(dir);
                return false;
            }
            list = nl;
        }
        vfs_entry *e = &list[n];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", de->d_name);
        char child[VFS_PATH_MAX];
        struct stat st;
        if (vfs_join(child, sizeof(child), real, de->d_name) && stat(child, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode);
            e->size = e->is_dir ? 0 : (u64)st.st_size;
        }
        n++;
    }
    closedir(dir);

    if (n > 1)
        qsort(list, n, sizeof(vfs_entry), entry_cmp);
    *out = list;
    *count = n;
    return true;
}

/* Files the operating systems drop on FAT volumes: not worth showing. */
bool vfs_is_hidden_name(const char *name)
{
    static const char *const junk[] = {
        "thumbs.db", "desktop.ini", ".ds_store", "system volume information", "$recycle.bin",
        ".trashes", ".spotlight-v100", ".fseventsd", ".temporaryitems", ".apdisk",
        "__macosx", "ehthumbs.db", "found.000",
    };
    if (name[0] == '.')                             /* dotfiles: .DS_Store, ._*, .Trashes, ... */
        return true;
    for (size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); i++)
        if (!strcasecmp(name, junk[i]))
            return true;
    return false;
}

bool vfs_list_visible(const char *path, vfs_entry **out, int *count)
{
    if (!vfs_list(path, out, count))
        return false;
    if (arcfs_is_path(path))
        return true;
    int k = 0;
    for (int i = 0; i < *count; i++)
        if (!vfs_is_hidden_name((*out)[i].name))
            (*out)[k++] = (*out)[i];
    *count = k;
    return true;
}

bool vfs_stat(const char *path, bool *is_dir, u64 *size)
{
    int dev = vfs_dev_of(path);
    if (dev < 0 || !g_devs[dev].mounted)
        return false;
    if (vfs_is_root(path)) {
        if (is_dir) *is_dir = true;
        if (size) *size = 0;
        return true;
    }
    if (arcfs_is_path(path))
        return arcfs_stat(path, is_dir, size);
    char real[VFS_PATH_MAX];
    if (!to_real(path, real, sizeof(real)))
        return false;
    struct stat st;
    if (stat(real, &st) != 0)
        return false;
    if (is_dir) *is_dir = S_ISDIR(st.st_mode);
    if (size) *size = S_ISDIR(st.st_mode) ? 0 : (u64)st.st_size;
    return true;
}

bool vfs_exists(const char *path)
{
    return vfs_stat(path, NULL, NULL);
}

/* ---------------------------------------------------------------- modify */

bool vfs_mkdir(const char *path)
{
    int dev = vfs_dev_of(path);
    char real[VFS_PATH_MAX];
    if (dev < 0 || arcfs_is_path(path) || !to_real(path, real, sizeof(real)))
        return false;
    arcfs_flush();
    return mkdir(real, 0777) == 0;
}

bool vfs_remove(const char *path, bool is_dir)
{
    char real[VFS_PATH_MAX];
    if (arcfs_is_path(path) || !to_real(path, real, sizeof(real)))
        return false;
    arcfs_flush();
    return is_dir ? rmdir(real) == 0 : unlink(real) == 0;
}

bool vfs_rename(const char *from, const char *to)
{
    int a = vfs_dev_of(from), b = vfs_dev_of(to);
    if (a < 0 || a != b || arcfs_is_path(from) || arcfs_is_path(to))
        return false;
    char rf[VFS_PATH_MAX], rt[VFS_PATH_MAX];
    if (!to_real(from, rf, sizeof(rf)) || !to_real(to, rt, sizeof(rt)))
        return false;
    arcfs_flush();
    return rename(rf, rt) == 0;
}

/* ---------------------------------------------------------------- files */

bool vfs_open_read(vfs_file *f, const char *path)
{
    memset(f, 0, sizeof(*f));
    f->dev = vfs_dev_of(path);
    if (f->dev < 0)
        return false;
    if (arcfs_is_path(path))
        return arcfs_open(f, path);
    char real[VFS_PATH_MAX];
    if (!to_real(path, real, sizeof(real)))
        return false;
    f->fp = fopen(real, "rb");
    if (!f->fp)
        return false;
    fseek(f->fp, 0, SEEK_END);
    f->size = (u64)ftell(f->fp);
    fseek(f->fp, 0, SEEK_SET);
    return true;
}

bool vfs_open_write(vfs_file *f, const char *path)
{
    memset(f, 0, sizeof(*f));
    f->dev = vfs_dev_of(path);
    g_write_err = NULL;
    if (arcfs_is_path(path) || f->dev < 0)
        return false;
    char real[VFS_PATH_MAX];
    if (!to_real(path, real, sizeof(real)))
        return false;
    arcfs_flush();
    f->fp = fopen(real, "wb");
    return f->fp != NULL;
}

s32 vfs_read(vfs_file *f, void *buf, u32 len)
{
    if (f->arc)
        return arcfs_read(f, buf, len);
    size_t n = fread(buf, 1, len, f->fp);
    if (n == 0 && ferror(f->fp))
        return -1;
    return (s32)n;
}

s32 vfs_write(vfs_file *f, const void *buf, u32 len)
{
    if (f->arc)
        return -1;
    size_t n = fwrite(buf, 1, len, f->fp);
    return (n == len) ? (s32)n : -1;
}

bool vfs_seek(vfs_file *f, u64 off)
{
    if (f->arc)
        return arcfs_seek(f, off);
    return fseek(f->fp, (long)off, SEEK_SET) == 0;
}

bool vfs_close_checked(vfs_file *f)
{
    if (f->arc) {
        arcfs_close(f);
        return true;
    }
    bool ok = true;
    if (f->fp) {
        if (fflush(f->fp) != 0)
            ok = false;
        if (fclose(f->fp) != 0)
            ok = false;
    }
    f->fp = NULL;
    return ok;
}

void vfs_close(vfs_file *f)
{
    vfs_close_checked(f);
}
