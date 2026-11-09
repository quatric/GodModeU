/*
 * The 640 x 480 YUY2 framebuffer on the TV and the GamePad. See screen.h.
 *
 * OSScreen gives each screen two RGBA buffers (0xRRGGBBAA) and shows one while the other is drawn.
 * There is no call that says which one is the back buffer, so a marker pixel finds it (and the pitch),
 * as GodModeU's first OSScreen code did. Each buffer remembers a hash of every framebuffer row it
 * holds, so a frame only converts the rows that changed since that buffer was drawn, and nothing is
 * flipped while the picture stands still.
 */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <coreinit/cache.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/mutex.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <proc_ui/procui.h>

#include "screen.h"

#define TV_W 1280
#define TV_H 720
#define TV_X0 160                   /* 960 x 720 picture centered on the TV */
#define DRC_W 854
#define DRC_H 480
#define HEAP_TAG 0x474D5553

static uint8_t *xfb;
static const uint8_t *volatile src_buf;   /* what is shown: xfb or a video frame */
static void *mem;                         /* both screens' buffers, in the MEM1 frame heap */
static uint32_t tv_size, drc_size;
static volatile bool ready, running;
static OSMutex lock;
static OSThread thread;
static uint8_t *stack;
static volatile uint32_t frames;
static volatile uint32_t border = 0x00800080;
static int border_dirty[2];               /* per screen: buffers whose border is stale */

typedef struct {
    OSScreenID id;
    uint32_t *half[2];
    int pitch;
    uint32_t hash[2][SCREEN_H];            /* what each buffer holds, per framebuffer row */
    bool valid[2];
} scr_t;
static scr_t scr[2];

/* ---------------------------------------------------------------- colour */

static int16_t t_y[256], t_rv[256], t_gu[256], t_gv[256], t_bu[256];
static uint8_t clamp_tab[1024];
#define CL(v) clamp_tab[(v) + 384]

static void tables_init(void)
{
    for (int i = 0; i < 256; i++) {
        t_y[i] = (int16_t)((298 * (i - 16)) >> 8);
        t_rv[i] = (int16_t)((409 * (i - 128)) >> 8);
        t_gu[i] = (int16_t)((100 * (i - 128)) >> 8);
        t_gv[i] = (int16_t)((208 * (i - 128)) >> 8);
        t_bu[i] = (int16_t)((516 * (i - 128)) >> 8);
    }
    for (int i = 0; i < 1024; i++) {
        int v = i - 384;
        clamp_tab[i] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
}

static inline uint32_t rgba(int y, int u, int v)
{
    int yy = t_y[y];
    return (uint32_t)CL(yy + t_rv[v]) << 24 | (uint32_t)CL(yy - t_gu[u] - t_gv[v]) << 16 |
           (uint32_t)CL(yy + t_bu[u]) << 8 | 0xff;
}

static inline uint32_t avg(uint32_t a, uint32_t b)
{
    return ((a >> 1) & 0x7f7f7f7f) + ((b >> 1) & 0x7f7f7f7f) + (a & b & 0x01010101);
}

/* One framebuffer row to 640 RGBA pixels. */
static void row_rgba(const uint8_t *s, uint32_t *out)
{
    for (int x = 0; x < SCREEN_W; x += 2, s += 4) {
        out[x] = rgba(s[0], s[1], s[3]);
        out[x + 1] = rgba(s[2], s[1], s[3]);
    }
}

static uint32_t row_hash(const uint8_t *row)
{
    const uint32_t *w = (const uint32_t *)row;
    uint32_t h = 2166136261u;
    for (int i = 0; i < SCREEN_W / 2; i += 4)
        h = ((h ^ w[i]) * 16777619u ^ w[i + 1] ^ (w[i + 2] << 7) ^ (w[i + 3] >> 3)) * 16777619u;
    return h;
}

static uint32_t border_rgba(void)
{
    uint32_t b = border;
    return rgba(b >> 24, b >> 16 & 0xff, b & 0xff);
}

/* ---------------------------------------------------------------- buffers */

/* Which half OSScreen draws into next (and the pitch), by a marker pixel at (0, 0) and (0, 1). */
static int back_half(scr_t *s)
{
    const uint32_t mark = 0x01020300;
    uint32_t bg = border_rgba();
    OSScreenPutPixelEx(s->id, 0, 0, mark);
    OSScreenPutPixelEx(s->id, 0, 1, mark);
    int which = -1;
    for (int i = 0; i < 2 && which < 0; i++)
        if (s->half[i][0] == mark)
            which = i;
    if (which >= 0 && !s->pitch)
        for (int i = 1; i < 4096; i++)
            if (s->half[which][i] == mark) {
                s->pitch = i;
                break;
            }
    OSScreenPutPixelEx(s->id, 0, 0, bg);
    OSScreenPutPixelEx(s->id, 0, 1, bg);
    return which;
}

static void fill_border(scr_t *s, uint32_t *buf, int w, int h, int x0, int pw, int py0, int ph)
{
    uint32_t c = border_rgba();
    for (int y = 0; y < h; y++) {
        uint32_t *line = buf + y * s->pitch;
        if (y < py0 || y >= py0 + ph) {
            for (int x = 0; x < w; x++) line[x] = c;
        } else {
            for (int x = 0; x < x0; x++) line[x] = c;
            for (int x = x0 + pw; x < w; x++) line[x] = c;
        }
    }
    DCFlushRange(buf, (uint32_t)s->pitch * h * 4);
}

static uint32_t cur_hash[SCREEN_H];
static uint32_t rowbuf[2][SCREEN_W];

/* TV: two framebuffer rows -> three screen rows (the middle one their average), and two pixels ->
 * three the same way, so the 1.5 scale stays even. */
static void draw_tv(scr_t *s, int half, const uint8_t *src)
{
    uint32_t *buf = s->half[half];
    if (border_dirty[0] & (1 << half)) {
        fill_border(s, buf, TV_W, TV_H, TV_X0, 960, 0, TV_H);
        border_dirty[0] &= ~(1 << half);
        s->valid[half] = false;
    }
    for (int r = 0; r < SCREEN_H; r += 2) {
        if (s->valid[half] && s->hash[half][r] == cur_hash[r] && s->hash[half][r + 1] == cur_hash[r + 1])
            continue;
        row_rgba(src + r * SCREEN_W * 2, rowbuf[0]);
        row_rgba(src + (r + 1) * SCREEN_W * 2, rowbuf[1]);
        int y = r * 3 / 2;
        uint32_t *l0 = buf + y * s->pitch + TV_X0, *l1 = l0 + s->pitch, *l2 = l1 + s->pitch;
        for (int x = 0, o = 0; x < SCREEN_W; x += 2, o += 3) {
            uint32_t a = rowbuf[0][x], b = rowbuf[0][x + 1], c = rowbuf[1][x], d = rowbuf[1][x + 1];
            uint32_t ab = avg(a, b), cd = avg(c, d);
            l0[o] = a;          l0[o + 1] = ab;          l0[o + 2] = b;
            l1[o] = avg(a, c);  l1[o + 1] = avg(ab, cd); l1[o + 2] = avg(b, d);
            l2[o] = c;          l2[o + 1] = cd;          l2[o + 2] = d;
        }
        DCFlushRange(l0, 960 * 4);
        DCFlushRange(l1, 960 * 4);
        DCFlushRange(l2, 960 * 4);
        s->hash[half][r] = cur_hash[r];
        s->hash[half][r + 1] = cur_hash[r + 1];
    }
    s->valid[half] = true;
}

/* GamePad: 1:1. */
static void draw_drc(scr_t *s, int half, const uint8_t *src)
{
    uint32_t *buf = s->half[half];
    if (border_dirty[1] & (1 << half)) {
        fill_border(s, buf, DRC_W, DRC_H, SCREEN_DRC_X0, SCREEN_W, 0, DRC_H);
        border_dirty[1] &= ~(1 << half);
        s->valid[half] = false;
    }
    for (int r = 0; r < SCREEN_H; r++) {
        if (s->valid[half] && s->hash[half][r] == cur_hash[r])
            continue;
        uint32_t *l = buf + r * s->pitch + SCREEN_DRC_X0;
        row_rgba(src + r * SCREEN_W * 2, l);
        DCFlushRange(l, SCREEN_W * 4);
        s->hash[half][r] = cur_hash[r];
    }
    s->valid[half] = true;
}

static bool same_as(scr_t *s, int half)
{
    if (!s->valid[half]) return false;
    return !memcmp(s->hash[half], cur_hash, sizeof(cur_hash));
}

static void present(void)
{
    OSLockMutex(&lock);
    if (!ready) {
        OSUnlockMutex(&lock);
        return;
    }
    const uint8_t *src = src_buf ? src_buf : xfb;
    for (int r = 0; r < SCREEN_H; r++)
        cur_hash[r] = row_hash(src + r * SCREEN_W * 2);
    for (int i = 0; i < 2; i++) {
        scr_t *s = &scr[i];
        int back = back_half(s);
        if (back < 0 || !s->pitch)
            continue;
        /* the front buffer already shows this picture */
        if (same_as(s, back ^ 1) && !(border_dirty[i] & (1 << (back ^ 1))))
            continue;
        if (i == 0) draw_tv(s, back, src);
        else draw_drc(s, back, src);
        OSScreenFlipBuffersEx(s->id);
    }
    frames++;
    OSUnlockMutex(&lock);
}

static int present_thread(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    while (running) {
        OSTime start = OSGetSystemTime();
        present();
        OSTime spent = OSGetSystemTime() - start, frame = OSMicrosecondsToTicks(16667);
        OSSleepTicks(spent < frame ? frame - spent : OSMillisecondsToTicks(1));
    }
    return 0;
}

/* ---------------------------------------------------------------- foreground */

static uint32_t acquired(void *ctx)
{
    (void)ctx;
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    MEMRecordStateForFrmHeap(heap, HEAP_TAG);
    tv_size = OSScreenGetBufferSizeEx(SCREEN_TV);
    drc_size = OSScreenGetBufferSizeEx(SCREEN_DRC);
    mem = MEMAllocFromFrmHeapEx(heap, tv_size + drc_size, 0x100);
    if (!mem)
        return 0;
    OSLockMutex(&lock);
    OSScreenSetBufferEx(SCREEN_TV, mem);
    OSScreenSetBufferEx(SCREEN_DRC, (uint8_t *)mem + tv_size);
    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);
    scr[0].id = SCREEN_TV;
    scr[0].half[0] = mem;
    scr[0].half[1] = (uint32_t *)((uint8_t *)mem + tv_size / 2);
    scr[1].id = SCREEN_DRC;
    scr[1].half[0] = (uint32_t *)((uint8_t *)mem + tv_size);
    scr[1].half[1] = (uint32_t *)((uint8_t *)mem + tv_size + drc_size / 2);
    for (int i = 0; i < 2; i++) {
        scr[i].pitch = 0;
        scr[i].valid[0] = scr[i].valid[1] = false;
        border_dirty[i] = 3;
    }
    ready = true;
    OSUnlockMutex(&lock);
    return 0;
}

static uint32_t released(void *ctx)
{
    (void)ctx;
    OSLockMutex(&lock);
    bool was = ready;
    ready = false;
    OSUnlockMutex(&lock);
    if (was) {
        MEMFreeByStateToFrmHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1), HEAP_TAG);
        mem = NULL;
    }
    return 0;
}

/* ---------------------------------------------------------------- API */

bool screen_init(void)
{
    tables_init();
    OSInitMutex(&lock);
    xfb = memalign(64, SCREEN_W * SCREEN_H * 2);
    if (!xfb)
        return false;
    screen_clear(0x00800080);
    OSScreenInit();
    acquired(NULL);
    ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, acquired, NULL, 100);
    ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, released, NULL, 100);

    stack = memalign(16, 0x10000);
    running = true;
    if (stack && OSCreateThread(&thread, present_thread, 0, NULL, stack + 0x10000, 0x10000, 16,
                                OS_THREAD_ATTRIB_AFFINITY_CPU2)) {
        OSSetThreadName(&thread, "GodModeU screen");
        OSResumeThread(&thread);
    } else {
        running = false;
    }
    return ready;
}

void screen_shutdown(void)
{
    if (running) {
        running = false;
        int r;
        OSJoinThread(&thread, &r);
    }
    if (ready) {
        OSScreenShutdown();
        released(NULL);
    }
}

uint8_t *screen_xfb(void) { return xfb; }

void screen_show(const uint8_t *yuy2) { src_buf = yuy2; }

void screen_set_border(uint32_t yuy2_pair)
{
    if (border != yuy2_pair) {
        border = yuy2_pair;
        border_dirty[0] = border_dirty[1] = 3;
    }
}

void screen_clear(uint32_t yuy2_pair)
{
    uint32_t *p = (uint32_t *)xfb;
    for (int i = 0; i < SCREEN_W * SCREEN_H / 2; i++)
        p[i] = yuy2_pair;
}

void screen_vsync(void)
{
    if (!running) {
        present();
        OSSleepTicks(OSMillisecondsToTicks(16));
        return;
    }
    uint32_t f = frames;
    OSTime until = OSGetSystemTime() + OSMillisecondsToTicks(40);
    while (frames == f && OSGetSystemTime() < until)
        OSSleepTicks(OSMicrosecondsToTicks(500));
}

void screen_flush(void)
{
    present();
}
