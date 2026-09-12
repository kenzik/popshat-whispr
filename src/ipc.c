// whispr — MIT
// Unix-socket control channel and the command parsing boundary.

#define IPC_INTERNAL
#include "ipc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct
{
  const char       *verb;
  whispr_cmd_kind_t kind;
} ipc_verb_t;

static const ipc_verb_t ipc_verbs[] =
{
  { "start",  WHISPR_CMD_START  },
  { "stop",   WHISPR_CMD_STOP   },
  { "toggle", WHISPR_CMD_TOGGLE },
  { "dictate",WHISPR_CMD_DICTATE},
  { "cancel", WHISPR_CMD_CANCEL },
  { "status", WHISPR_CMD_STATUS },
  { "reload", WHISPR_CMD_RELOAD },
};

bool
ipc_parse_cmd(const char *buf, size_t len, whispr_cmd_t *out)
{
  char word[32];
  const char *sp = NULL;
  size_t vlen;
  size_t i;

  // len is what we read, never a count the peer supplied. Trim the framing
  // newline and any trailing whitespace before matching.
  while(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                    buf[len - 1] == ' '  || buf[len - 1] == '\t'))
    len--;

  while(len > 0 && (*buf == ' ' || *buf == '\t'))
  {
    buf++;
    len--;
  }

  if(len == 0)
    return(false);

  sp = memchr(buf, ' ', len);
  vlen = sp ? (size_t)(sp - buf) : len;

  if(vlen == 0 || vlen >= sizeof word)
    return(false);

  memcpy(word, buf, vlen);
  word[vlen] = '\0';

  for(i = 0; i < sizeof ipc_verbs / sizeof *ipc_verbs; i++)
  {
    if(strcmp(word, ipc_verbs[i].verb))
      continue;

    // The one place a whispr_cmd_t is constructed.
    *out = (whispr_cmd_t){
      .kind = ipc_verbs[i].kind,
      .json = sp && memmem(sp, len - vlen, "--json", 6) != NULL,
    };

    return(true);
  }

  return(false);
}

void
ipc_socket_path(char *out, size_t len)
{
  const char *run = getenv("XDG_RUNTIME_DIR");

  if(run && *run)
    snprintf(out, len, "%s/whispr.sock", run);

  else
    snprintf(out, len, "/tmp/whispr-%u.sock", (unsigned)getuid());
}

static bool
ipc_socket_alive(const char *path)
{
  int fd = ipc_connect(path);

  if(fd < 0)
    return(false);

  close(fd);

  return(true);
}

int
ipc_listen(const char *path)
{
  struct sockaddr_un addr;
  mode_t prev;
  int rc;
  int fd;

  if(strlen(path) >= sizeof addr.sun_path)
  {
    errno = ENAMETOOLONG;

    return(-1);
  }

  // Only clear a socket nothing answers on. Unlinking unconditionally would
  // let a second daemon silently displace a running one.
  if(ipc_socket_alive(path))
  {
    errno = EADDRINUSE;

    return(-1);
  }

  unlink(path);

  fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

  if(fd < 0)
    return(-1);

  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  strlcpy(addr.sun_path, path, sizeof addr.sun_path);

  // Create the socket 0600. A bare bind() leaves it 0777-minus-umask, and when
  // XDG_RUNTIME_DIR is unset -- a system without a systemd user manager -- the
  // path falls back to /tmp, where any local user could then connect and drive
  // the daemon. "stop" is the interesting one: it transcribes and types the
  // result into whatever window the real user has focused.
  //
  // umask rather than a chmod after bind, which would leave the socket briefly
  // open to everyone.
  prev = umask(0177);
  rc   = bind(fd, (struct sockaddr *)&addr, sizeof addr);
  umask(prev);

  if(rc != 0 || listen(fd, 16) != 0)
  {
    int e = errno;

    close(fd);
    errno = e;

    return(-1);
  }

  return(fd);
}

int
ipc_connect(const char *path)
{
  struct sockaddr_un addr;
  int fd;

  if(strlen(path) >= sizeof addr.sun_path)
  {
    errno = ENAMETOOLONG;

    return(-1);
  }

  fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

  if(fd < 0)
    return(-1);

  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  strlcpy(addr.sun_path, path, sizeof addr.sun_path);

  if(connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0)
  {
    int e = errno;

    close(fd);
    errno = e;

    return(-1);
  }

  return(fd);
}
