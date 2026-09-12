// whispr — MIT
// Microphone capture: a child process writing raw f32 PCM into a growable buffer.

#define AUDIO_INTERNAL
#include "audio.h"
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define AUDIO_CHUNK 16384

// Ordered by preference. All four write raw little-endian f32 mono at
// WHISPR_SAMPLE_RATE to stdout, so the parent never resamples or converts.
typedef struct
{
  const char *bin;
  const char *argv[16];
  int         source_slot;  // argv index to fill with the device, or -1
} audio_backend_t;

static const audio_backend_t audio_backends[] =
{
  { "pw-record",
    { "pw-record", "--raw", "--rate=16000", "--channels=1", "--format=f32",
      "--target=%s", "-", NULL }, 5 },
  { "parecord",
    { "parecord", "--raw", "--rate=16000", "--channels=1", "--format=float32le",
      "--device=%s", NULL }, 5 },
  { "arecord",
    { "arecord", "-q", "-f", "FLOAT_LE", "-r", "16000", "-c", "1", "-t", "raw",
      "-D%s", NULL }, 10 },
  { "ffmpeg",
    { "ffmpeg", "-loglevel", "quiet", "-f", "pulse", "-i", "%s",
      "-ar", "16000", "-ac", "1", "-f", "f32le", "-" }, 6 },
};

static bool
pcm_reserve(pcm_buf_t *b, size_t want)
{
  size_t cap = b->cap ? b->cap : 1 << 16;
  float *p = NULL;

  if(b->n + want <= b->cap)
    return(true);

  while(cap < b->n + want)
    cap *= 2;

  p = realloc(b->samples, cap * sizeof *p);

  if(!p)
    return(false);

  b->samples = p;
  b->cap = cap;

  return(true);
}

void
pcm_free(pcm_buf_t *b)
{
  free(b->samples);
  b->samples = NULL;
  b->n = 0;
  b->cap = 0;
}

double
pcm_seconds(const pcm_buf_t *b)
{
  return((double)b->n / (double)WHISPR_SAMPLE_RATE);
}

audio_cap_t *
audio_start(const char *source)
{
  audio_cap_t *c = NULL;
  int fds[2];
  size_t i;

  if(pipe2(fds, O_CLOEXEC) != 0)
    return(NULL);

  c = calloc(1, sizeof *c);

  if(!c)
  {
    close(fds[0]);
    close(fds[1]);

    return(NULL);
  }

  c->pid = fork();

  if(c->pid < 0)
  {
    close(fds[0]);
    close(fds[1]);
    free(c);

    return(NULL);
  }

  if(c->pid == 0)
  {
    char slot[320];

    if(dup2(fds[1], STDOUT_FILENO) < 0)
      _exit(127);

    // A new group so a stray signal to the daemon's group cannot orphan the
    // recorder -- the failure that produced 200-second takes during setup.
    setpgid(0, 0);

    for(i = 0; i < sizeof audio_backends / sizeof *audio_backends; i++)
    {
      const audio_backend_t *b = &audio_backends[i];
      const char *argv[16];
      size_t j;

      for(j = 0; j < sizeof b->argv / sizeof *b->argv && b->argv[j]; j++)
        argv[j] = b->argv[j];

      argv[j] = NULL;

      if(b->source_slot >= 0 && (size_t)b->source_slot < j)
      {
        if(source && *source)
        {
          snprintf(slot, sizeof slot, b->argv[b->source_slot], source);
          argv[b->source_slot] = slot;
        }

        else if(!strcmp(b->bin, "ffmpeg"))
          argv[b->source_slot] = "default";

        else
        {
          // No device named: drop the selector rather than pass an empty one.
          for(j = (size_t)b->source_slot; argv[j]; j++)
            argv[j] = argv[j + 1];
        }
      }

      execvp(b->bin, (char *const *)argv);
    }

    _exit(127);
  }

  close(fds[1]);
  c->fd = fds[0];

  // Read end must be non-blocking: the event loop drains on readability and
  // has to return promptly. Only the read end -- a non-blocking write end would
  // make the recorder drop samples whenever the pipe filled.
  if(fcntl(c->fd, F_SETFL, O_NONBLOCK) != 0)
  {
    audio_free(c);

    return(NULL);
  }

  return(c);
}

int
audio_fd(const audio_cap_t *c)
{
  return(c ? c->fd : -1);
}

bool
audio_drain(audio_cap_t *c, pcm_buf_t *b)
{
  ssize_t got;

  if(!c || c->fd < 0)
    return(false);

  for(;;)
  {
    if(!pcm_reserve(b, AUDIO_CHUNK / sizeof(float)))
      return(false);

    got = read(c->fd, b->samples + b->n, AUDIO_CHUNK);

    if(got > 0)
    {
      b->n += (size_t)got / sizeof(float);

      continue;
    }

    if(got == 0)
      return(true);

    if(errno == EINTR)
      continue;

    if(errno == EAGAIN || errno == EWOULDBLOCK)
      return(true);

    return(false);
  }
}

bool
audio_stop(audio_cap_t *c, pcm_buf_t *b)
{
  int status;

  if(!c || c->reaped)
    return(true);

  // SIGINT first: every backend treats it as "finish cleanly".
  if(c->pid > 0)
    kill(c->pid, SIGINT);

  // The final drain is the opposite case from the loop's: the child has been
  // signalled and is flushing, so we must wait for its tail rather than give up
  // on EAGAIN. Blocking again makes EOF -- the child closing the pipe -- the
  // terminator.
  if(c->fd >= 0)
    fcntl(c->fd, F_SETFL, 0);

  audio_drain(c, b);

  if(c->fd >= 0)
  {
    close(c->fd);
    c->fd = -1;
  }

  if(c->pid > 0)
  {
    // The child has already been signalled and its pipe closed, so this waits
    // on a process that is on its way out rather than blocking indefinitely.
    if(waitpid(c->pid, &status, 0) < 0 && errno == EINTR)
      waitpid(c->pid, &status, 0);
  }

  c->reaped = true;

  return(true);
}

void
audio_free(audio_cap_t *c)
{
  if(!c)
    return;

  if(!c->reaped && c->pid > 0)
  {
    kill(c->pid, SIGKILL);
    waitpid(c->pid, NULL, 0);
  }

  if(c->fd >= 0)
    close(c->fd);

  free(c);
}
