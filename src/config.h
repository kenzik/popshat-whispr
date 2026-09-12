#ifndef WHISPR_CONFIG_H
#define WHISPR_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define WHISPR_PATH_MAX   4096
#define WHISPR_PROMPT_MAX 1024
#define WHISPR_SAMPLE_RATE 16000

typedef enum
{
  WHISPR_ENGINE_WHISPER,
  WHISPR_ENGINE_PARAKEET,
} whispr_engine_kind_t;

typedef struct
{
  whispr_engine_kind_t engine;
  bool                 use_gpu;
  int                  gpu_device;
  int                  n_threads;

  char whisper_model[WHISPR_PATH_MAX];
  char whisper_language[16];
  bool whisper_translate;
  char whisper_initial_prompt[WHISPR_PROMPT_MAX];
  bool whisper_vad;
  char whisper_vad_model[WHISPR_PATH_MAX];

  char parakeet_model[WHISPR_PATH_MAX];

  char source[256];
  int  max_record_seconds;
  int  min_record_ms;
  int  skip_start_ms;
  double min_rms_dbfs;
  double max_no_speech;
  int  idle_unload_seconds;
  bool append_space;
  char inject_cmd[WHISPR_PATH_MAX];
  char notify_cmd[WHISPR_PATH_MAX];
} whispr_config_t;

void config_defaults(whispr_config_t *);

// Missing file is not an error -- defaults stand. Returns false only on a
// malformed line, and leaves the config populated with everything parsed
// before it.
bool config_load(whispr_config_t *, const char *path);
void config_default_path(char *, size_t);

#ifdef CONFIG_INTERNAL
static bool config_apply(whispr_config_t *, const char *, const char *);
static void config_expand_home(char *, size_t);
static bool config_parse_int(const char *, int *);
static bool config_parse_bool(const char *, bool *);
#endif

#endif
