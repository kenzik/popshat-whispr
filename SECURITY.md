# Security

## Reporting

Use GitHub's **private vulnerability reporting** on this repository
(Security → Report a vulnerability). Please do not open a public issue for
something exploitable.

This is a small project maintained by one person; expect a response in days,
not hours.

## What whispr has access to

A dictation tool is in a position to see a lot, so:

**Your microphone, while you hold the key.** Capture is a child process
(`pw-record`, `parecord` or `arecord`) spawned when recording starts and killed
when it stops. Audio lives in a heap buffer, is transcribed in-process, and is
freed. It is never written to disk.

**Nothing on the network.** whispr opens exactly one socket: a Unix domain
socket under `$XDG_RUNTIME_DIR`. There is no HTTP client in the daemon, no
telemetry, no crash reporting, no model fetching at runtime. `scripts/install.sh`
downloads models over HTTPS once, at install time, and that is the only network
access in the project.

**Your keyboard: no.** whispr deliberately does not read `/dev/input/event*`.
Doing so would mean joining the `input` group, which grants *every* process you
run the ability to keylog your session — a far larger concession than the
feature is worth. The window manager owns the hotkey and runs a client that
sends one word over the socket.

**`/dev/uinput`, for typing.** The installed udev rule tags the node with
`uaccess`, which grants access to the locally logged-in user only — the same
mechanism used for your webcam and sound devices. It is not world-writable, and
whispr does not run as root. `ydotoold` runs as your user.

Two consequences worth understanding: anything that can write to `/dev/uinput`
can synthesise input for your session, and `whispr-inject` types into whatever
window has focus at that moment. Dictate into a password field and the password
gets typed there, like any keyboard would.

## The trust boundary

`src/ipc.c` parses commands arriving on the Unix socket. The socket is created
mode `0600` and owned by you — at `$XDG_RUNTIME_DIR/whispr.sock`, or
`/tmp/whispr-<uid>.sock` on a system with no systemd user manager — so the
boundary is between your own processes rather than between users. It is still
the one untrusted-input parser in the codebase, and it is where to look first.

The permission matters more than it looks: `stop` makes the daemon transcribe
and type into whatever window you have focused, so a socket another local user
could reach would let them put text into your session.

The other place data crosses a boundary is transcript text on its way to
`whispr-inject`. It is passed on **stdin**, never interpolated into a shell
command, precisely so that a transcript can never be executed.

## Scope

In scope: anything that lets one user's process act as another user, escape the
socket's parser, escalate via the udev rule or the systemd units, or cause
whispr to send audio or transcripts anywhere.

Out of scope: the microphone hearing the room (that is what microphones do),
Whisper transcribing audio it should not have been played, and anything
requiring an attacker who already runs code as you.
