// whispr — MIT
// Event loop, recording state machine, and the transcription worker thread.

#include "daemon.h"
#include "audio.h"
#include "engine.h"
#include "ipc.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef enum
{
  ST_IDLE,
  ST_RECORDING,
  ST_TRANSCRIBING,
} state_t;

typedef struct
{
  whispr_config_t   cfg;
  char              config_path[WHISPR_PATH_MAX];
  daemon_opts_t     opts;

  engine_t         *engine;
  audio_cap_t      *cap;
  pcm_buf_t         pcm;

  int               epfd;
  int               lfd;
  int               sigfd;
  int               maxfd;      // recording watchdog
  int               idlefd;     // model unload timer
  int               donefd;     // worker -> loop wakeup

  state_t           state;
  struct timespec   rec_start;
  bool              running;
  bool              reload_pending;

  // Guards state, and the job handoff to the worker.
  pthread_mutex_t   lock;
  pthread_cond_t    cv;
  pthread_t         worker;
  bool              worker_started;
  bool              job_ready;
  bool              job_quit;
  pcm_buf_t         job_pcm;
  bool              job_want_load;
} daemon_t;

// Best-effort reply. The command has already run, so a peer that hung up
// between sending and reading is not a failure we can or should recover from.
static void
write_reply(int fd, const char *s)
{
  size_t len = strlen(s);
  size_t off = 0;

  while(off < len)
  {
    ssize_t w = write(fd, s + off, len - off);

    if(w > 0)
      off += (size_t)w;

    else if(w < 0 && errno == EINTR)
      continue;

    else
      break;
  }
}

static double
elapsed_since(const struct timespec *t0)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);

  return((double)(now.tv_sec - t0->tv_sec) + (double)(now.tv_nsec - t0->tv_nsec) / 1e9);
}

static void
arm_timer(int fd, int seconds)
{
  struct itimerspec its;

  memset(&its, 0, sizeof its);
  its.it_value.tv_sec = seconds;

  timerfd_settime(fd, 0, &its, NULL);
}

static void
disarm_timer(int fd)
{
  arm_timer(fd, 0);
}

static void
epoll_add(int epfd, int fd, uint32_t events)
{
  struct epoll_event ev;

  memset(&ev, 0, sizeof ev);
  ev.events = events;
  ev.data.fd = fd;

  epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

// Spawn a helper with the text on its stdin. Never blocks the caller: the
// helper is reaped by the SIGCHLD handler path, and feedback must never be
// able to delay injection.
static void
spawn_with_stdin(const char *cmd, const char *const *extra, const char *text)
{
  int fds[2];
  pid_t pid;

  if(!cmd || !*cmd)
    return;

  if(pipe(fds) != 0)
    return;

  pid = fork();

  if(pid < 0)
  {
    close(fds[0]);
    close(fds[1]);

    return;
  }

  if(pid == 0)
  {
    const char *argv[8];
    size_t i = 0;

    dup2(fds[0], STDIN_FILENO);
    close(fds[0]);
    close(fds[1]);

    argv[i++] = cmd;

    for(; extra && *extra && i < 6; extra++)
      argv[i++] = *extra;

    argv[i] = NULL;

    execvp(cmd, (char *const *)argv);

    // Not on PATH. The helpers are installed alongside the daemon, so look
    // there before giving up: systemd's user-manager PATH does not include
    // ~/.local/bin, which made both helpers fail here silently -- no typing, no
    // sound, no notification, and nothing in the journal to say why.
    if(!strchr(cmd, '/'))
    {
      char self[WHISPR_PATH_MAX];
      ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);

      if(n > 0)
      {
        char *slash = NULL;

        self[n] = '\0';
        slash = strrchr(self, '/');

        if(slash)
        {
          char alt[WHISPR_PATH_MAX];

          *slash = '\0';

          // Built with bounded appends so truncation is a checked condition
          // rather than a silently shortened path we would then try to exec.
          if(strlcpy(alt, self, sizeof alt) < sizeof alt &&
             strlcat(alt, "/",  sizeof alt) < sizeof alt &&
             strlcat(alt, cmd,  sizeof alt) < sizeof alt)
          {
            argv[0] = alt;
            execv(alt, (char *const *)argv);
          }
        }
      }
    }

    // Reached only if every exec failed. Say so: a helper that cannot start is
    // indistinguishable from one that ran and did nothing.
    fprintf(stderr, "whispr: cannot exec %s: %s\n", cmd, strerror(errno));
    _exit(127);
  }

  close(fds[0]);

  if(text && *text)
  {
    size_t len = strlen(text);
    size_t off = 0;

    while(off < len)
    {
      ssize_t w = write(fds[1], text + off, len - off);

      if(w > 0)
        off += (size_t)w;

      else if(w < 0 && errno == EINTR)
        continue;

      else
        break;
    }
  }

  close(fds[1]);
}

static void
notify(daemon_t *d, const char *event, const char *text)
{
  const char *extra[2];

  extra[0] = event;
  extra[1] = NULL;

  spawn_with_stdin(d->cfg.notify_cmd, extra, text);
}

static void *
worker_main(void *arg)
{
  daemon_t *d = arg;

  for(;;)
  {
    pcm_buf_t job;
    bool want_load;
    bool quit;
    char *text = NULL;

    pthread_mutex_lock(&d->lock);

    while(!d->job_ready && !d->job_quit)
      pthread_cond_wait(&d->cv, &d->lock);

    quit      = d->job_quit;
    job       = d->job_pcm;
    want_load = d->job_want_load;

    d->job_ready = false;
    d->job_want_load = false;
    memset(&d->job_pcm, 0, sizeof d->job_pcm);

    pthread_mutex_unlock(&d->lock);

    if(quit)
      break;

    // A bare load request: warm the model while the user is still speaking, so
    // the wait after they stop is decode time only.
    if(want_load && !job.samples)
    {
      engine_load(d->engine);

      continue;
    }

    if(!engine_load(d->engine) || !engine_transcribe(d->engine, job.samples, job.n, &text))
      notify(d, "error", "transcription failed");

    else
    {
      if(*text)
      {
        if(d->cfg.append_space)
        {
          size_t len = strlen(text);
          char *padded = realloc(text, len + 2);

          if(padded)
          {
            padded[len] = ' ';
            padded[len + 1] = '\0';
            text = padded;
          }
        }

        if(!d->opts.no_inject)
          spawn_with_stdin(d->cfg.inject_cmd, NULL, text);

        else
          printf("%s\n", text);

        fflush(stdout);
        notify(d, "done", text);
      }

      else
        notify(d, "empty", NULL);

      free(text);
    }

    pcm_free(&job);

    if(d->donefd >= 0)
    {
      uint64_t one = 1;
      ssize_t w = write(d->donefd, &one, sizeof one);

      // An eventfd counter write cannot short-write. If it failed the loop is
      // already tearing down, and it re-checks state on its next wakeup.
      (void)w;
    }
  }

  return(NULL);
}

static void
post_job(daemon_t *d, pcm_buf_t *pcm, bool want_load)
{
  pthread_mutex_lock(&d->lock);

  if(pcm)
  {
    d->job_pcm = *pcm;
    memset(pcm, 0, sizeof *pcm);
  }

  d->job_want_load = want_load;
  d->job_ready = true;

  pthread_cond_signal(&d->cv);
  pthread_mutex_unlock(&d->lock);
}

static void
start_recording(daemon_t *d)
{
  if(d->state != ST_IDLE)
    return;

  pcm_free(&d->pcm);
  d->cap = audio_start(d->cfg.source[0] ? d->cfg.source : NULL);

  if(!d->cap)
  {
    notify(d, "error", "could not start recording");

    return;
  }

  epoll_add(d->epfd, audio_fd(d->cap), EPOLLIN);
  clock_gettime(CLOCK_MONOTONIC, &d->rec_start);
  d->state = ST_RECORDING;

  disarm_timer(d->idlefd);
  arm_timer(d->maxfd, d->cfg.max_record_seconds);

  // Warm the model now, concurrently with speech, rather than after the stop.
  post_job(d, NULL, true);
  notify(d, "start", NULL);
}

static void
finish_recording(daemon_t *d, bool inject)
{
  double secs;

  if(d->state != ST_RECORDING)
    return;

  epoll_ctl(d->epfd, EPOLL_CTL_DEL, audio_fd(d->cap), NULL);
  audio_stop(d->cap, &d->pcm);
  audio_free(d->cap);
  d->cap = NULL;

  disarm_timer(d->maxfd);
  secs = pcm_seconds(&d->pcm);

  if(!inject)
  {
    pcm_free(&d->pcm);
    d->state = ST_IDLE;
    arm_timer(d->idlefd, d->cfg.idle_unload_seconds);
    notify(d, "cancel", NULL);

    return;
  }

  // Too short to be speech: a stray tap, not dictation. Say nothing rather
  // than notify, or the hotkey becomes noisy to brush against.
  if(secs * 1000.0 < (double)d->cfg.min_record_ms)
  {
    pcm_free(&d->pcm);
    d->state = ST_IDLE;
    arm_timer(d->idlefd, d->cfg.idle_unload_seconds);

    return;
  }

  d->state = ST_TRANSCRIBING;
  notify(d, "stop", NULL);
  post_job(d, &d->pcm, false);
}

// Only ever called with the worker idle. The worker reads d->cfg through the
// engine's cfg pointer for the whole of a transcription, and engine_unload()
// frees the context it may be inside -- so swapping either mid-flight is a data
// race and a use-after-free respectively. Deferring is why that cannot happen.
static void
apply_reload_when_idle(daemon_t *d)
{
  whispr_config_t fresh;
  bool engine_changed;

  config_defaults(&fresh);
  config_load(&fresh, d->config_path);

  engine_changed = fresh.engine != d->cfg.engine ||
                   strcmp(fresh.whisper_model, d->cfg.whisper_model) != 0;
  d->cfg = fresh;
  d->reload_pending = false;

  // A different model behind the same context would silently keep serving the
  // old weights, so drop it and let the next start reload.
  if(engine_changed)
    engine_unload(d->engine);
}

static void
reply_status(daemon_t *d, int fd, bool json)
{
  const char *name = "idle";
  char out[256];
  double secs = 0.0;
  state_t st;

  pthread_mutex_lock(&d->lock);
  st = d->state;
  pthread_mutex_unlock(&d->lock);

  if(st == ST_RECORDING)
  {
    name = "recording";
    secs = elapsed_since(&d->rec_start);
  }

  else if(st == ST_TRANSCRIBING)
    name = "transcribing";

  if(json)
    snprintf(out, sizeof out,
             "{\"text\":\"%s\",\"class\":\"%s\",\"tooltip\":\"whispr: %s %.1fs\"}\n",
             st == ST_RECORDING ? "REC" : (st == ST_TRANSCRIBING ? "..." : ""),
             name, name, secs);

  else
    snprintf(out, sizeof out, "%s %.1f\n", name, secs);

  write_reply(fd, out);
}

static void
handle_client(daemon_t *d, int cfd)
{
  whispr_cmd_t cmd;
  char buf[256];
  ssize_t got;

  got = read(cfd, buf, sizeof buf - 1);

  if(got <= 0)
  {
    close(cfd);

    return;
  }

  if(!ipc_parse_cmd(buf, (size_t)got, &cmd))
  {
    write_reply(cfd, "error unknown command\n");
    close(cfd);

    return;
  }

  switch(cmd.kind)
  {
    case WHISPR_CMD_START:
      start_recording(d);
      break;

    case WHISPR_CMD_STOP:
      finish_recording(d, true);
      break;

    case WHISPR_CMD_TOGGLE:
      if(d->state == ST_RECORDING)
        finish_recording(d, true);

      else
        start_recording(d);

      break;

    case WHISPR_CMD_CANCEL:
      finish_recording(d, false);
      break;

    case WHISPR_CMD_STATUS:
      reply_status(d, cfd, cmd.json);
      close(cfd);

      return;

    case WHISPR_CMD_RELOAD:
      if(d->state == ST_IDLE)
        apply_reload_when_idle(d);

      // Busy: queue it rather than mutate config out from under the worker.
      else
        d->reload_pending = true;

      break;
  }

  write_reply(cfd, "ok\n");
  close(cfd);
}

int
daemon_run(const whispr_config_t *cfg, const char *config_path, const daemon_opts_t *opts)
{
  daemon_t d;
  sigset_t mask;
  char sock[WHISPR_PATH_MAX];
  int rc = 1;

  memset(&d, 0, sizeof d);
  d.cfg = *cfg;
  d.opts = *opts;
  d.state = ST_IDLE;
  d.running = true;
  strlcpy(d.config_path, config_path, sizeof d.config_path);

  pthread_mutex_init(&d.lock, NULL);
  pthread_cond_init(&d.cv, NULL);

  d.engine = engine_create(&d.cfg);

  if(!d.engine)
    return(1);

  ipc_socket_path(sock, sizeof sock);
  d.lfd = ipc_listen(sock);

  if(d.lfd < 0)
  {
    fprintf(stderr, "whispr: cannot listen on %s: %s\n", sock, strerror(errno));

    if(errno == EADDRINUSE)
      fprintf(stderr, "whispr: another daemon is already running\n");

    engine_free(d.engine);

    return(1);
  }

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  d.sigfd  = signalfd(-1, &mask, SFD_CLOEXEC);
  d.maxfd  = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  d.idlefd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  d.donefd = eventfd(0, EFD_CLOEXEC);
  d.epfd   = epoll_create1(EPOLL_CLOEXEC);

  if(d.sigfd < 0 || d.maxfd < 0 || d.idlefd < 0 || d.donefd < 0 || d.epfd < 0)
  {
    fprintf(stderr, "whispr: cannot create event fds: %s\n", strerror(errno));

    goto out;
  }

  epoll_add(d.epfd, d.lfd,    EPOLLIN);
  epoll_add(d.epfd, d.sigfd,  EPOLLIN);
  epoll_add(d.epfd, d.maxfd,  EPOLLIN);
  epoll_add(d.epfd, d.idlefd, EPOLLIN);
  epoll_add(d.epfd, d.donefd, EPOLLIN);

  if(pthread_create(&d.worker, NULL, worker_main, &d) != 0)
  {
    fprintf(stderr, "whispr: cannot start worker thread\n");

    goto out;
  }

  d.worker_started = true;

  if(opts->verbose)
    fprintf(stderr, "whispr: listening on %s, engine=%s\n", sock, engine_name(d.engine));

  while(d.running)
  {
    struct epoll_event evs[8];
    int n = epoll_wait(d.epfd, evs, 8, -1);
    int i;

    if(n < 0)
    {
      if(errno == EINTR)
        continue;

      break;
    }

    for(i = 0; i < n; i++)
    {
      int fd = evs[i].data.fd;
      uint64_t ticks;

      if(fd == d.lfd)
      {
        int cfd = accept4(d.lfd, NULL, NULL, SOCK_CLOEXEC);

        if(cfd >= 0)
          handle_client(&d, cfd);
      }

      else if(fd == d.sigfd)
      {
        struct signalfd_siginfo si;

        if(read(d.sigfd, &si, sizeof si) != sizeof si)
          continue;

        if(si.ssi_signo == SIGCHLD)
        {
          while(waitpid(-1, NULL, WNOHANG) > 0)
            ;
        }

        else if(si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM)
          d.running = false;
      }

      else if(fd == d.maxfd)
      {
        if(read(d.maxfd, &ticks, sizeof ticks) != sizeof ticks)
          continue;

        // The release event never arrived. Stop rather than record forever --
        // a stuck push-to-talk is the worst failure this tool has.
        if(d.state == ST_RECORDING)
        {
          notify(&d, "timeout", NULL);
          finish_recording(&d, true);
        }
      }

      else if(fd == d.idlefd)
      {
        if(read(d.idlefd, &ticks, sizeof ticks) != sizeof ticks)
          continue;

        if(d.state == ST_IDLE)
          engine_unload(d.engine);
      }

      else if(fd == d.donefd)
      {
        if(read(d.donefd, &ticks, sizeof ticks) != sizeof ticks)
          continue;

        pthread_mutex_lock(&d.lock);

        if(d.state == ST_TRANSCRIBING)
          d.state = ST_IDLE;

        pthread_mutex_unlock(&d.lock);

        if(d.reload_pending && d.state == ST_IDLE)
          apply_reload_when_idle(&d);

        arm_timer(d.idlefd, d.cfg.idle_unload_seconds);
      }

      else if(d.cap && fd == audio_fd(d.cap))
      {
        if(!audio_drain(d.cap, &d.pcm))
          finish_recording(&d, true);
      }
    }
  }

  rc = 0;

out:

  if(d.state == ST_RECORDING)
    finish_recording(&d, false);

  if(d.worker_started)
  {
    pthread_mutex_lock(&d.lock);
    d.job_quit = true;
    pthread_cond_signal(&d.cv);
    pthread_mutex_unlock(&d.lock);
    pthread_join(d.worker, NULL);
  }

  if(d.cap)
    audio_free(d.cap);

  pcm_free(&d.pcm);
  pcm_free(&d.job_pcm);
  engine_free(d.engine);

  if(d.epfd   >= 0) close(d.epfd);
  if(d.sigfd  >= 0) close(d.sigfd);
  if(d.maxfd  >= 0) close(d.maxfd);
  if(d.idlefd >= 0) close(d.idlefd);
  if(d.donefd >= 0) close(d.donefd);
  if(d.lfd    >= 0) close(d.lfd);

  unlink(sock);
  pthread_mutex_destroy(&d.lock);
  pthread_cond_destroy(&d.cv);

  return(rc);
}
