/*
 * GodModeU - a GodMode9-style, text-only file manager for the Wii U.
 * See README.md for credits and the brick-risk warning.
 */
#include <stdio.h>
#include <stdlib.h>
#include <coreinit/systeminfo.h>
#include <coreinit/title.h>
#include <coreinit/time.h>
#include <whb/proc.h>
#include <sysapp/launch.h>

#include "app.h"
#include "ui.h"
#include "input.h"
#include "vfs.h"
#include "risk.h"
#include "ftp.h"

/* The same title IDs WHBProcInit treats as the Homebrew Launcher. */
static bool launched_from_hbl(void)
{
    uint64_t id = OSGetTitleID();
    return id == 0x0005000013374842ULL || id == 0x000500101004A000ULL ||
           id == 0x000500101004A100ULL || id == 0x000500101004A200ULL;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* HOME opens the app's own SYSTEM menu; this must come before ProcUI starts. */
    OSEnableHomeButtonMenu(FALSE);
    WHBProcInit();
    ui_init();
    ui_splash("Starting...");

    input_init();
    ui_splash("Looking for drives...");
    uint64_t began = OSTicksToMilliseconds(OSGetSystemTime());
    vfs_init();
    bool toggled = risk_startup_poll(2500, began);
    risk_load_forced();
    if (toggled) {
        if (!risk_toggle_forced())
            ui_message("RISKY MODE", C_RED, "Could not save the setting. Is the SD card writable?");
        else if (risk_forced())
            ui_message("RISKY MODE FORCED ON", C_RED,
                       "Dangerous permissions are now ALWAYS enabled, and stay that way every time this program starts. "
                       "Nothing will ask for a warning or code first.");
        else
            ui_message("RISKY MODE OFF", C_GREEN, "Dangerous permissions are no longer forced. Risky changes ask for the warning and code again.");
    } else if (risk_forced()) {
        ui_message("RISKY MODE FORCED ON", C_RED,
                   "Dangerous permissions are ALWAYS enabled. Nothing will ask for a warning or code first.");
    }

    int mode = app_run();

    ui_splash("Unmounting...");
    risk_lock();
    ftp_net_shutdown();
    vfs_shutdown();
    input_shutdown();

    /* g_quit_request means ProcUI already ended the app. Outside the Homebrew
     * Launcher a title may only leave through the Wii U Menu, with ProcUI running. */
    if (!g_quit_request) {
        if (!launched_from_hbl()) {
            SYSLaunchMenu();
            while (WHBProcIsRunning()) {}
        } else if (mode == EXIT_MENU) {
            SYSLaunchMenu();
        }
    }
    /* Only after the foreground is handed back: shutting OSScreen down first left
     * the Wii U Menu black on both screens. */
    ui_shutdown();
    WHBProcShutdown();
    return 0;
}
