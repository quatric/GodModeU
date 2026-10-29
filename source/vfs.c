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
#include <mocha/mocha.h>

#include "vfs.h"
#include "arcfs.h"
#include "risk.h"
#include "usbfat.h"
#include "usbntfs.h"
#include <sys/statvfs.h>

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
    { "nand:", "nand:/", "vWii NAND (SLCCMPT)", "NAND", true,  false, false, 0, 0 },
    { "slc:",  "slc:/",  "Wii U NAND (SLC)", "SLC",  true,  false, false, 0, 0 },
};

const char *g_write_err;
static FSAClientHandle fsa_client;
static bool fsa_ready;
static bool mocha_ready;
static char mocha_name[DEV_COUNT][32];  /* the Mocha devoptab a drive is reached through, if any */

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
    return dev == DEV_MLC || dev == DEV_NAND || dev == DEV_SLC;
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

/* The FSA calls want "/vol/..." without the devoptab prefix. Mocha devoptabs are named after
 * their volume: "storage_usb01:/x" -> "/vol/storage_usb01/x". */
static const char *fsa_path(const char *real, char *buf, size_t n)
{
    if (strncmp(real, "fs:", 3) == 0)
        return real + 3;
    const char *colon = strchr(real, ':');
    if (!colon)
        return real;
    snprintf(buf, n, "/vol/%.*s%s", (int)(colon - real), real, colon + 1);
    return buf;
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

/* An app may lack permission for a system volume. Aroma's Mocha can then reach it through an
 * unlocked client, as a devoptab named after the volume. */
static bool mocha_try(const char *vol, const char *devpath)
{
    char mnt[32], root[32];
    snprintf(mnt, sizeof(mnt), "/vol/%s", vol);
    snprintf(root, sizeof(root), "%s:/", vol);
    if (Mocha_MountFS(vol, devpath, mnt) != MOCHA_RESULT_SUCCESS)
        return false;
    /* Mocha registers the devoptab even when nothing is mounted there. */
    if (dir_exists(root))
        return true;
    Mocha_UnmountFS(vol);
    return false;
}

/* devpath mounts the volume when nothing has yet; Cafe OS leaves both NANDs unmounted. */
static bool attach_vol(int dev, const char *vol, const char *devpath)
{
    vfs_dev *d = &g_devs[dev];
    snprintf(d->real, sizeof(d->real), "fs:/vol/%s", vol);
    if (dir_exists(d->real))
        return true;
    if (mocha_ready && (mocha_try(vol, NULL) || (devpath && mocha_try(vol, devpath)))) {
        snprintf(mocha_name[dev], sizeof(mocha_name[dev]), "%s", vol);
        snprintf(d->real, sizeof(d->real), "%s:", vol);
        return true;
    }
    d->real[0] = '\0';
    return false;
}

static bool usbfat_on[USB_SLOTS], usbntfs_on[USB_SLOTS];

static void detach_all(void)
{
    for (int i = 0; i < DEV_COUNT; i++) {
        if (mocha_name[i][0])
            Mocha_UnmountFS(mocha_name[i]);
        mocha_name[i][0] = '\0';
    }
    for (int i = 0; i < USB_SLOTS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "usbfat%d", i + 1);
        if (usbfat_on[i])
            usbfat_unmount(name);
        usbfat_on[i] = false;
        snprintf(name, sizeof(name), "usbntfs%d", i + 1);
        if (usbntfs_on[i])
            usbntfs_unmount(i, name);
        usbntfs_on[i] = false;
    }
}

/* Cafe OS mounts a Wii U-formatted drive as /vol/storage_usb01; external01 is the SD card. */
static void mount_usb(void)
{
    for (int i = 0; i < USB_SLOTS; i++) {
        char vol[16];
        vfs_dev *d = &g_devs[DEV_USB0 + i];
        snprintf(vol, sizeof(vol), "storage_usb%02d", i + 1);
        d->mounted = attach_vol(DEV_USB0 + i, vol, NULL);
        d->note[0] = '\0';
        d->fat = false;
        d->always_show = false;
        /* Only a slot Cafe OS didn't mount as WFS is opened raw. */
        bool exfat;
        char name[16];
        snprintf(name, sizeof(name), "usbfat%d", i + 1);
        if (!d->mounted && fsa_ready && mocha_ready && usbfat_mount(fsa_client, i, name, &exfat)) {
            usbfat_on[i] = true;
            d->mounted = true;
            d->fat = !exfat;
            snprintf(d->real, sizeof(d->real), "%s:", name);
            snprintf(d->note, sizeof(d->note), "%s", exfat ? "exFAT" : "FAT32");
        }
        snprintf(name, sizeof(name), "usbntfs%d", i + 1);
        int nt = !d->mounted && fsa_ready && mocha_ready ? usbntfs_mount(fsa_client, i, name) : USBNTFS_NONE;
        if (nt == USBNTFS_MOUNTED) {
            usbntfs_on[i] = true;
            d->mounted = true;
            snprintf(d->real, sizeof(d->real), "%s:", name);
            snprintf(d->note, sizeof(d->note), "NTFS");
        } else if (nt != USBNTFS_NONE) {
            d->always_show = true;
            snprintf(d->note, sizeof(d->note), "%s", nt == USBNTFS_DIRTY ? "NTFS: shut Windows down fully" : "NTFS: can't mount");
        }
    }
}

static void mount_internal(void)
{
    vfs_dev *mlc = &g_devs[DEV_MLC];
    mlc->mounted = attach_vol(DEV_MLC, "storage_mlc01", NULL);
    if (!mlc->mounted)
        snprintf(mlc->note, sizeof(mlc->note), "not accessible");

    vfs_dev *nand = &g_devs[DEV_NAND];
    nand->mounted = attach_vol(DEV_NAND, "storage_slccmpt01", "/dev/slccmpt01");
    if (!nand->mounted)
        snprintf(nand->note, sizeof(nand->note), "needs system access (Aroma / Mocha)");

    vfs_dev *slc = &g_devs[DEV_SLC];
    slc->mounted = attach_vol(DEV_SLC, "storage_slc01", "/dev/slc01");
    if (!slc->mounted)
        snprintf(slc->note, sizeof(slc->note), "needs system access (Aroma / Mocha)");
}

static void fsa_start(void)
{
    if (fsa_ready)
        return;
    FSAInit();
    fsa_client = FSAAddClient(NULL);
    fsa_ready = fsa_client != 0;
    if (fsa_ready && mocha_ready)
        Mocha_UnlockFSClientEx(fsa_client);
}

void vfs_update_space(int dev)
{
    vfs_dev *d = &g_devs[dev];
    d->space_known = false;
    if (!d->mounted)
        return;
    if (dev >= DEV_USB0 && dev < DEV_USB0 + USB_SLOTS && (usbfat_on[dev - DEV_USB0] || usbntfs_on[dev - DEV_USB0])) {
        char root[24];
        struct statvfs sv;
        snprintf(root, sizeof(root), "%s/", d->real);
        if (statvfs(root, &sv) != 0)
            return;
        d->total = (u64)sv.f_blocks * sv.f_frsize;
        d->free = (u64)sv.f_bfree * sv.f_frsize;
        d->space_known = d->total > 0;
        return;
    }
    if (!fsa_ready)
        return;
    char buf[VFS_PATH_MAX];
    const char *fp = fsa_path(d->real, buf, sizeof(buf));
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
    detach_all();
    mount_sd();
    mount_usb();
    mount_internal();
    for (int i = 0; i < DEV_COUNT; i++)
        vfs_update_space(i);
}

void vfs_init(void)
{
    mocha_ready = Mocha_InitLibrary() == MOCHA_RESULT_SUCCESS;
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
    detach_all();
    for (int i = 0; i < DEV_COUNT; i++)
        g_devs[i].mounted = false;
    if (fsa_ready) {
        FSADelClient(fsa_client);
        fsa_ready = false;
    }
    if (mocha_ready) {
        Mocha_DeInitLibrary();
        mocha_ready = false;
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

/* Callers ask for the Konami code first; this keeps a locked drive unwritable even if one forgets. */
static bool unlocked_for_write(const char *path)
{
    if (risk_dev_writable(vfs_dev_of(path)))
        return true;
    g_write_err = "Protected drive is locked (Konami code not entered)";
    return false;
}

bool vfs_mkdir(const char *path)
{
    g_write_err = NULL;
    int dev = vfs_dev_of(path);
    char real[VFS_PATH_MAX];
    if (dev < 0 || arcfs_is_path(path) || !unlocked_for_write(path) || !to_real(path, real, sizeof(real)))
        return false;
    arcfs_flush();
    return mkdir(real, 0777) == 0;
}

bool vfs_remove(const char *path, bool is_dir)
{
    g_write_err = NULL;
    char real[VFS_PATH_MAX];
    if (arcfs_is_path(path) || !unlocked_for_write(path) || !to_real(path, real, sizeof(real)))
        return false;
    arcfs_flush();
    if (is_dir ? rmdir(real) == 0 : unlink(real) == 0)
        return true;
    /* FAT refuses to delete a read-only entry: make it writable and try again */
    char buf[VFS_PATH_MAX];
    if (!fsa_ready || FSAChangeMode(fsa_client, fsa_path(real, buf, sizeof(buf)), 0x666) != (FSError)FS_STATUS_OK)
        return false;
    return is_dir ? rmdir(real) == 0 : unlink(real) == 0;
}

bool vfs_rename(const char *from, const char *to)
{
    g_write_err = NULL;
    int a = vfs_dev_of(from), b = vfs_dev_of(to);
    if (a < 0 || a != b || arcfs_is_path(from) || arcfs_is_path(to) || !unlocked_for_write(from))
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
    if (arcfs_is_path(path) || f->dev < 0 || !unlocked_for_write(path))
        return false;
    char real[VFS_PATH_MAX];
    if (!to_real(path, real, sizeof(real)))
        return false;
    arcfs_flush();
    f->fp = fopen(real, "wb");
    return f->fp != NULL;
}

/* wut reports 512-byte blocks, so stdio would split every copy chunk into 512-byte requests. */
void vfs_unbuffered(vfs_file *f)
{
    if (f->fp)
        setvbuf(f->fp, NULL, _IONBF, 0);
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
