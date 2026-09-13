# whispr

Local voice dictation for Linux. Hold a key, speak, release — the text is typed
into whatever window has focus.

No network. No API key. No account. The audio never leaves the machine, because
there is nothing in it that can send audio anywhere.

```
  hold key ──► speak ──► release ──► text appears where your cursor is
                                      ~0.5s later
```

Built on [whisper.cpp](https://github.com/ggml-org/whisper.cpp).

---

## Why this one

- **It is a daemon and a CLI.** No tray icon, no settings window, no browser
  tab. Configuration is a text file; state is `whispr status --json`, which a
  status bar can poll in one line of shell.
- **It is fast enough to use mid-sentence.** ~210 ms to transcribe, ~280 ms to
  type. The model loads while you are still talking, so the wait after you
  release the key is decode time only.
- **It is distro-agnostic.** PipeWire, PulseAudio or ALSA. Wayland or X11.
  systemd or not. Arch, Debian, Fedora, openSUSE, Alpine, Void — the installer
  detects the family and names the right packages.
- **It does not grab your keyboard.** Reading keys directly would mean joining
  the `input` group, which lets every process you run keylog your session.
  Your window manager owns the hotkey and calls a tiny client over a Unix
  socket. That is also why whispr never needs to know which compositor you use.
- **It stays quiet when you do.** Whisper will confabulate speech from room
  tone, and a tool that types into the focused window must not. Four guards
  sit in front of the decoder; the silence fixture in the test suite asserts
  that nothing comes out.

## Status

| | |
|---|---|
| Engine | Whisper `base.en` via `libwhisper` — Vulkan if available, CPU otherwise |
| Latency | ~0.5 s from key release to typed text |
| Accuracy | WER 0.00 / 0.095 / 0.158 on the bundled short / plain / jargon fixtures |
| Alternative engine | NVIDIA Parakeet, implemented and switchable, not the default — [why](#engines) |
| Upstream | `ggml-org/whisper.cpp`, pinned at v1.9.4 |
| Verified on | CachyOS + Hyprland + PipeWire + Vulkan. One machine, one voice, one room |

Working and in daily use — but by one person, on one setup. If you run it
somewhere else, an issue saying what happened is the most useful thing you can
contribute.

---

## Install

Arch and derivatives:

```sh
sudo pacman -S --needed base-devel cmake git curl ydotool libnotify \
                       vulkan-headers spirv-headers glslang shaderc

git clone --recursive https://github.com/kenzik/popshat-whispr.git
cd popshat-whispr
./scripts/install.sh
```

**Debian, Ubuntu, Fedora, openSUSE, Alpine, Void, or no systemd at all:** see
[docs/install.md](docs/install.md). The installer detects your distribution and
tells you what to install if something is missing, so running it first is a
reasonable way to find out.

The Vulkan packages are optional — CPU is a supported configuration, not a
degraded one. A GPU takes encode from 278 ms to 7.5 ms; it does not change what
gets transcribed.

## Bind a key

```sh
scripts/probe-keys.sh
```

Press the key you want. It prints what that key *actually* sends and a
ready-to-paste binding, then [wm/README.md](wm/README.md) has snippets for
Hyprland, sway/i3, river, KDE, GNOME, sxhkd and xbindkeys.

Two things that will otherwise cost you an evening:

**The obvious keysym name is often wrong.** A keyboard's F13 arrives through
xkb as `XF86Tools`. A binding written as `F13` silently never fires — no error,
no log. Probe the key; do not guess.

**The key must carry no modifier.** With `SUPER+grave` you release the combo and
whispr starts typing while `SUPER` is still physically down, so every character
of your transcript becomes a window-manager shortcut. F13–F24, spare macro
keys, or a remapped Caps Lock. No spare key? Bind `whispr toggle` instead —
it is a tap, so nothing is held while typing.

## Use

| Command | |
|---|---|
| `whispr start` / `whispr stop` | push-to-talk; bind to key press and release |
| `whispr dictate` | hands-free: tap, speak, and it stops once you go quiet |
| `whispr toggle` | tap on, tap off |
| `whispr cancel` | discard without typing |
| `whispr status [--json]` | `idle` / `recording` / `transcribing`, for a bar |
| `whispr reload` | re-read the config, including an engine swap |
| `whispr-doctor` | one-screen environment report; attach it to bug reports |

Push-to-talk needs a compositor that can bind key *releases* — Hyprland, sway,
river and sxhkd can; KDE and GNOME cannot, and use `dictate` or `toggle`.

---

## Accuracy

Everything in `~/.config/whispr/config`. The one setting worth your attention:

```
whisper.initial_prompt = Kubernetes, Postgres, Grafana, Anika, Okonkwo, SRE
```

This primes the decoder with your vocabulary, and it is the cheapest and
largest accuracy lever available. On the bundled jargon fixture:

| | WER | |
|---|---|---|
| no prompt | 0.42 | *"Hyperland on cacheOS with Pipewire YDO tool … Vulcan back end"* |
| with prompt | 0.16 | *"Hyperland on CachyOS with PipeWire ydotool … Vulkan backend"* |

It ships **empty**, deliberately: the gain only applies to words you actually
say, so shipping one person's vocabulary would bias everyone else's decoder
toward terms they never use. Fill it with your own proper nouns, jargon and
names, and add to it whenever you catch a mangling.

Only then reach for a bigger model (`ggml-small.en.bin` is a drop-in).

### Engines

| | short | plain | jargon | wall clock | size |
|---|---|---|---|---|---|
| **whisper base.en** + prompt | 0.000 | 0.095 | **0.158** | **221 ms** | **142 MB** |
| parakeet tdt-0.6b-v3 | 0.000 | 0.143 | 0.474 | 434 ms | 1.2 GB |

Parakeet is fully implemented — `engine = parakeet`, `whispr reload`, and
`install.sh --with-parakeet` to fetch the weights. It is not the default
because most of that jargon gap is structural: Parakeet TDT has no prompt
conditioning to offer. Against an *unprimed* Whisper the two are close (0.421
vs 0.474), so this is a statement about the workload — short English dictation
dense with proper nouns — not about the model, which is strong on general
speech and covers 25 languages to `base.en`'s one. Worth revisiting if you
dictate in anything but English. Its weights are CC-BY-4.0, not MIT; see
[THIRD-PARTY.md](THIRD-PARTY.md).

---

## How it works

```
  WM keybind ──► whispr (client) ──unix socket──► whispr daemon
                                                     │
                               pw-record ────────────┤ f32 16kHz mono
                                                     │
                               libwhisper ───────────┤ worker thread
                                                     │
                               whispr-inject ◄───────┘ ydotool → focused window
```

One binary is both the daemon and the client. The daemon runs an `epoll` loop
over the listen socket, the capture pipe and three timerfds — a recording
watchdog, an idle-unload timer, and the worker's done-eventfd. Transcription
and injection happen on a single worker thread, so `whispr status` never stalls
behind a 300 ms `ydotool type`.

Three decisions shaped the rest:

**The window manager owns the hotkey.** Security first — no `input` group, no
raw device access — and portability falls out of it for free: the daemon never
learns which compositor it runs under, so `wm/README.md` is the only file that
differs between systems.

**C linking `libwhisper` directly, not a language binding.** It `#include`s
`whisper.h`, so struct layouts cannot drift the way a hand-mirrored
`whisper_full_params` would. The low-risk option, not the ambitious one.

**The model loads when recording *starts*, not when it ends.** Loading overlaps
with speech, so the wait after the key release is decode time only. It is
dropped after 10 minutes idle — 317 MiB of VRAM back down to 7.

There is a longer engineering document, including the measurements behind every
default and an explicit list of what is *not* verified, in
[docs/plans/](docs/plans/).

---

## Tests

```sh
./tests/run-tests.sh          # WER against recorded fixtures; fast, automated
./scripts/acceptance.sh       # operator-driven, drives the real hotkeys
```

`run-tests.sh` asserts **word error rate under a threshold**, not string
equality — ASR never matches byte-for-byte — and normalizes first, so
`"sixty four"` and `"64"` compare equal. Thresholds are a **ratchet: tighten
only.** One loosened to make a failing run go green catches nothing ever again.

One fixture is silence. Whisper emits `[BLANK_AUDIO]`, `[Music]` or subtitle
credits over room tone, and in this tool that text would be typed into whatever
you had focused — so marker-stripping is tested, not assumed.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Particularly welcome: a compositor
snippet that actually works, a distribution that does or does not build, or a
fixture in a voice that is not the author's.

Run `whispr-doctor` and paste the output into any bug report — it answers most
of what a maintainer would otherwise have to ask.

## License

MIT — see [LICENSE.txt](LICENSE.txt).

whispr bundles nothing. whisper.cpp is a submodule (MIT) and the models are
downloaded from their own upstreams at install time; the Whisper and Silero
models are MIT, the optional Parakeet weights are CC-BY-4.0. Full attribution
in [THIRD-PARTY.md](THIRD-PARTY.md).
