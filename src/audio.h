#ifndef WHISPR_AUDIO_H
#define WHISPR_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

// 32-bit float PCM at WHISPR_SAMPLE_RATE, mono -- the format both engines
// consume, so nothing downstream resamples.
typedef struct
{
  float  *samples;
  size_t  n;
  size_t  cap;
} pcm_buf_t;

typedef struct audio_cap audio_cap_t;

// Spawns the capture child. NULL source means the system default device.
audio_cap_t *audio_start(const char *source);

// Read end of the capture pipe, for the event loop. -1 once stopped.
int audio_fd(const audio_cap_t *);

// Moves whatever bytes are ready into buf. Call on readability; a short read
// is normal, not end of stream.
bool audio_drain(audio_cap_t *, pcm_buf_t *);

// Signals the child, drains the remainder, and reaps it. Safe to call twice.
bool audio_stop(audio_cap_t *, pcm_buf_t *);

void audio_free(audio_cap_t *);
void pcm_free(pcm_buf_t *);
double pcm_seconds(const pcm_buf_t *);

// RMS level in dBFS. Speech runs about -35..-12; room tone sits below -50.
// Returns -INFINITY for an empty buffer.
double pcm_rms_dbfs(const pcm_buf_t *);

#ifdef AUDIO_INTERNAL
struct audio_cap
{
  pid_t pid;
  int   fd;
  bool  reaped;
};
static bool pcm_reserve(pcm_buf_t *, size_t);
#endif

#endif
