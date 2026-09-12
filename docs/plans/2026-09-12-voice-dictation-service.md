# popshat-whispr — local voice dictation into the focused window

## Context

You want push-to-talk / toggle voice dictation on CachyOS that transcribes locally and types the
result into whatever window has focus. You asked for `ggml-org/whisper.cpp`, then asked whether
Parakeet + Vulkan was possible, since Parakeet works well for you on an M3 Ultra.

**It is — in the repo you originally named.** Upstream `ggml-org/whisper.cpp` gained first-class
NVIDIA Parakeet support in v1.9.x: `include/parakeet.h`, `examples/parakeet-cli/`, a converter, a
quantizer, and an installable `parakeet` CMake library target. Latest release **v1.9.4
(2026-09-11)**; pre-converted weights at `ggml-org/parakeet-GGUF`. Because Parakeet runs on ggml
here, the Vulkan backend applies and no CUDA toolkit is needed.

But we **ship Whisper first and add Parakeet as a fast follow.** Whisper is the mature path in
that tree, so shipping it first gets you a working tool on known-good ground and moves the one
genuine unknown — whether ggml's Vulkan backend covers FastConformer's depthwise convolutions —
off the critical path. The engine sits behind a vtable from day one, so Parakeet is an additive
change, not a rewrite.

### Decisions made

| Decision | Choice |
|---|---|
| Engine (v1) | **Whisper**, `ggml-base.en.bin` — whisper.cpp's own default |
| Engine (fast follow) | **Parakeet** `parakeet-tdt-0.6b-v3-f16` (1.20 GB), same vtable |
| Backend | Vulkan (`-DGGML_VULKAN=ON`), CPU fallback via config |
| Hotkeys | **F13** hold = push-to-talk, **F14** tap = toggle (VIA/QMK on the Keychron V6) |
| Injection | `ydotool` via `/dev/uinput`, with `wtype`/`xdotool`/clipboard fallbacks |
| Residency | Lazy load, unload after 10 min idle |
| Feedback | Audio cue + desktop notification, plus `status` for a bar module |
| Hypr config | New `~/.config/hypr/config/whispr.lua`; **you** add the `require` line |

### Why the architecture is shaped this way

Two environment facts drove it:

1. **Hotkeys must come from the WM, not the daemon.** Only 1 of your 26 `/dev/input/event*` nodes
   is readable by you; the Keychron V6 nodes are `root:input`. Joining `input` would let any
   process on the box keylog you. So the daemon listens on a Unix socket and the WM's keybind
   calls a tiny client — which is also what makes this portable: only the binding snippet changes
   per WM, never the daemon.
2. **`/dev/uinput` is already writable by you** (ACL via `TAG+="uaccess"`), so `ydotool` runs
   unprivileged. Caveat: that rule ships in `/usr/lib/udev/rules.d/60-steam-input.rules`, i.e.
   we'd be depending on Steam. We ship our own rule instead.

F13/F14 also sidestep a real hazard: with a modifier-based PTT key you release the combo and the
daemon types while `SUPER` is still physically down — every character fires a WM shortcut.
Modifier-free keys make that impossible, so no settle-delay hack is needed.

---

## Layout

```
~/src/popshat-whispr/            # git init (currently empty, not a repo)
├── docs/plans/
│   └── 2026-09-12-voice-dictation-service.md   # this plan, committed first
├── tests/
│   ├── fixtures/                # real recordings of your voice + ground truth
│   ├── wer.py                   # word error rate, stdlib only
│   └── run-tests.sh
├── vendor/whisper.cpp           # submodule, pinned to v1.9.4
├── CMakeLists.txt               # add_subdirectory(vendor/whisper.cpp); link whisper (+ parakeet)
├── src/
│   ├── main.c                   # arg dispatch: daemon vs client
│   ├── daemon.c/.h              # poll() loop, state machine, worker thread
│   ├── engine.c/.h              # vtable: whisper now, parakeet in phase 3
│   ├── audio.c/.h               # fork/exec capture child -> f32 PCM buffer
│   ├── ipc.c/.h                 # $XDG_RUNTIME_DIR/whispr.sock protocol
│   └── config.c/.h              # ~/.config/whispr/config (key = value)
├── scripts/{whispr-inject,whispr-notify,install.sh}
├── systemd/{whispr.service,ydotoold.service}
├── wm/hyprland/whispr.lua       # + sway / i3-sxhkd / KDE / GNOME notes in wm/README.md
└── README.md
```

One binary, `whispr`, is both daemon and client (`whispr daemon` vs `whispr start|stop|…`).
Writing the daemon in C linking `libwhisper` is the *low-risk* option, not the ambitious one: it
`#include`s `whisper.h` directly, so struct layouts can never drift out of sync the way a
Python/ctypes mirror of `whisper_full_params` would.

---

## Phase 0 — repo and plan document

`git init`, write this plan to **`docs/plans/2026-09-12-voice-dictation-service.md`**, commit it
before any code lands. Later plans use the same `YYYY-MM-DD-<slug>.md` convention. If the design
changes during implementation, amend that document in the same commit as the change.

## Phase 1 — record test fixtures (do this first; you're present for it)

**This is time-critical — it needs you at the mic, and everything afterwards can run without
you.** We capture your voice on your default input (the TONOR G11 USB mic) so there is a real,
regression-stable fixture to test against, rather than `samples/jfk.wav`. It also becomes the
apples-to-apples baseline for the Parakeet comparison in phase 3 — same audio, same ground truth,
two engines.

Captured with `pw-record --rate=16000 --channels=1 --format=s16 <out>.wav` — 16 kHz mono is
exactly what both engines consume, so no resampling enters the test path. I'll prompt you with
each line, you read it, I save the take and its ground truth next to it.

| Fixture | What you'll be asked to read | Exercises |
|---|---|---|
| `short.wav` | "Commit and push." | short-clip path, `single_segment`, the `min_record_ms` guard |
| `plain.wav` | "Please open the configuration file and increase the buffer size to sixty four kilobytes, then restart the service and check the logs." | ordinary prose baseline |
| `jargon.wav` | "I'm running Hyprland on CachyOS with PipeWire and ydotool, and the whisper dot cpp build uses the Vulkan backend." | the hard case — motivates `initial_prompt` |

Each take is saved as `tests/fixtures/<name>.wav` plus `<name>.txt` holding the exact sentence as
ground truth. If a take is misread or noisy we just redo it — better to spend a minute now than
to bake a bad baseline into the test suite.

`tests/wer.py` computes word error rate (word-level Levenshtein, stdlib only, no deps) after
case-folding and stripping punctuation. `tests/run-tests.sh` transcribes each fixture and asserts
WER is under a per-fixture threshold — **not** exact string match, which ASR will never satisfy.
Thresholds start loose for `base.en` (≤ 0.25 on `jargon`, ≤ 0.10 on `plain`) and get ratcheted
down as we move to better models; a ratchet that only ever tightens is what turns this into a
real regression test.

## Phase 2 — working tool on Whisper

### 2.1 Dependencies and model

Missing, install via pacman: `cmake`, `vulkan-headers`, `ydotool` (plus `wev` to verify F13/F14).
Already present: `gcc`, `make`, `glslang`, `shaderc`, `git`, `wget`, `jq`, `pw-record`,
`notify-send`, `paplay`, `wl-copy`, `python3`.

Model → `~/.local/share/whispr/models/ggml-base.en.bin` (~142 MB, confirmed as `whisper-cli`'s
own default at `examples/cli/cli.cpp:87`):
`https://huggingface.co/ggml-org/whisper.cpp/resolve/main/ggml-base.en.bin`

Build: `cmake -B build -DGGML_VULKAN=ON -DWHISPER_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release`
(examples ON so `whisper-cli` exists for the smoke test; can go OFF later).

### 2.2 `whispr` daemon — `src/daemon.c`

States `IDLE → RECORDING → TRANSCRIBING → IDLE`. Main thread runs one `poll()` over the listen
socket, the capture pipe, and two timerfds; transcription + injection run on a worker thread so
`status` never stalls behind a 300 ms `ydotool type`.

Socket `$XDG_RUNTIME_DIR/whispr.sock`, newline-delimited commands:

| Command | Behaviour |
|---|---|
| `start` | begin capture; no-op if already recording |
| `stop` | end capture, transcribe, inject |
| `toggle` | `start` if idle, else `stop` |
| `cancel` | discard audio, inject nothing |
| `status` | `idle`/`recording`/`transcribing` + elapsed; `--json` for a bar module |
| `reload` | re-read config (including engine swap) |

**Capture** — `fork`/`exec` a child writing raw f32le 16 kHz mono to a pipe; parent reallocs into
a buffer. First available of:

1. `pw-record --raw --rate=16000 --channels=1 --format=f32 --target=<src> -`  ← verified working
2. `parecord --raw --rate=16000 --channels=1 --format=float32le --device=<src>`
3. `arecord -f FLOAT_LE -r 16000 -c 1 -t raw`
4. `ffmpeg -f pulse -i default -ar 16000 -ac 1 -f f32le -`

`stop` sends `SIGTERM`, drains the pipe, `waitpid`s.

**Model lifecycle** — load is kicked off on **`start`**, not `stop`, so it overlaps with you
speaking and is effectively free after the first word. A 10-minute idle timerfd frees the context.

**Guards** — `max_record_seconds` (120) auto-stops if a release event is ever lost, so PTT can't
stick on; clips under `min_record_ms` (300) are dropped silently so a stray tap doesn't notify.

### 2.3 Engine vtable — `src/engine.c`

`engine.h` exposes `load(opts)`, `transcribe(pcm, n, out)`, `unload()`. Whisper is the only
implementation in this phase, but the seam exists from the first commit so phase 3 is additive.
Both engines take f32 16 kHz mono, so the capture path is shared verbatim and nothing else in the
daemon knows which engine is live.

Whisper path: `whisper_init_from_file_with_params` with `use_gpu`/`gpu_device`; `whisper_full`
with `no_timestamps = true`, `single_segment` for short clips, `n_threads = 8`; segments via
`whisper_full_n_segments` / `whisper_full_get_segment_text`. Three params matter for dictation and
are wired to config immediately:

- **`initial_prompt`** — primes the decoder with your vocabulary (`"CachyOS, Hyprland, ydotool,
  PipeWire, Keychron, zsh, systemd"`). This is the direct lever on the `jargon.wav` fixture.
- **`translate`** — speak another language, get English text.
- **`vad` / `vad_model_path` / `whisper_vad_params`** — built-in Silero VAD, models at
  `ggml-org/whisper-vad`. Off in v1; it's the clean path to silence auto-stop later.

### 2.4 `scripts/whispr-inject` — text on stdin

First backend that works: `ydotool type --file -` → `wtype -` → `xdotool type --clearmodifiers
--file -` → `wl-copy`/`xclip` plus a "copied to clipboard" notification. `WHISPR_INJECT_DELAY_MS`
defaults to **0** (unnecessary with F13/F14); document raising it to ~120 only if PTT ever moves
onto a modified key.

### 2.5 Feedback — `scripts/whispr-notify`

Sound on start/stop from `/usr/share/sounds/freedesktop/stereo/`; notifications tagged
`-h string:x-canonical-private-synchronous:whispr` so they replace rather than stack. Always
spawned detached so feedback can never delay injection.

### 2.6 systemd user units + udev

- `ydotoold.service` — `ydotoold --socket-path=%t/.ydotool_socket`, `Restart=always`.
- `whispr.service` — `%h/.local/bin/whispr daemon`, `PartOf=graphical-session.target`,
  `Restart=on-failure`. You run Hyprland under uwsm, which imports `WAYLAND_DISPLAY` /
  `XDG_RUNTIME_DIR` into the systemd user manager; README documents
  `systemctl --user import-environment` as the fallback for other setups.
- `/etc/udev/rules.d/99-whispr-uinput.rules` (the one root action, `install.sh` prompts) declaring
  `KERNEL=="uinput", SUBSYSTEM=="misc", TAG+="uaccess"` so uinput access survives Steam being
  removed.

### 2.7 `~/.config/hypr/config/whispr.lua`

Verified against `/usr/share/hypr/stubs/hl.meta.lua`: the bind opts table really does accept
`release?: boolean` (alongside `repeating`, `locked`, `long_press`), so PTT is a clean pair.

```lua
-- whispr: local voice dictation
hl.bind("F13", hl.dsp.exec_cmd("whispr start"))
hl.bind("F13", hl.dsp.exec_cmd("whispr stop"), { release = true })
hl.bind("F14", hl.dsp.exec_cmd("whispr toggle"))
hl.bind("SUPER + F14", hl.dsp.exec_cmd("whispr cancel"))
```

Your `binds.lua` is untouched. You add one line to `~/.config/hypr/hyprland.lua`:
`require("config.whispr")`. Uninstall = delete one file, remove one line.

Prerequisite: map two unused Keychron V6 keys to **F13**/**F14** in VIA/QMK, and confirm with
`wev` that they arrive as F13/F14 before binding.

### 2.8 `~/.config/whispr/config`

Flat `key = value`; engine-specific keys namespaced so both stay configured and switching is a
one-word edit plus `whispr reload`:

```ini
engine              = whisper           # whisper | parakeet (phase 3)
use_gpu             = true
gpu_device          = 0
n_threads           = 8

whisper.model       = ~/.local/share/whispr/models/ggml-base.en.bin
whisper.language    = en
whisper.translate   = false
whisper.initial_prompt =                # e.g. "CachyOS, Hyprland, ydotool, PipeWire"

parakeet.model      =                   # filled in during phase 3

source              =                   # empty = default PipeWire source
max_record_seconds  = 120
min_record_ms       = 300
idle_unload_seconds = 600
append_space        = true
inject_cmd          = whispr-inject
notify_cmd          = whispr-notify
```

## Phase 3 — Parakeet fast follow

Additive: a second `engine.c` implementation plus a model download. The call sequences are
one-to-one with Whisper's, which is why the vtable is thin:

| Operation | Whisper | Parakeet |
|---|---|---|
| init | `whisper_init_from_file_with_params` | `parakeet_init_from_file_with_params` |
| context params | `whisper_context_params{use_gpu,gpu_device}` | `parakeet_context_params{use_gpu,gpu_device}` |
| run | `whisper_full` | `parakeet_full` / `parakeet_chunk` |
| segments | `whisper_full_n_segments` / `…_get_segment_text` | `parakeet_full_n_segments` / `…_get_segment_text` |
| free | `whisper_free` | `parakeet_free` |

Model → `ggml-parakeet-tdt-0.6b-v3-f16.bin` (1.20 GB, verified) from
`https://huggingface.co/ggml-org/parakeet-GGUF/resolve/main/ggml-parakeet-tdt-0.6b-v3-f16.bin`.
Use `parakeet_chunk` for short clips (the header notes it is "more efficient than
`parakeet_full()` for short audio clips" — confirm its ceiling during implementation).

Acceptance is the phase 1 fixtures: run both engines over the identical recordings and compare WER
and wall-clock. Parakeet TDT 0.6B v3 is 25-language with ~6.3% avg English WER on the Open ASR
leaderboard vs ~7.4% for Whisper large-v3, and decodes far cheaper than an autoregressive Whisper
decoder — so we expect it to win on both axes. If it doesn't, `engine = whisper` stays the
default and we've lost nothing.

---

## Verification

1. **Fixtures exist and are sane** — play back each `tests/fixtures/*.wav`, confirm audio is clean
   and the `.txt` matches what was actually said.
2. **Vulkan gate** — `whisper-cli -m ggml-base.en.bin -f tests/fixtures/plain.wav`, then again
   with `-ng`. Text must match; compare timings. Fallback is `use_gpu = false`, no rebuild.
3. **Unit tests** — `tests/run-tests.sh` passes WER thresholds on all three fixtures.
4. **Capture + transcribe** — `whispr daemon --foreground --no-inject`, then
   `whispr start; sleep 3; whispr stop`; transcript prints to stdout.
5. **Injection** — `echo "hello world" | whispr-inject` into a terminal, a browser address bar,
   and an Electron app. Confirm the clipboard is untouched on the ydotool path.
6. **End-to-end** — hold F13 in a text editor, speak, release; then tap F14, speak, tap F14.
7. **Latency** — daemon logs per-stage ms (capture / model load / encode / decode / inject).
8. **Robustness** — `start` with no `stop` (watchdog fires at `max_record_seconds`); `kill` the
   capture child mid-recording; second PTT during transcription; `systemctl --user restart whispr`
   mid-recording.
9. **Idle unload** — memory released 10 minutes after the last utterance.
10. **Phase 3 only** — `engine = parakeet`, `whispr reload`, rerun step 3 and compare against the
    Whisper numbers on the same fixtures.

## Risks

1. **Vulkan op coverage** — ggml's Vulkan backend is historically weaker on convolution ops than
   CUDA. Shipping Whisper first means this is now gated at step 2 on a well-trodden path, and the
   riskier FastConformer case is deferred to phase 3 where it can't block a working tool.
   Fallbacks, none needing a rebuild: `use_gpu = false`, or staying on `engine = whisper`. Only
   for maximum speed does the escape hatch involve installing `cuda` and `-DGGML_CUDA=ON`.
2. **`base.en` accuracy is modest**, especially on `jargon.wav`. That is expected and is why the
   WER thresholds start loose and ratchet. Levers in order of cost: `whisper.initial_prompt`, then
   `ggml-small.en.bin`, then phase 3.
3. **Upstream Parakeet support is young** — added in v1.9.0 with TDT decode fixes as recently as
   v1.9.2. Submodule pinned to v1.9.4; treat bumps as deliberate.
4. **uinput ACL provenance** — mitigated by our own udev rule (§2.6).
5. **systemd user env** — if `WAYLAND_DISPLAY` is missing from the user manager, injection
   silently no-ops; `install.sh` checks and says so rather than failing mysteriously.
6. **F13/F14 don't exist until mapped in VIA** — verify with `wev` before step 6.

## Out of scope for v1

Silence-based auto-stop (VAD), post-hoc replacement lists, streaming partial results. The config
gets a hook for the replacement list, and VAD is already in-tree (`whisper_vad_params`) whenever
we want it.
