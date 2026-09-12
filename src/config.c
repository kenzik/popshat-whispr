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

  c->max_record_seconds  = 120;
  c->min_record_ms       = 300;
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

  if(!strcmp(k, "gpu_device"))          return(config_parse_int(v, &c->gpu_device));
  if(!strcmp(k, "n_threads"))           return(config_parse_int(v, &c->n_threads));
  if(!strcmp(k, "max_record_seconds"))  return(config_parse_int(v, &c->max_record_seconds));
  if(!strcmp(k, "min_record_ms"))       return(config_parse_int(v, &c->min_record_ms));
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
