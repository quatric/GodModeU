/*
 * Text entry with the Wii U's own software keyboard (nn::swkbd, the system keyboard applet that games and
 * the Wii U Menu use), drawn through GX2 while it is up (screen_gx2_*). Started once and kept; if it can't
 * be (no keyboard resources, no memory), ui.c falls back to GodModeWii's character roller.
 */
#include <string.h>
#include <malloc.h>
#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/mcp.h>
#include <vpad/input.h>
#include <nn/swkbd.h>

extern "C" {
#include "screen.h"
}
#include "swkbd.h"

static bool created, failed;
static FSClient *fs;
static void *work;

static nn::swkbd::RegionType region(void)
{
    alignas(0x40) static MCPSysProdSettings set;
    int h = MCP_Open();
    if (h < 0) return nn::swkbd::RegionType::USA;
    MCPError e = MCP_GetSysProdSettings(h, &set);
    MCP_Close(h);
    if (e < 0) return nn::swkbd::RegionType::USA;
    switch (set.product_area) {
    case MCP_REGION_JAPAN:  return nn::swkbd::RegionType::Japan;
    case MCP_REGION_EUROPE: return nn::swkbd::RegionType::Europe;
    case MCP_REGION_CHINA:  return nn::swkbd::RegionType::China;
    case MCP_REGION_KOREA:  return nn::swkbd::RegionType::Korea;
    case MCP_REGION_TAIWAN: return nn::swkbd::RegionType::Taiwan;
    default:                return nn::swkbd::RegionType::USA;
    }
}

static bool start(void)
{
    if (created) return true;
    if (failed) return false;
    failed = true;                                  /* until it works: one try a session */
    FSInit();
    fs = (FSClient *)MEMAllocFromDefaultHeap(sizeof(FSClient));
    if (!fs) return false;
    if (FSAddClient(fs, FS_ERROR_FLAG_NONE) < 0) { MEMFreeToDefaultHeap(fs); fs = nullptr; return false; }
    work = MEMAllocFromDefaultHeap(nn::swkbd::GetWorkMemorySize(0));
    if (!work) return false;
    nn::swkbd::CreateArg arg;
    arg.workMemory = work;
    arg.regionType = region();
    arg.fsClient = fs;
    if (!nn::swkbd::Create(arg)) {
        MEMFreeToDefaultHeap(work);
        work = nullptr;
        return false;
    }
    created = true;
    failed = false;
    return true;
}

/* UTF-8 -> UTF-16 (BMP only; anything else becomes '?') */
static void to_utf16(const char *s, char16_t *out, size_t n)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && o + 1 < n) {
        unsigned c = *p++;
        if (c >= 0xF0) { c = '?'; while ((*p & 0xC0) == 0x80) p++; }
        else if (c >= 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0xC0 && (p[0] & 0xC0) == 0x80) {
            c = (c & 0x1F) << 6 | (p[0] & 0x3F);
            p += 1;
        } else if (c >= 0x80) c = '?';
        out[o++] = (char16_t)c;
    }
    out[o] = 0;
}

/* UTF-16 -> UTF-8, at most max bytes (whole characters only) */
static void to_utf8(const char16_t *s, char *out, int max)
{
    int o = 0;
    for (; s && *s; s++) {
        unsigned c = *s;
        char b[3];
        int n;
        if (c >= 0xD800 && c < 0xE000) { b[0] = '?'; n = 1; }   /* surrogate pairs: not in file names here */
        else if (c < 0x80) { b[0] = (char)c; n = 1; }
        else if (c < 0x800) { b[0] = (char)(0xC0 | c >> 6); b[1] = (char)(0x80 | (c & 0x3F)); n = 2; }
        else { b[0] = (char)(0xE0 | c >> 12); b[1] = (char)(0x80 | (c >> 6 & 0x3F)); b[2] = (char)(0x80 | (c & 0x3F)); n = 3; }
        if (o + n > max) break;
        memcpy(out + o, b, n);
        o += n;
    }
    out[o] = '\0';
}

static void draw_tv(void) { nn::swkbd::DrawTV(); }
static void draw_drc(void) { nn::swkbd::DrawDRC(); }

/* One frame of the keyboard: the GamePad's buttons and touch screen, then the picture. */
static void frame(void)
{
    VPADStatus vpad;
    VPADReadError err = VPAD_READ_SUCCESS;
    memset(&vpad, 0, sizeof(vpad));
    VPADRead(VPAD_CHAN_0, &vpad, 1, &err);
    VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpad.tpNormal, &vpad.tpNormal);
    nn::swkbd::ControllerInfo info;
    info.vpad = err == VPAD_READ_SUCCESS ? &vpad : nullptr;
    nn::swkbd::Calc(info);
    if (nn::swkbd::IsNeedCalcSubThreadFont()) nn::swkbd::CalcSubThreadFont();
    if (nn::swkbd::IsNeedCalcSubThreadPredict()) nn::swkbd::CalcSubThreadPredict();
    screen_gx2_frame(draw_tv, draw_drc);
}

extern "C" int swkbd_input(const char *hint, char *buf, int maxlen)
{
    if (!start()) return -1;
    if (!screen_gx2_begin()) return -1;

    static char16_t init[512], hint16[128];
    to_utf16(buf, init, sizeof(init) / sizeof(init[0]));
    to_utf16(hint, hint16, sizeof(hint16) / sizeof(hint16[0]));
    nn::swkbd::AppearArg arg;
    arg.keyboardArg.configArg.languageType = nn::swkbd::LanguageType::English;
    arg.inputFormArg.initialText = init;
    arg.inputFormArg.hintText = hint16;
    arg.inputFormArg.maxTextLength = maxlen;
    arg.inputFormArg.higlightInitialText = true;
    if (!nn::swkbd::AppearInputForm(arg)) {
        screen_gx2_end();
        return -1;
    }
    nn::swkbd::MuteAllSound(false);

    int result;
    for (;;) {
        frame();
        if (nn::swkbd::IsDecideOkButton(nullptr)) { result = 1; break; }
        if (nn::swkbd::IsDecideCancelButton(nullptr)) { result = 0; break; }
    }
    if (result == 1) to_utf8(nn::swkbd::GetInputFormString(), buf, maxlen);
    nn::swkbd::DisappearInputForm();
    for (int i = 0; i < 120 && nn::swkbd::GetStateInputForm() != nn::swkbd::State::Hidden; i++)
        frame();                                    /* let it slide away */
    screen_gx2_end();
    return result;
}
