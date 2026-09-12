// whispr — MIT
// Entry point: dispatches between the daemon and the control client.

#include "config.h"
#include "daemon.h"
#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
  fputs("usage: whispr <command> [options]\n"
        "\n"
        "  daemon              run the service\n"
        "  start               begin recording\n"
        "  stop                end recording, transcribe, inject\n"
        "  toggle              start if idle, else stop\n"
        "  cancel              discard the recording\n"
        "  status [--json]     report state\n"
        "  reload              re-read the config file\n"
        "\n"
        "daemon options:\n"
        "  --foreground        do not detach (default; systemd runs it this way)\n"
        "  --no-inject         print transcripts to stdout instead of typing them\n"
        "  -v, --verbose       log to stderr\n"
        "  -c, --config PATH   config file (default $XDG_CONFIG_HOME/whispr/config)\n",
        stderr);
}

static int
run_client(int argc, char **argv)
{
  char sock[WHISPR_PATH_MAX];
  char line[256];
  char reply[512];
  ssize_t got;
  int fd;
  int i;
  size_t used;

  ipc_socket_path(sock, sizeof sock);
  fd = ipc_connect(sock);

  if(fd < 0)
  {
    fprintf(stderr, "whispr: daemon not running (%s)\n", sock);

    return(1);
  }

  used = strlcpy(line, argv[1], sizeof line);

  for(i = 2; i < argc && used < sizeof line; i++)
  {
    strlcat(line, " ", sizeof line);
    used = strlcat(line, argv[i], sizeof line);
  }

  strlcat(line, "\n", sizeof line);

  if(write(fd, line, strlen(line)) < 0)
  {
    close(fd);

    return(1);
  }

  got = read(fd, reply, sizeof reply - 1);
  close(fd);

  if(got <= 0)
    return(1);

  reply[got] = '\0';

  // status is the only command whose output is meant to be consumed.
  if(!strcmp(argv[1], "status"))
    fputs(reply, stdout);

  return(strncmp(reply, "error", 5) == 0 ? 1 : 0);
}

int
main(int argc, char **argv)
{
  whispr_config_t cfg;
  daemon_opts_t opts;
  char path[WHISPR_PATH_MAX];
  int i;

  if(argc < 2)
  {
    usage();

    return(2);
  }

  if(!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
  {
    usage();

    return(0);
  }

  if(strcmp(argv[1], "daemon"))
    return(run_client(argc, argv));

  memset(&opts, 0, sizeof opts);
  opts.foreground = true;
  config_default_path(path, sizeof path);

  for(i = 2; i < argc; i++)
  {
    if(!strcmp(argv[i], "--foreground"))
      opts.foreground = true;

    else if(!strcmp(argv[i], "--no-inject"))
      opts.no_inject = true;

    else if(!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
      opts.verbose = true;

    else if((!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")) && i + 1 < argc)
      strlcpy(path, argv[++i], sizeof path);

    else
    {
      fprintf(stderr, "whispr: unknown option: %s\n", argv[i]);

      return(2);
    }
  }

  config_defaults(&cfg);

  if(!config_load(&cfg, path) && opts.verbose)
    fprintf(stderr, "whispr: config had errors; defaults used for bad keys\n");

  return(daemon_run(&cfg, path, &opts));
}
