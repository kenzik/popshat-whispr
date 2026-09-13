# Contributing

Bug reports, compositor snippets and distro fixes are all welcome. So are
"this doesn't work on my machine" issues — most of what whispr does is talk to
other people's systems, and only one system has actually been tested on.

## Before you file a bug

```sh
whispr-doctor
```

Paste the output. It answers most of the questions a maintainer would otherwise
have to ask — distro, session type, capture program, whether `ydotoold` is
running, whether `/dev/uinput` is writable, which models are present, whether
the daemon is alive. It prints your home directory as `~`, and deliberately
does not print `whisper.initial_prompt`, which is the one config field likely
to contain something personal.

## Scope

whispr is **a daemon and a CLI**. That is the whole surface.

- No GUI, no tray icon, no Electron, no web UI, no settings app. Configuration
  is a flat text file; status is `whispr status --json`, which a bar module can
  poll in a few lines of shell.
- No network. Ever. No telemetry, no model downloads at runtime, no cloud
  fallback. `scripts/install.sh` fetches models over HTTPS once, and that is the
  only socket whispr's world contains besides its own Unix one.
- No key grabbing. The window manager owns the hotkey — see
  [wm/README.md](wm/README.md) for why that is a security decision rather than
  a limitation.

Features that fit: engines, accuracy, latency, robustness, more compositors,
more distros, better diagnostics.

## Building

```sh
git clone --recursive https://github.com/kenzik/popshat-whispr.git
cd popshat-whispr
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
cmake --build build -j"$(nproc)" --target whispr
```

`-DGGML_VULKAN=ON` adds GPU support if the Vulkan and SPIR-V headers, glslang
and shaderc are installed. It is an optimisation: CPU is fully functional, so
nothing may ever *require* it.

Run the daemon you just built without disturbing the installed one:

```sh
./build/whispr daemon --no-inject -v
```

**`--no-inject` is not optional for anything automated.** Without it the daemon
types transcripts into whatever window has focus, which during a test run is
your editor.

## Tests

```sh
./tests/run-tests.sh          # WER against recorded fixtures; fast, automated
./scripts/acceptance.sh       # operator-driven, drives the real hotkeys
```

Two rules:

**Thresholds are a ratchet — tighten only.** `run-tests.sh` asserts word error
rate under a limit, not string equality, because ASR never matches
byte-for-byte. A threshold loosened to make a failing run go green catches
nothing ever again. If a change makes a fixture worse, the change is the
problem.

**The silence fixture may never produce output.** Whisper will confabulate
`[BLANK_AUDIO]`, `[Music]` or subtitle credits from room tone, and whispr types
what it gets into the focused window. Stripping that is tested, not assumed.

Fixtures are recordings of one voice in one room. `./tests/record-fixtures.sh`
records your own. A second speaker or a noisy room would genuinely improve this
suite.

## Code style

Match the file you are editing. The C is deliberately plain:

- Allman braces, two-space indent, no tabs.
- `return(value)` with parentheses; that is the house style, not a mistake.
- One blank line before `return`, and around logical blocks.
- C11 plus the GNU extensions already in use (`_GNU_SOURCE` is set project-wide
  for `pipe2`, `accept4`, `strlcpy`).
- The daemon is single-threaded apart from one worker. Keep it that way:
  transcription and injection happen on the worker so `whispr status` never
  stalls behind a `ydotool type`.

Shell is POSIX `sh` for the runtime helpers (`whispr-inject`, `whispr-notify`,
`whispr-doctor`) because they run on every utterance and must work anywhere.
`bash` is fine for the developer-facing scripts under `scripts/` and `tests/`.

### Comments explain *why*

The codebase has a consistent convention worth preserving: a comment earns its
place by recording something that cost time to learn and is invisible from the
code. Look at `skip_start_ms`, or the `execvp` fallback, or the `--no-repeat`
note in the sway binding. Comments that restate the code are noise; comments
that stop the next person re-deriving a two-hour debugging session are the
point.

## Commits

Imperative mood, describing the change from the user's side:

```
Type 13x faster
Stop transcribing silence, and stop recording our own start beep
Find helper programs regardless of PATH, and report exec failures
```

Not `fix bug` or `update daemon.c`.

## Adding a compositor

[wm/README.md](wm/README.md) marks which snippets are verified on a real system
(currently only Hyprland) and which are transcribed from documentation. If you
run one of the unverified ones, saying so — and correcting it — is a genuinely
useful contribution.

Push-to-talk needs a **key-release** binding. If your compositor has none, say
that explicitly in the snippet and point at `whispr dictate` instead.

## Design notes

`docs/plans/` holds the original design document and an engineering handoff:
architecture, the measurements behind the defaults, and an honest list of what
is and is not verified. Read the handoff before changing anything in `src/`.
