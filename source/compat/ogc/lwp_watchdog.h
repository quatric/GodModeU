#ifndef GMU_COMPAT_LWP_WATCHDOG_H
#define GMU_COMPAT_LWP_WATCHDOG_H
#include <stdint.h>
#include <coreinit/time.h>
/* libogc's time base calls on the Cafe OS system timer (OSTimerClockSpeed ticks a second) */
static inline uint64_t gettime(void) { return (uint64_t)OSGetSystemTime(); }
static inline uint32_t gettick(void) { return (uint32_t)OSGetSystemTick(); }
#define ticks_to_secs(t)       OSTicksToSeconds(t)
#define ticks_to_millisecs(t)  OSTicksToMilliseconds(t)
#define ticks_to_microsecs(t)  OSTicksToMicroseconds(t)
#define ticks_to_nanosecs(t)   OSTicksToNanoseconds(t)
#define secs_to_ticks(s)       OSSecondsToTicks(s)
#define millisecs_to_ticks(m)  OSMillisecondsToTicks(m)
#define microsecs_to_ticks(u)  OSMicrosecondsToTicks(u)
#define diff_ticks(a, b)       ((uint64_t)(b) - (uint64_t)(a))
#define diff_msec(a, b)        OSTicksToMilliseconds(diff_ticks(a, b))
#define diff_usec(a, b)        OSTicksToMicroseconds(diff_ticks(a, b))
#endif
