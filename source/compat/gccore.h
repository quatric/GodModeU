#ifndef GMU_COMPAT_GCCORE_H
#define GMU_COMPAT_GCCORE_H

/* GodModeU: the parts of libogc's <gccore.h> that the code shared with GodModeWii uses, on Cafe OS
 * (wut). The framebuffer, threads, timing and cache calls map onto wiiu/ (see screen.h, ogc_compat.c). */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <coreinit/cache.h>

#include "gmu_types.h"
#include "ogc/lwp_watchdog.h"

#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
#endif
#ifndef ATTRIBUTE_PACKED
#define ATTRIBUTE_PACKED __attribute__((packed))
#endif

#define MEM_K0_TO_K1(x) (x)
#define MEM_K1_TO_K0(x) (x)

/* YUY2 pixel pairs, as libogc names them */
#define COLOR_BLACK 0x00800080
#define COLOR_WHITE 0xEB7FEB7F

typedef uint32_t lwp_t;
typedef uint32_t mutex_t;
#define LWP_THREAD_NULL 0
#define LWP_MUTEX_NULL 0

/* threads (wiiu/ogc_compat.c, on OSThread) */
int32_t LWP_CreateThread(lwp_t *thethread, void *(*entry)(void *), void *arg, void *stackbase,
                         uint32_t stack_size, uint8_t prio);
int32_t LWP_JoinThread(lwp_t thethread, void **value_ptr);
int32_t LWP_MutexInit(mutex_t *mutex, bool use_recursive);
int32_t LWP_MutexLock(mutex_t mutex);
int32_t LWP_MutexUnlock(mutex_t mutex);
int32_t LWP_MutexDestroy(mutex_t mutex);
void LWP_YieldThread(void);

/* the next frame on screen */
void VIDEO_WaitVSync(void);

#endif
