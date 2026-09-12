#ifndef WHISPR_DAEMON_H
#define WHISPR_DAEMON_H

#include <stdbool.h>
#include "config.h"

typedef struct
{
  bool foreground;
  bool no_inject;
  bool verbose;
} daemon_opts_t;

int daemon_run(const whispr_config_t *, const char *config_path, const daemon_opts_t *);

#endif
