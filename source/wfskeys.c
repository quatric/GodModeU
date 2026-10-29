/*
 * Saves the two keys GodModeWii needs to read this console's Wii U drives from vWii,
 * where it can't read the Wii U's OTP or SEEPROM itself.
 */
#include <stdio.h>
#include <string.h>
#include <mocha/mocha.h>
#include <mocha/otp.h>

#include "wfskeys.h"
#include "aes.h"
#include "ui.h"
#include "vfs.h"

static bool save_key(const char *path, const u8 key[16])
{
    vfs_file f;
    if (!vfs_open_write(&f, path))
        return false;
    bool ok = vfs_write(&f, key, 16) == 16;
    return vfs_close_checked(&f) && ok;
}

void wfskeys_save(void)
{
    if (!g_devs[DEV_SD].mounted) {
        ui_message("SAVE WII U KEYS", C_YELLOW, "Insert an SD card first.");
        return;
    }
    if (!ui_confirm("SAVE WII U KEYS", C_RED,
                    "This writes sd:/wfs/usb.key and sd:/wfs/mlc.key: the keys that decrypt THIS console's "
                    "Wii U drive and internal storage, so GodModeWii can read them (read-only) from vWii.\n\n"
                    "They are unique to your console. Keep them private and never share them.\n\nSave them now?"))
        return;

    static WiiUConsoleOTP otp;
    static u8 seeprom[0x200];
    u8 usb[16], mlc[16];
    bool read = Mocha_ReadOTP(&otp) == MOCHA_RESULT_SUCCESS &&
                Mocha_SEEPROMRead(seeprom, 0, sizeof(seeprom)) == MOCHA_RESULT_SUCCESS;
    if (read) {
        /* The same derivation GodModeWii applies to a full otp.bin / seeprom.bin. */
        memcpy(mlc, otp.wiiUBank.mlcKey, 16);
        aes128_ecb_encrypt(otp.wiiUBank.usbStorageSeedsKey, seeprom + 0xB0, usb);
    }
    memset(&otp, 0, sizeof(otp));
    memset(seeprom, 0, sizeof(seeprom));
    if (!read) {
        ui_message("SAVE WII U KEYS", C_RED, "Couldn't read the console's keys. This needs Aroma with Mocha.");
        return;
    }

    vfs_mkdir("sd:/wfs");
    bool ok = save_key("sd:/wfs/usb.key", usb) && save_key("sd:/wfs/mlc.key", mlc);
    memset(usb, 0, sizeof(usb));
    memset(mlc, 0, sizeof(mlc));
    if (ok)
        ui_message("KEYS SAVED", C_GREEN,
                   "Saved to sd:/wfs/usb.key and sd:/wfs/mlc.key.\n\n"
                   "GodModeWii can now open this console's Wii U drive (read-only). Keep these files private.");
    else
        ui_message("SAVE WII U KEYS", C_RED, "Couldn't write to sd:/wfs/. Is the SD card writable?");
}
