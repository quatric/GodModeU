/*
 * GodModeU - a GodMode9-style, text-only file manager for the Wii U.
 * See README.md for credits and the brick-risk warning.
 */
#include <stdio.h>
#include <stdlib.h>
#include <whb/proc.h>
#include <sysapp/launch.h>

#include "app.h"
#include "ui.h"
#include "input.h"
#include "vfs.h"
#include "risk.h"
#include "ftp.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    WHBProcInit();
    ui_init();
    ui_splash("Starting...");

    input_init();
    ui_splash("Looking for drives...");
    vfs_init();

    int mode = app_run();

    ui_splash("Unmounting...");
    risk_lock();
    ftp_net_shutdown();
    vfs_shutdown();
    input_shutdown();
    ui_shutdown();

    if (mode == EXIT_MENU)
        SYSLaunchMenu();
    WHBProcShutdown();
    return 0;
}
