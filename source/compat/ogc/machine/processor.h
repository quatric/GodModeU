#ifndef GMU_COMPAT_PROCESSOR_H
#define GMU_COMPAT_PROCESSOR_H
#include <coreinit/interrupts.h>
/* libogc masks interrupts around short critical sections; the audio callback here runs on its own
 * thread (wiiu/asnd.c), so the section takes that lock instead. */
void gmu_audio_lock(void);
void gmu_audio_unlock(void);
#define _CPU_ISR_Disable(level) do { (level) = 0; gmu_audio_lock(); } while (0)
#define _CPU_ISR_Restore(level) do { (void)(level); gmu_audio_unlock(); } while (0)
#endif
