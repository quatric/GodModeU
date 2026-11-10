/*
 * libogc's ASND on the Wii U. See compat/asndlib.h.
 *
 * Every ASND voice is mixed in software into one 48 kHz stereo stream, written into two looping AX
 * voices (left and right, each sent to the TV and the GamePad) a few milliseconds ahead of where AX
 * is playing. The mixing runs on its own thread, so the voice callbacks (which queue the next buffer)
 * run there too, and the players' "interrupts off" sections (_CPU_ISR_Disable) take its lock.
 * The AX side follows SDL2's Wii U audio driver.
 */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <coreinit/cache.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/device.h>
#include <sndcore2/voice.h>

#include "asndlib.h"

#define RATE 48000
#define RING 8192                    /* samples per channel in each AX voice's loop */
#define AHEAD 2048                   /* how far ahead of the play position to keep it filled */
#define CHUNK 256

typedef struct {
    int state;                       /* SND_UNUSED / SND_WORKING */
    bool paused, infinite, starved;
    int format;
    uint32_t step;                   /* source frames per output frame, 16.16 */
    uint64_t pos;                    /* in the current buffer, 16.16 frames */
    const uint8_t *buf;
    uint32_t frames;
    const uint8_t *next;
    uint32_t next_frames;
    int vol_l, vol_r;                /* 0 .. 256 */
    ASNDVoiceCallback cb;
} voice_t;

static voice_t v[MAX_SND_VOICES];
static OSMutex lock;
static bool inited, paused_all;
static volatile bool running;
static OSThread thread;
static uint8_t *stack;
static AXVoice *ax[2];
static int16_t *ring[2];
static uint32_t wpos;
static int32_t mixbuf[CHUNK * 2];

void gmu_audio_lock(void) { if (inited) OSLockMutex(&lock); }
void gmu_audio_unlock(void) { if (inited) OSUnlockMutex(&lock); }

static int frame_bytes(int fmt)
{
    switch (fmt) {
    case VOICE_MONO_8BIT: case VOICE_MONO_8BIT_U: return 1;
    case VOICE_MONO_16BIT: case VOICE_MONO_16BIT_LE: case VOICE_STEREO_8BIT: case VOICE_STEREO_8BIT_U: return 2;
    default: return 4;
    }
}

static inline int16_t s16_le(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static inline void sample(const voice_t *vc, uint32_t i, int *l, int *r)
{
    const uint8_t *p = vc->buf + i * frame_bytes(vc->format);
    switch (vc->format) {
    case VOICE_MONO_8BIT:      *l = *r = (int8_t)p[0] << 8; break;
    case VOICE_MONO_8BIT_U:    *l = *r = ((int)p[0] - 128) << 8; break;
    case VOICE_MONO_16BIT:     *l = *r = *(const int16_t *)p; break;
    case VOICE_MONO_16BIT_LE:  *l = *r = s16_le(p); break;
    case VOICE_STEREO_8BIT:    *l = (int8_t)p[0] << 8; *r = (int8_t)p[1] << 8; break;
    case VOICE_STEREO_8BIT_U:  *l = ((int)p[0] - 128) << 8; *r = ((int)p[1] - 128) << 8; break;
    case VOICE_STEREO_16BIT:   *l = ((const int16_t *)p)[0]; *r = ((const int16_t *)p)[1]; break;
    case VOICE_STEREO_16BIT_LE: *l = s16_le(p); *r = s16_le(p + 2); break;
    }
}

/* The current buffer ran out: move to the queued one, loop, or wait for the callback to queue one. */
static void advance(int n)
{
    voice_t *vc = &v[n];
    if (vc->infinite) {
        vc->pos = 0;
        return;
    }
    if (vc->next) {
        vc->buf = vc->next;
        vc->frames = vc->next_frames;
        vc->next = NULL;
        vc->pos = 0;
        vc->starved = false;
        if (vc->cb) vc->cb(n);
        return;
    }
    if (vc->cb) {
        vc->starved = true;              /* plays silence and asks again every chunk */
        vc->buf = NULL;
        vc->pos = 0;
        vc->cb(n);
        return;
    }
    vc->state = SND_UNUSED;
    vc->buf = NULL;
}

static void mix_chunk(int count)
{
    memset(mixbuf, 0, count * 2 * sizeof(int32_t));
    if (!paused_all) {
        for (int n = 0; n < MAX_SND_VOICES; n++) {
            voice_t *vc = &v[n];
            if (vc->state != SND_WORKING || vc->paused) continue;
            if (vc->starved && !vc->buf) {
                if (vc->cb) vc->cb(n);
                if (!vc->buf) continue;
            }
            for (int i = 0; i < count && vc->state == SND_WORKING && vc->buf; i++) {
                uint32_t f = (uint32_t)(vc->pos >> 16);
                if (f >= vc->frames) {
                    advance(n);
                    if (vc->state != SND_WORKING || !vc->buf) break;
                    f = 0;
                }
                int l = 0, r = 0;
                sample(vc, f, &l, &r);
                mixbuf[i * 2] += l * vc->vol_l >> 8;
                mixbuf[i * 2 + 1] += r * vc->vol_r >> 8;
                vc->pos += vc->step;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        int32_t l = mixbuf[i * 2], r = mixbuf[i * 2 + 1];
        ring[0][wpos] = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        ring[1][wpos] = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        wpos = (wpos + 1) % RING;
    }
}

static void fill(void)
{
    AXVoiceOffsets o;
    AXGetVoiceOffsets(ax[0], &o);
    uint32_t play = o.currentOffset % RING;
    uint32_t ahead = (wpos + RING - play) % RING;
    if (ahead > RING / 2) {               /* fell behind (a long stall): start again just ahead */
        wpos = (play + CHUNK) % RING;
        ahead = CHUNK;
    }
    while (ahead < AHEAD) {
        uint32_t start = wpos;
        int n = CHUNK;
        if (start + n > RING) n = RING - start;
        mix_chunk(n);
        DCFlushRange(ring[0] + start, n * 2);
        DCFlushRange(ring[1] + start, n * 2);
        ahead += n;
    }
}

static int mixer_thread(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    while (running) {
        OSLockMutex(&lock);
        fill();
        OSUnlockMutex(&lock);
        OSSleepTicks(OSMillisecondsToTicks(3));
    }
    return 0;
}

static bool ax_start(void)
{
    if (!AXIsInit()) {
        AXInitParams p = { AX_INIT_RENDERER_48KHZ, { 0 }, AX_INIT_PIPELINE_SINGLE };
        AXInitWithParams(&p);
    }
    for (int c = 0; c < 2; c++) {
        ring[c] = memalign(64, RING * 2);
        if (!ring[c]) return false;
        memset(ring[c], 0, RING * 2);
        DCFlushRange(ring[c], RING * 2);
        ax[c] = AXAcquireVoice(31, NULL, NULL);
        if (!ax[c]) return false;
    }
    for (int c = 0; c < 2; c++) {
        AXVoiceBegin(ax[c]);
        AXSetVoiceType(ax[c], 0);
        AXVoiceVeData ve = { .volume = 0x8000, .delta = 0 };
        AXSetVoiceVe(ax[c], &ve);
        AXVoiceDeviceMixData tv[6], drc[4];
        memset(tv, 0, sizeof(tv));
        memset(drc, 0, sizeof(drc));
        tv[c].bus[0].volume = 0x8000;
        drc[c].bus[0].volume = 0x8000;
        AXSetVoiceDeviceMix(ax[c], AX_DEVICE_TYPE_TV, 0, tv);
        AXSetVoiceDeviceMix(ax[c], AX_DEVICE_TYPE_DRC, 0, drc);
        AXSetVoiceSrcType(ax[c], AX_VOICE_SRC_TYPE_LINEAR);
        AXSetVoiceSrcRatio(ax[c], (float)RATE / (float)AXGetInputSamplesPerSec());
        AXVoiceOffsets o = { .dataType = AX_VOICE_FORMAT_LPCM16, .loopingEnabled = 1, .loopOffset = 0,
                             .endOffset = RING - 1, .currentOffset = 0, .data = ring[c] };
        AXSetVoiceOffsets(ax[c], &o);
        AXVoiceEnd(ax[c]);
    }
    wpos = CHUNK;
    AXSetVoiceState(ax[0], AX_VOICE_STATE_PLAYING);
    AXSetVoiceState(ax[1], AX_VOICE_STATE_PLAYING);
    return true;
}

void ASND_Init(void)
{
    if (inited) return;
    OSInitMutex(&lock);
    memset(v, 0, sizeof(v));
    if (!ax_start()) return;
    inited = true;
    stack = memalign(16, 0x8000);
    running = true;
    if (!stack || !OSCreateThread(&thread, (OSThreadEntryPointFn)mixer_thread, 0, NULL, stack + 0x8000, 0x8000, 10,
                                  OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        running = false;
        return;
    }
    OSSetThreadName(&thread, "GodModeU audio");
    OSResumeThread(&thread);
}

/* The output stays up for the rest of the run (several players share it); this only stops the voices. */
void ASND_End(void)
{
    if (!inited) return;
    OSLockMutex(&lock);
    for (int n = 0; n < MAX_SND_VOICES; n++) v[n].state = SND_UNUSED;
    OSUnlockMutex(&lock);
}

void ASND_Pause(int32_t paused)
{
    if (!inited) return;
    OSLockMutex(&lock);
    paused_all = paused != 0;
    OSUnlockMutex(&lock);
}

int32_t ASND_Is_Paused(void) { return paused_all; }

int32_t ASND_GetFirstUnusedVoice(void)
{
    for (int n = 1; n < MAX_SND_VOICES; n++)
        if (v[n].state == SND_UNUSED) return n;
    return v[0].state == SND_UNUSED ? 0 : SND_INVALID;
}

static int32_t set_voice(int32_t n, int32_t format, int32_t pitch, void *snd, int32_t size, int32_t vl,
                         int32_t vr, ASNDVoiceCallback cb, bool infinite)
{
    if (n < 0 || n >= MAX_SND_VOICES || !snd || size <= 0 || format < 0 || format > 7) return SND_INVALID;
    if (!inited) ASND_Init();
    if (!inited) return SND_INVALID;
    OSLockMutex(&lock);
    voice_t *vc = &v[n];
    memset(vc, 0, sizeof(*vc));
    vc->format = format;
    vc->step = (uint32_t)(((uint64_t)(pitch > 0 ? pitch : RATE) << 16) / RATE);
    vc->buf = snd;
    vc->frames = size / frame_bytes(format);
    vc->vol_l = vl < 0 ? 0 : vl > 255 ? 256 : vl;
    vc->vol_r = vr < 0 ? 0 : vr > 255 ? 256 : vr;
    vc->cb = cb;
    vc->infinite = infinite;
    vc->state = SND_WORKING;
    OSUnlockMutex(&lock);
    return SND_OK;
}

int32_t ASND_SetVoice(int32_t voice, int32_t format, int32_t pitch, int32_t delay, void *snd, int32_t size_snd,
                      int32_t volume_l, int32_t volume_r, ASNDVoiceCallback callback)
{
    (void)delay;
    return set_voice(voice, format, pitch, snd, size_snd, volume_l, volume_r, callback, false);
}

int32_t ASND_SetInfiniteVoice(int32_t voice, int32_t format, int32_t pitch, int32_t delay, void *snd,
                              int32_t size_snd, int32_t volume_l, int32_t volume_r)
{
    (void)delay;
    return set_voice(voice, format, pitch, snd, size_snd, volume_l, volume_r, NULL, true);
}

/* Called from the voice callback (on the mixer thread, which holds the lock: OSMutex is recursive)
 * or from a player. */
int32_t ASND_AddVoice(int32_t n, void *snd, int32_t size)
{
    if (n < 0 || n >= MAX_SND_VOICES || !snd || size <= 0 || !inited) return SND_INVALID;
    OSLockMutex(&lock);
    voice_t *vc = &v[n];
    int32_t r = SND_OK;
    uint32_t frames = size / frame_bytes(vc->format);
    if (vc->state != SND_WORKING) {
        r = SND_INVALID;
    } else if (vc->starved && !vc->buf) {
        vc->buf = snd;
        vc->frames = frames;
        vc->pos = 0;
        vc->starved = false;
    } else if (vc->next) {
        r = SND_BUSY;
    } else {
        vc->next = snd;
        vc->next_frames = frames;
    }
    OSUnlockMutex(&lock);
    return r;
}

int32_t ASND_StopVoice(int32_t n)
{
    if (n < 0 || n >= MAX_SND_VOICES) return SND_INVALID;
    if (!inited) return SND_OK;
    OSLockMutex(&lock);
    v[n].state = SND_UNUSED;
    v[n].buf = v[n].next = NULL;
    OSUnlockMutex(&lock);
    return SND_OK;
}

int32_t ASND_PauseVoice(int32_t n, int32_t pause)
{
    if (n < 0 || n >= MAX_SND_VOICES) return SND_INVALID;
    v[n].paused = pause != 0;
    return SND_OK;
}

int32_t ASND_StatusVoice(int32_t n)
{
    if (n < 0 || n >= MAX_SND_VOICES) return SND_INVALID;
    if (v[n].state != SND_WORKING) return SND_UNUSED;
    return v[n].paused ? SND_WAITING : SND_WORKING;
}

int32_t ASND_ChangeVolumeVoice(int32_t n, int32_t vl, int32_t vr)
{
    if (n < 0 || n >= MAX_SND_VOICES) return SND_INVALID;
    v[n].vol_l = vl < 0 ? 0 : vl > 255 ? 256 : vl;
    v[n].vol_r = vr < 0 ? 0 : vr > 255 ? 256 : vr;
    return SND_OK;
}

int32_t ASND_ChangePitchVoice(int32_t n, int32_t pitch)
{
    if (n < 0 || n >= MAX_SND_VOICES || pitch <= 0) return SND_INVALID;
    v[n].step = (uint32_t)(((uint64_t)pitch << 16) / RATE);
    return SND_OK;
}

int32_t ASND_TestPointer(int32_t n, void *p)
{
    if (n < 0 || n >= MAX_SND_VOICES) return SND_INVALID;
    return v[n].buf == p ? 1 : v[n].next == p ? 2 : 0;
}
