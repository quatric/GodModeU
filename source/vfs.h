#ifndef VFS_H
#define VFS_H

#include <stdio.h>
#include <stdbool.h>
#include <gccore.h>

/* One flat namespace for every storage device the Wii U can mount (GodModeWii's interface, so the
 * code shared with it works unchanged):
 *   sd:/path          SD card                         (read / write)
 *   usb1:/..usb8:/    USB drives: Wii U formatted (WFS), or FAT32 / exFAT / NTFS (read / write) and
 *                     ext2/3/4 (read-only) through Mocha's raw sector access, one entry per partition
 *   mlc:/path         Wii U internal storage (MLC)    (protected: Konami code to change anything)
 *   nand:/path        vWii NAND (SLCCMPT)             (protected, 12 character names)
 *   slc:/path         Wii U system NAND (SLC)         (protected, with an extra warning)
 * Each drive is reached through its Cafe OS mount point ("fs:/vol/...") via wut's FSA devoptab, or
 * through a Mocha devoptab when the app lacks permission, so the normal stdio calls work on it. GodModeWii's Wii-only drives (Gecko, memory cards, SEEPROM, disc,
 * keys) keep their names below so the shared code compiles, but are never mounted here. */

#define USB_SLOTS 8

enum {
    DEV_SD,
    DEV_USB0,
    DEV_USB1,
    DEV_USB2,
    DEV_USB3,
    DEV_USB4,
    DEV_USB5,
    DEV_USB6,
    DEV_USB7,
    DEV_MLC,
    DEV_NAND,
    DEV_SLC,
    /* GodModeWii's, never present on the Wii U */
    DEV_GCA,
    DEV_GCB,
    DEV_SEEPROM,
    DEV_DISC,
    DEV_MCA,
    DEV_MCB,
    DEV_KEYS,
    DEV_COUNT
};

#define VFS_PATH_MAX 512
#define VFS_NAME_MAX 256
#define NAND_NAME_MAX 12

typedef struct {
    char name[VFS_NAME_MAX];
    bool is_dir;
    u64 size;
} vfs_entry;

typedef struct {
    int dev;
    FILE *fp;
    s32 fd;
    u64 size;
    int vkind;
    u64 pos;       /* archive reads keep their own position */
    u64 base;
    void *ctx;     /* archive entry */
    bool arc;      /* a file inside an archive (read-only) */
} vfs_file;

typedef struct {
    const char *prefix;  /* "sd:" */
    const char *root;    /* "sd:/" */
    const char *name;    /* "SD Card" */
    const char *tag;     /* "SD" */
    bool always_show;    /* listed on the home screen even when not mounted */
    bool mounted;
    bool space_known;
    u64 total;
    u64 free;
    char note[48];
    bool exfat;          /* no 4 GiB file limit */
    bool maybe_wiiu;
    bool ntfs;
    u8 ext;
    char real[VFS_PATH_MAX];  /* "fs:/vol/..." or a Mocha / USB devoptab, "storage_usb01:" (no trailing slash) */
    bool fat;                 /* FAT32, with its 4 GiB file size limit */
    bool readonly;            /* ext2/3/4: mounted read-only */
} vfs_dev;

extern vfs_dev g_devs[DEV_COUNT];
extern bool g_is_vwii;      /* always false on the Wii U side */
extern bool g_is_dolphin;
extern bool g_nand_full;
extern const char *g_write_err; /* why the last write / close failed, if a backend says so */

void vfs_init(void);
void vfs_shutdown(void);
void vfs_remount(void);                 /* re-detect SD / USB / internal storage, refresh space */
void vfs_update_space(int dev);

int  vfs_dev_of(const char *path);      /* -1 if unknown */
bool vfs_is_protected(int dev);         /* MLC / NAND / SLC: changing anything asks for the Konami code */
bool vfs_is_virtual(int dev);           /* GodModeWii's virtual drives: none here */
bool vfs_is_card(int dev);
bool vfs_is_readonly(const char *path); /* archives and ext drives are read-only */
bool vfs_is_archive(const char *path);  /* inside an archive: "sd:/a.wad|/x" */
/* The path as the user sees it: "sd:/a.wad|/x" -> "sd:/a.wad/x" (the '|' only marks an archive). */
void vfs_shown_path(const char *path, char *out, size_t n);
const char *vfs_dev_root(int dev);      /* "sd:/" */
bool vfs_is_root(const char *path);     /* "sd:/" */
bool vfs_parent(const char *path, char *out, size_t outsz);
const char *vfs_basename(const char *path);
bool vfs_join(char *out, size_t outsz, const char *dir, const char *name);
bool vfs_is_within(const char *path, const char *dir); /* path == dir or below */
/* The Cafe OS path ("/vol/...") of a vfs path on a real drive, for the system calls that want one. */
bool vfs_os_path(const char *path, char *out, size_t outsz);

bool vfs_list(const char *path, vfs_entry **out, int *count);      /* everything, for copy / delete */
bool vfs_list_visible(const char *path, vfs_entry **out, int *count); /* without OS junk, for display */
bool vfs_is_hidden_name(const char *name);
bool vfs_stat(const char *path, bool *is_dir, u64 *size);
bool vfs_exists(const char *path);
bool vfs_mkdir(const char *path);
bool vfs_remove(const char *path, bool is_dir);   /* empty dirs only */
bool vfs_rename(const char *from, const char *to);

/* GodModeWii reads raw USB sectors for drives libfat can't mount; Cafe OS mounts them itself here. */
static inline bool vfs_usb_raw_read(int slot, u64 off, void *buf, u32 len)
{
    (void)slot; (void)off; (void)buf; (void)len;
    return false;
}
static inline u64 vfs_usb_raw_size(int slot) { (void)slot; return 0; }

bool vfs_open_read(vfs_file *f, const char *path);
bool vfs_open_write(vfs_file *f, const char *path);   /* create / truncate */
void vfs_unbuffered(vfs_file *f);                     /* for big sequential copies */
s32  vfs_read(vfs_file *f, void *buf, u32 len);
s32  vfs_write(vfs_file *f, const void *buf, u32 len);
bool vfs_seek(vfs_file *f, u64 off);
void vfs_close(vfs_file *f);
bool vfs_close_checked(vfs_file *f);
static inline void vfs_clone_attrs(const char *from, const char *to) { (void)from; (void)to; }

#endif
