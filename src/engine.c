// whispr — MIT
// Transcription engine: a vtable over libwhisper, with libparakeet to follow.

#include "engine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "whisper.h"
#include "parakeet.h"

struct engine
{
  const whispr_config_t  *cfg;
  struct whisper_context  *wctx;
  struct parakeet_context *pctx;
};

engine_t *
engine_create(const whispr_config_t *cfg)
{
  engine_t *e = calloc(1, sizeof *e);

  if(!e)
    return(NULL);

  e->cfg = cfg;

  return(e);
}

const char *
engine_name(const engine_t *e)
{
  if(e->cfg->engine == WHISPR_ENGINE_PARAKEET)
    return("parakeet");

  return("whisper");
}

bool
engine_is_loaded(const engine_t *e)
{
  return(e->wctx != NULL || e->pctx != NULL);
}

bool
engine_load(engine_t *e)
{
  struct whisper_context_params cp;

  if(engine_is_loaded(e))
    return(true);

  if(e->cfg->engine == WHISPR_ENGINE_PARAKEET)
  {
    struct parakeet_context_params pp = { .use_gpu = e->cfg->use_gpu,
                                          .gpu_device = e->cfg->gpu_device };

    if(!e->cfg->parakeet_model[0])
    {
      fprintf(stderr, "whispr: engine = parakeet but parakeet.model is unset\n");

      return(false);
    }

    e->pctx = parakeet_init_from_file_with_params(e->cfg->parakeet_model, pp);

    if(!e->pctx)
    {
      fprintf(stderr, "whispr: failed to load model: %s\n", e->cfg->parakeet_model);

      return(false);
    }

    return(true);
  }

  cp = whisper_context_default_params();
  cp.use_gpu    = e->cfg->use_gpu;
  cp.gpu_device = e->cfg->gpu_device;

  e->wctx = whisper_init_from_file_with_params(e->cfg->whisper_model, cp);

  if(!e->wctx)
  {
    fprintf(stderr, "whispr: failed to load model: %s\n", e->cfg->whisper_model);

    return(false);
  }

  return(true);
}

void
engine_unload(engine_t *e)
{
  if(e->pctx)
  {
    parakeet_free(e->pctx);
    e->pctx = NULL;
  }

  if(e->wctx)
  {
    whisper_free(e->wctx);
    e->wctx = NULL;
  }
}

void
engine_clean_text(char *s)
{
  char *r = s;
  char *w = s;
  int depth = 0;
  size_t len;

  // Drop bracketed and parenthesised non-speech markers wholesale. These are
  // status ([BLANK_AUDIO], [Music], (silence)), and typing them into whatever
  // window has focus would be worse than typing nothing.
  for(; *r; r++)
  {
    if(*r == '[' || *r == '(')
    {
      depth++;

      continue;
    }

    if(*r == ']' || *r == ')')
    {
      if(depth > 0)
        depth--;

      continue;
    }

    if(depth == 0)
      *w++ = *r;
  }

  *w = '\0';

  // Collapse internal runs of whitespace, then trim both ends.
  for(r = s, w = s; *r; r++)
  {
    if(isspace((unsigned char)*r))
    {
      if(w > s && !isspace((unsigned char)w[-1]))
        *w++ = ' ';

      continue;
    }

    *w++ = *r;
  }

  *w = '\0';

  len = strlen(s);

  while(len > 0 && isspace((unsigned char)s[len - 1]))
    s[--len] = '\0';
}

// Appends one segment to a growable string. Returns false only on allocation
// failure, in which case *text is freed and set NULL.
static bool
append_segment(char **text, size_t *cap, size_t *used, const char *s)
{
  size_t slen = strlen(s);

  if(*used + slen + 1 > *cap)
  {
    size_t want = *cap ? *cap : 256;
    char *bigger = NULL;

    while(want < *used + slen + 1)
      want *= 2;

    bigger = realloc(*text, want);

    if(!bigger)
    {
      free(*text);
      *text = NULL;

      return(false);
    }

    *text = bigger;
    *cap = want;
  }

  memcpy(*text + *used, s, slen);
  *used += slen;
  (*text)[*used] = '\0';

  return(true);
}

static bool
engine_transcribe_parakeet(engine_t *e, const float *pcm, size_t n, char **out_text)
{
  struct parakeet_full_params p = parakeet_full_default_params(PARAKEET_SAMPLING_GREEDY);
  char *text = NULL;
  size_t cap = 0;
  size_t used = 0;
  int segs;
  int i;

  p.n_threads = e->cfg->n_threads;

  if(parakeet_full(e->pctx, p, pcm, (int)n) != 0)
    return(false);

  segs = parakeet_full_n_segments(e->pctx);

  for(i = 0; i < segs; i++)
  {
    const char *s = parakeet_full_get_segment_text(e->pctx, i);

    if(!s)
      continue;

    if(!append_segment(&text, &cap, &used, s))
      return(false);
  }

  if(!text)
  {
    text = calloc(1, 1);

    if(!text)
      return(false);
  }

  engine_clean_text(text);
  *out_text = text;

  return(true);
}

bool
engine_transcribe(engine_t *e, const float *pcm, size_t n, char **out_text)
{
  struct whisper_full_params p;
  char *text = NULL;
  size_t cap = 0;
  size_t used = 0;
  int segs;
  int i;

  *out_text = NULL;

  if(e->pctx)
    return(engine_transcribe_parakeet(e, pcm, n, out_text));

  if(!e->wctx)
    return(false);

  p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  p.n_threads        = e->cfg->n_threads;
  p.translate        = e->cfg->whisper_translate;
  p.no_timestamps    = true;
  p.print_progress   = false;
  p.print_realtime   = false;
  p.print_special    = false;
  p.print_timestamps = false;
  p.single_segment   = false;

  // Whisper will confabulate speech from room tone. suppress_nst drops the
  // non-speech tokens ("(dramatic music)"), and no_speech_thold lets the
  // decoder itself bail on a segment it does not believe contains speech.
  p.suppress_nst     = true;
  p.no_speech_thold  = (float)e->cfg->max_no_speech;

  // Voice activity detection is the only one of these that reliably works. An
  // energy gate cannot separate a -32 dB noise floor from quiet speech, and the
  // decoder's own no_speech_prob still passed "and" through from pure noise.
  // Silero segments the audio first, so a recording with no speech in it
  // reaches the decoder as nothing at all.
  if(e->cfg->whisper_vad && e->cfg->whisper_vad_model[0])
  {
    p.vad            = true;
    p.vad_model_path = e->cfg->whisper_vad_model;
    p.vad_params     = whisper_vad_default_params();
  }

  if(e->cfg->whisper_language[0] && strcmp(e->cfg->whisper_language, "auto"))
    p.language = e->cfg->whisper_language;

  if(e->cfg->whisper_initial_prompt[0])
    p.initial_prompt = e->cfg->whisper_initial_prompt;

  if(whisper_full(e->wctx, p, pcm, (int)n) != 0)
    return(false);

  // With VAD on, zero speech segments is the definitive answer: nothing was
  // said, so emit nothing rather than whatever the decoder would invent.
  if(p.vad && whisper_full_n_vad_segments(e->wctx) == 0)
  {
    *out_text = calloc(1, 1);

    return(*out_text != NULL);
  }

  segs = whisper_full_n_segments(e->wctx);

  for(i = 0; i < segs; i++)
  {
    const char *s = whisper_full_get_segment_text(e->wctx, i);

    if(!s)
      continue;

    // Second line of defence: discard any segment the model itself rates as
    // probably-not-speech. Typing a hallucination into the focused window is
    // worse than typing nothing, so this errs toward dropping.
    if(whisper_full_get_segment_no_speech_prob(e->wctx, i) > (float)e->cfg->max_no_speech)
      continue;

    if(!append_segment(&text, &cap, &used, s))
      return(false);
  }

  if(!text)
  {
    text = calloc(1, 1);

    if(!text)
      return(false);
  }

  engine_clean_text(text);
  *out_text = text;

  return(true);
}

void
engine_free(engine_t *e)
{
  if(!e)
    return;

  engine_unload(e);
  free(e);
}
