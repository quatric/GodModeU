#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <whb/proc.h>
#include <wiiuupdate.h>

#include "update.h"
#include "app.h"
#include "ui.h"
#include "input.h"

static int quit_check(void)
{
    return !WHBProcIsRunning();
}

void update_start(void)
{
    WIIUUPDATE_SetQuitCheck(quit_check);
    WIIUUPDATE_Start("quatric", "GodModeU", GMW_VERSION);
}

int update_poll(void)
{
    if (!WIIUUPDATE_TakeNotice())
        return 0;

    char text[320];
    snprintf(text, sizeof(text), "%s\n\n%s", WIIUUPDATE_Message(),
             WIIUUPDATE_CanInstall() ? "Download and install it now?" : "Get it from:\n" "github.com/quatric/GodModeU/releases");
    if (!WIIUUPDATE_CanInstall()) {
        ui_message("UPDATE AVAILABLE", C_CYAN, text);
        return 1;
    }
    if (!ui_confirm("UPDATE AVAILABLE", C_CYAN, text))
        return 1;

    if (WIIUUPDATE_Install() != 0) {
        ui_message("UPDATE", C_RED, "Couldn't start the update.");
        return 1;
    }
    ui_busy_begin("update");
    for (;;) {
        wiiuupdate_install_state st = WIIUUPDATE_InstallState();
        if (st == WIIUUPDATE_INSTALL_DONE || st == WIIUUPDATE_INSTALL_FAILED || !WHBProcIsRunning())
            break;
        if (st == WIIUUPDATE_INSTALL_DOWNLOADING && input_cancel_held())
            WIIUUPDATE_Cancel();
        ui_busy_progress((u64)WIIUUPDATE_Progress(), 100);
        ui_busy_tick();
        usleep(50000);
    }
    ui_busy_end();

    if (WIIUUPDATE_InstallState() == WIIUUPDATE_INSTALL_DONE) {
        ui_message("UPDATED", C_GREEN, "The new version is installed.\n\nGodModeU will close now; start it again from the Wii U Menu.");
        return 2;
    }
    snprintf(text, sizeof(text), "The update failed: %s.\n\nThe installed copy was not changed.", WIIUUPDATE_InstallError());
    ui_message("UPDATE FAILED", C_RED, text);
    return 1;
}

void update_shutdown(void)
{
    WIIUUPDATE_Shutdown();
}
