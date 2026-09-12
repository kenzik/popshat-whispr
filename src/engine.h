#ifndef WHISPR_ENGINE_H
#define WHISPR_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include "config.h"

typedef struct engine engine_t;

engine_t *engine_create(const whispr_config_t *);

// Loading is the expensive step, so the daemon starts it when recording
// starts rather than when it ends. Idempotent.
bool engine_load(engine_t *);
void engine_unload(engine_t *);
bool engine_is_loaded(const engine_t *);

// On success *out_text is a heap string owned by the caller. Text is already
// stripped of non-speech markers and trimmed; it may legitimately be empty.
bool engine_transcribe(engine_t *, const float *pcm, size_t n, char **out_text);

void engine_free(engine_t *);
const char *engine_name(const engine_t *);

// whisper.cpp reports non-speech as bracketed markers -- [BLANK_AUDIO],
// [Music], (silence). They are status rather than speech and must never reach
// the focused window. Exposed for testing.
void engine_clean_text(char *);

#endif
