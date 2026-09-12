// whispr — MIT
// Transcription engine: a vtable over libwhisper, with libparakeet to follow.

#include "engine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "whisper.h"

struct engine
{
  const whispr_config_t *cfg;
  struct whisper_context *wctx;
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
  return(e->wctx != NULL);
}

bool
engine_load(engine_t *e)
{
  struct whisper_context_params cp;

  if(e->wctx)
    return(true);

  if(e->cfg->engine == WHISPR_ENGINE_PARAKEET)
  {
    fprintf(stderr, "whispr: parakeet engine not built in yet; set engine = whisper\n");

    return(false);
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
  if(!e->wctx)
    return;

  whisper_free(e->wctx);
  e->wctx = NULL;
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

  if(e->cfg->whisper_language[0] && strcmp(e->cfg->whisper_language, "auto"))
    p.language = e->cfg->whisper_language;

  if(e->cfg->whisper_initial_prompt[0])
    p.initial_prompt = e->cfg->whisper_initial_prompt;

  if(whisper_full(e->wctx, p, pcm, (int)n) != 0)
    return(false);

  segs = whisper_full_n_segments(e->wctx);

  for(i = 0; i < segs; i++)
  {
    const char *s = whisper_full_get_segment_text(e->wctx, i);
    size_t slen;

    if(!s)
      continue;

    slen = strlen(s);

    if(used + slen + 1 > cap)
    {
      size_t want = cap ? cap : 256;
      char *bigger = NULL;

      while(want < used + slen + 1)
        want *= 2;

      bigger = realloc(text, want);

      if(!bigger)
      {
        free(text);

        return(false);
      }

      text = bigger;
      cap = want;
    }

    memcpy(text + used, s, slen);
    used += slen;
    text[used] = '\0';
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
