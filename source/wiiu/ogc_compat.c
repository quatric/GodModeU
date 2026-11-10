/*
 * libogc calls used by the code shared with GodModeWii, on Cafe OS: LWP threads and mutexes on
 * OSThread / OSMutex, VIDEO_WaitVSync on the screen thread (screen.c).
 */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>

#include <gccore.h>
#include "screen.h"

#define MAX_THREADS 8

typedef struct {
    OSThread t;
    void *(*entry)(void *);
    void *arg, *ret;
    uint8_t *stack;
    bool used;
} lthread;

static lthread threads[MAX_THREADS];

static int trampoline(int argc, const char **argv)
{
    (void)argc;
    lthread *t = (lthread *)argv;
    t->ret = t->entry(t->arg);
    return 0;
}

int32_t LWP_CreateThread(lwp_t *out, void *(*entry)(void *), void *arg, void *stackbase, uint32_t stack_size,
                         uint8_t prio)
{
    (void)stackbase;
    int i = 0;
    while (i < MAX_THREADS && threads[i].used) i++;
    if (i == MAX_THREADS) return -1;
    lthread *t = &threads[i];
    memset(t, 0, sizeof(*t));
    if (stack_size < 0x8000) stack_size = 0x8000;
    t->stack = memalign(16, stack_size);
    if (!t->stack) return -1;
    t->entry = entry;
    t->arg = arg;
    /* libogc: 0 (lowest) .. 127 (highest); Cafe OS: 0 (highest) .. 31 (lowest) */
    int p = 31 - prio / 4;
    if (p < 1) p = 1;
    if (!OSCreateThread(&t->t, (OSThreadEntryPointFn)trampoline, 0, (char *)t, t->stack + stack_size, stack_size, p,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        free(t->stack);
        return -1;
    }
    t->used = true;
    OSResumeThread(&t->t);
    if (out) *out = (lwp_t)(i + 1);
    return 0;
}

int32_t LWP_JoinThread(lwp_t th, void **value)
{
    if (th < 1 || th > MAX_THREADS || !threads[th - 1].used) return -1;
    lthread *t = &threads[th - 1];
    int r;
    OSJoinThread(&t->t, &r);
    if (value) *value = t->ret;
    free(t->stack);
    t->used = false;
    return 0;
}

int32_t LWP_MutexInit(mutex_t *m, bool recursive)
{
    (void)recursive;                  /* OSMutex is always recursive */
    OSMutex *x = malloc(sizeof(OSMutex));
    if (!x) return -1;
    OSInitMutex(x);
    *m = (mutex_t)(uintptr_t)x;
    return 0;
}

int32_t LWP_MutexLock(mutex_t m) { if (m) OSLockMutex((OSMutex *)(uintptr_t)m); return 0; }
int32_t LWP_MutexUnlock(mutex_t m) { if (m) OSUnlockMutex((OSMutex *)(uintptr_t)m); return 0; }
int32_t LWP_MutexDestroy(mutex_t m) { free((void *)(uintptr_t)m); return 0; }
void LWP_YieldThread(void) { OSYieldThread(); }

void VIDEO_WaitVSync(void) { screen_vsync(); }
