// whispr — MIT
// Configuration file parsing and defaults.

#define CONFIG_INTERNAL
#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
config_defaults(whispr_config_t *c)
{
  const char *home = getenv("HOME");

  memset(c, 0, sizeof *c);

  c->engine     = WHISPR_ENGINE_WHISPER;
  c->use_gpu    = true;
  c->gpu_device = 0;
  c->n_threads  = 8;

  if(home)
    snprintf(c->whisper_model, sizeof c->whisper_model,
             "%s/.local/share/whispr/models/ggml-base.en.bin", home);

  strlcpy(c->whisper_language, "en", sizeof c->whisper_language);
  c->whisper_translate = false;

  // On by default because it earns its place: it takes jargon-heavy dictation
  // from 0.42 WER to 0.16 on base.en. Users override with their own terms.
  strlcpy(c->whisper_initial_prompt,
          "CachyOS, Hyprland, PipeWire, ydotool, whisper.cpp, Vulkan, systemd, Keychron",
          sizeof c->whisper_initial_prompt);

  // VAD is on by default because without it Whisper invents speech from room
  // tone, and this tool types the result into the focused window.
  c->whisper_vad = true;

  if(home)
    snprintf(c->whisper_vad_model, sizeof c->whisper_vad_model,
             "%s/.local/share/whispr/models/ggml-silero-v5.1.2.bin", home);

  c->max_record_seconds  = 120;
  c->min_record_ms       = 300;
  // The start cue plays through the speakers while the mic is already open,
  // so whispr records its own beep and Whisper transcribes it as words --
  // observed emitting "Get a Clospe." into a silent room. Discard the cue
  // window. The cue is also the signal to start talking, so by construction
  // nothing intentional is spoken during it.
  c->skip_start_ms       = 300;
  // Hands-free mode: stop once this much quiet has followed actual speech.
  c->silence_stop_ms     = 2000;
  // Hands-free waits for speech before any silence counts, so an accidental
  // tap would otherwise hold the microphone until max_record_seconds. Give up
  // quietly if nothing is ever said.
  c->dictate_wait_s      = 10;
  // Above this RMS counts as speech. Measured on this machine: room floor
  // -31.6 dB, speech -20.7 to -14.3 dB, so -28 sits in the gap with margin
  // either side. Lower it if dictation cuts off while you are still talking;
  // raise it if a noisy room stops it from ever detecting silence.
  c->voice_dbfs          = -28.0;
  // Whisper invents plausible text from near-silence -- observed emitting
  // "(dramatic music)" and "Fuck you!" from room tone. In a tool that types
  // into the focused window that is far worse than transcribing nothing, so
  // audio below this level is never sent to the model at all.
  c->min_rms_dbfs        = -50.0;
  c->max_no_speech       = 0.6;
  c->idle_unload_seconds = 600;
  c->append_space        = true;

  strlcpy(c->inject_cmd, "whispr-inject", sizeof c->inject_cmd);
  strlcpy(c->notify_cmd, "whispr-notify", sizeof c->notify_cmd);
}

void
config_default_path(char *out, size_t len)
{
  const char *xdg  = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");

  if(xdg && *xdg)
    snprintf(out, len, "%s/whispr/config", xdg);

  else if(home)
    snprintf(out, len, "%s/.config/whispr/config", home);

  else
    strlcpy(out, "/dev/null", len);
}

static void
config_expand_home(char *s, size_t len)
{
  const char *home = getenv("HOME");
  char tmp[WHISPR_PATH_MAX];

  if(s[0] != '~' || s[1] != '/' || !home)
    return;

  snprintf(tmp, sizeof tmp, "%s%s", home, s + 1);
  strlcpy(s, tmp, len);
}

static bool
config_parse_int(const char *v, int *out)
{
  char *end = NULL;
  long n;

  errno = 0;
  n = strtol(v, &end, 10);

  if(errno != 0 || end == v || *end != '\0' || n < INT_MIN || n > INT_MAX)
    return(false);

  *out = (int)n;

  return(true);
}

static bool
config_parse_bool(const char *v, bool *out)
{
  if(!strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "1"))
    *out = true;

  else if(!strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "0"))
    *out = false;

  else
    return(false);

  return(true);
}

static bool
config_apply(whispr_config_t *c, const char *k, const char *v)
{
  if(!strcmp(k, "engine"))
  {
    if(!strcmp(v, "whisper"))
      c->engine = WHISPR_ENGINE_WHISPER;

    else if(!strcmp(v, "parakeet"))
      c->engine = WHISPR_ENGINE_PARAKEET;

    else
      return(false);

    return(true);
  }

  if(!strcmp(k, "use_gpu"))      return(config_parse_bool(v, &c->use_gpu));
  if(!strcmp(k, "append_space")) return(config_parse_bool(v, &c->append_space));
  if(!strcmp(k, "whisper.translate")) return(config_parse_bool(v, &c->whisper_translate));
  if(!strcmp(k, "whisper.vad"))      return(config_parse_bool(v, &c->whisper_vad));

  if(!strcmp(k, "whisper.vad_model"))
  {
    strlcpy(c->whisper_vad_model, v, sizeof c->whisper_vad_model);
    config_expand_home(c->whisper_vad_model, sizeof c->whisper_vad_model);

    return(true);
  }

  if(!strcmp(k, "gpu_device"))          return(config_parse_int(v, &c->gpu_device));
  if(!strcmp(k, "n_threads"))           return(config_parse_int(v, &c->n_threads));
  if(!strcmp(k, "max_record_seconds"))  return(config_parse_int(v, &c->max_record_seconds));
  if(!strcmp(k, "min_record_ms"))       return(config_parse_int(v, &c->min_record_ms));
  if(!strcmp(k, "skip_start_ms"))       return(config_parse_int(v, &c->skip_start_ms));
  if(!strcmp(k, "silence_stop_ms"))     return(config_parse_int(v, &c->silence_stop_ms));
  if(!strcmp(k, "dictate_wait_s"))      return(config_parse_int(v, &c->dictate_wait_s));

  if(!strcmp(k, "min_rms_dbfs") || !strcmp(k, "max_no_speech") || !strcmp(k, "voice_dbfs"))
  {
    char *end = NULL;
    double d;

    errno = 0;
    d = strtod(v, &end);

    if(errno != 0 || end == v || *end != '\0')
      return(false);

    if(!strcmp(k, "min_rms_dbfs"))
      c->min_rms_dbfs = d;

    else if(!strcmp(k, "voice_dbfs"))
      c->voice_dbfs = d;

    else
      c->max_no_speech = d;

    return(true);
  }
  if(!strcmp(k, "idle_unload_seconds")) return(config_parse_int(v, &c->idle_unload_seconds));

  if(!strcmp(k, "whisper.model"))
  {
    strlcpy(c->whisper_model, v, sizeof c->whisper_model);
    config_expand_home(c->whisper_model, sizeof c->whisper_model);

    return(true);
  }

  if(!strcmp(k, "parakeet.model"))
  {
    strlcpy(c->parakeet_model, v, sizeof c->parakeet_model);
    config_expand_home(c->parakeet_model, sizeof c->parakeet_model);

    return(true);
  }

  if(!strcmp(k, "whisper.language"))
  {
    strlcpy(c->whisper_language, v, sizeof c->whisper_language);

    return(true);
  }

  if(!strcmp(k, "whisper.initial_prompt"))
  {
    strlcpy(c->whisper_initial_prompt, v, sizeof c->whisper_initial_prompt);

    return(true);
  }

  if(!strcmp(k, "source"))
  {
    strlcpy(c->source, v, sizeof c->source);

    return(true);
  }

  if(!strcmp(k, "inject_cmd"))
  {
    strlcpy(c->inject_cmd, v, sizeof c->inject_cmd);

    return(true);
  }

  if(!strcmp(k, "notify_cmd"))
  {
    strlcpy(c->notify_cmd, v, sizeof c->notify_cmd);

    return(true);
  }

  // An unknown key is a typo the user wants to hear about, not a silent no-op.
  return(false);
}

bool
config_load(whispr_config_t *c, const char *path)
{
  FILE *f = fopen(path, "re");
  char line[WHISPR_PROMPT_MAX + WHISPR_PATH_MAX];
  bool ok = true;
  int lineno = 0;

  if(!f)
    return(true);

  while(fgets(line, sizeof line, f))
  {
    char *k = line;
    char *v = NULL;
    char *eq = NULL;
    char *end = NULL;

    lineno++;

    end = strchr(line, '#');

    if(end)
      *end = '\0';

    while(*k == ' ' || *k == '\t')
      k++;

    eq = strchr(k, '=');

    if(!eq)
    {
      for(end = k; *end; end++)
        if(*end != ' ' && *end != '\t' && *end != '\n' && *end != '\r')
          break;

      if(*end)
      {
        fprintf(stderr, "whispr: %s:%d: expected key = value\n", path, lineno);
        ok = false;
      }

      continue;
    }

    *eq = '\0';
    v = eq + 1;

    for(end = k + strlen(k); end > k && (end[-1] == ' ' || end[-1] == '\t'); end--)
      end[-1] = '\0';

    while(*v == ' ' || *v == '\t')
      v++;

    for(end = v + strlen(v);
        end > v && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r');
        end--)
      end[-1] = '\0';

    if(!*k)
      continue;

    if(!config_apply(c, k, v))
    {
      fprintf(stderr, "whispr: %s:%d: bad key or value: %s = %s\n", path, lineno, k, v);
      ok = false;
    }
  }

  fclose(f);

  return(ok);
}
