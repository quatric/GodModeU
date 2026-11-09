#ifndef GMU_COMPAT_ASNDLIB_H
#define GMU_COMPAT_ASNDLIB_H

/* libogc's ASND (16 software voices, each with a buffer playing and one queued behind it), as the
 * audio code shared with GodModeWii uses it. On the Wii U the voices are mixed by wiiu/asnd.c into one
 * stereo stream that two AX voices play on the TV and the GamePad. */

#include <stdint.h>
#include <stdbool.h>

#define MAX_SND_VOICES 16

#define SND_OK               0
#define SND_INVALID         -1
#define SND_ISNOTASONGVOICE -2
#define SND_BUSY             1

#define SND_UNUSED   0
#define SND_WORKING  1
#define SND_WAITING  2

#define VOICE_MONO_8BIT       0
#define VOICE_MONO_16BIT      1
#define VOICE_MONO_16BIT_BE   1
#define VOICE_STEREO_8BIT     2
#define VOICE_STEREO_16BIT    3
#define VOICE_STEREO_16BIT_BE 3
#define VOICE_MONO_8BIT_U     4
#define VOICE_MONO_16BIT_LE   5
#define VOICE_STEREO_8BIT_U   6
#define VOICE_STEREO_16BIT_LE 7

#define MIN_VOLUME 0
#define MID_VOLUME 127
#define MAX_VOLUME 255

#define INIT_RATE_48000 48000
#define VOICE_FREQ48KHZ 48000

typedef void (*ASNDVoiceCallback)(int32_t voice);

void ASND_Init(void);
void ASND_End(void);
void ASND_Pause(int32_t paused);
int32_t ASND_Is_Paused(void);
int32_t ASND_GetFirstUnusedVoice(void);
int32_t ASND_SetVoice(int32_t voice, int32_t format, int32_t pitch, int32_t delay, void *snd, int32_t size_snd,
                      int32_t volume_l, int32_t volume_r, ASNDVoiceCallback callback);
int32_t ASND_SetInfiniteVoice(int32_t voice, int32_t format, int32_t pitch, int32_t delay, void *snd,
                              int32_t size_snd, int32_t volume_l, int32_t volume_r);
int32_t ASND_AddVoice(int32_t voice, void *snd, int32_t size_snd);
int32_t ASND_StopVoice(int32_t voice);
int32_t ASND_PauseVoice(int32_t voice, int32_t pause);
int32_t ASND_StatusVoice(int32_t voice);
int32_t ASND_ChangeVolumeVoice(int32_t voice, int32_t volume_l, int32_t volume_r);
int32_t ASND_ChangePitchVoice(int32_t voice, int32_t pitch);
int32_t ASND_TestPointer(int32_t voice, void *pointer);

#endif
