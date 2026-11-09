/*
 * GodModeWii draws 3D models, NW4R layouts and Mii faces with the Wii's GX. Those viewers aren't part
 * of the Wii U port: the file falls back to its text / archive view. (BFRES models have a software
 * viewer of their own: model_view.c.)
 */
#include <stdbool.h>
#include "media.h"

bool lyt_view(const char *path) { (void)path; return false; }
void model_gx_setup(void) {}
void model_gx_init(void) {}

#include "otp.h"
bool otp_read_all(u8 out[OTP_SIZE]) { (void)out; return false; }

#include <stdio.h>
#include <string.h>
#include "wadinstall.h"

int wad_install_paths(char **paths, int n) { (void)paths; (void)n; return 0; }
int wad_install_sd(char **paths, int n) { (void)paths; (void)n; return 0; }

bool wad_console_keys(w2b_keys_t *k, char *why, size_t wn)
{
    memset(k, 0, sizeof(*k));
    snprintf(why, wn, "GodModeU can't read the vWii's own keys from the Wii U side. Export the saves as folders "
                      "instead, or make the data.bin files with GodModeWii in vWii mode.");
    return false;
}

int wad_backup_save(u64 tid, char *where, size_t wsz, u32 *files_before)
{
    (void)tid;
    if (where && wsz) where[0] = '\0';
    if (files_before) *files_before = 0;
    return -1;
}

/* GodModeWii's virtual drives (disc, SEEPROM, memory cards, keys) don't exist here. */
#include "virt.h"
void virt_refresh(int dev) { (void)dev; }
bool virt_list(int dev, const char *rel, vfs_entry **out, int *count) { (void)dev; (void)rel; *out = NULL; *count = 0; return false; }
bool virt_stat(int dev, const char *rel, bool *is_dir, u64 *size) { (void)dev; (void)rel; (void)is_dir; (void)size; return false; }
bool virt_open(vfs_file *f, int dev, const char *rel) { (void)f; (void)dev; (void)rel; return false; }
s32 virt_read(vfs_file *f, void *buf, u32 len) { (void)f; (void)buf; (void)len; return -1; }
bool virt_seek(vfs_file *f, u64 off) { (void)f; (void)off; return false; }
void virt_close(vfs_file *f) { (void)f; }
void virt_shutdown(void) {}
void virt_disc_tolerate(bool on, bool (*cancel)(void)) { (void)on; (void)cancel; }
u32 virt_disc_error_count(void) { return 0; }
u64 virt_disc_error_bytes(void) { return 0; }
bool virt_disc_error_overflow(void) { return false; }
bool virt_disc_error_get(u32 i, u64 *file_off, u64 *disc_off, u64 *len) { (void)i; (void)file_off; (void)disc_off; (void)len; return false; }

/* Mii faces are drawn with GX on the Wii (MiiKit); the Wii U lists the Miis without them. */
#include "miiface.h"
bool miiface_open(char *err, size_t errsz) { if (err && errsz) snprintf(err, errsz, "Mii faces are only drawn on the Wii."); return false; }
void miiface_close(void) {}
bool miiface_draw(const u8 *rec, int expression, int x, int y) { (void)rec; (void)expression; (void)x; (void)y; return false; }
const char *miiface_expression_name(int expression) { (void)expression; return "normal"; }
bool miiface_dolphin_blank(void) { return false; }
