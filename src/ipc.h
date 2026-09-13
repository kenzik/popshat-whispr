#ifndef WHISPR_IPC_H
#define WHISPR_IPC_H

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
  WHISPR_CMD_START,
  WHISPR_CMD_STOP,
  WHISPR_CMD_TOGGLE,
  WHISPR_CMD_DICTATE,
  WHISPR_CMD_CANCEL,
  WHISPR_CMD_STATUS,
  WHISPR_CMD_RELOAD,
} whispr_cmd_kind_t;

// A command that has crossed the socket boundary. Brace-initialize this only
// inside ipc_parse_cmd(): building one anywhere else launders the validation
// it exists to carry.
typedef struct
{
  whispr_cmd_kind_t kind;
  bool              json;
} whispr_cmd_t;

// The boundary. len is the byte count actually read, never a length the peer
// claimed. Rejects anything not exactly matching a known verb.
bool ipc_parse_cmd(const char *buf, size_t len, whispr_cmd_t *out);

void ipc_socket_path(char *, size_t);

// Listening socket, or -1. Unlinks a stale socket only when no peer answers,
// so a second daemon cannot steal a live one.
int  ipc_listen(const char *path);
int  ipc_connect(const char *path);

#ifdef IPC_INTERNAL
static bool ipc_socket_alive(const char *);
#endif

#endif
