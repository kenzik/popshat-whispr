# whispr

Local voice dictation for Linux. Hold a key, speak, release — the text is typed
into whatever window has focus. No network, no cloud, no API key.

Built on [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (pinned at
v1.9.4). Whisper is the engine today; NVIDIA Parakeet is wired behind the same
interface and lands next.

## Status

| | |
|---|---|
| Engine | Whisper `base.en`, CPU |
| Hotkeys | circle hold = push-to-talk, square tap = hands-free, triangle tap = toggle |
| Injection | ydotool → wtype → xdotool → clipboard |
| Tested | WER 0.00 / 0.10 / 0.16 on the bundled fixtures |

## Install

```sh
git clone --recursive <this repo> && cd popshat-whispr
./scripts/install.sh
```

Then map two unused keys to **F13** and **F14** on your keyboard (VIA/QMK if
it is programmable), confirm with `wev`, and add the binding snippet — see
[wm/README.md](wm/README.md) for Hyprland, sway, KDE, GNOME and X11.

## Use

| Command | |
|---|---|
| `whispr start` / `whispr stop` | push-to-talk, bound to key press and release |
| `whispr dictate` | hands-free: starts, then stops on its own once you go quiet |
| `whispr toggle` | start if idle, else stop |
| `whispr cancel` | discard without typing |
| `whispr status [--json]` | for a bar module |
| `whispr reload` | re-read config, including an engine swap |

## How it is put together

```
  WM keybind ──► whispr (client) ──unix socket──► whispr daemon
                                                    │
                              pw-record ────────────┤ f32 16kHz mono
                                                    │
                              libwhisper ───────────┤ worker thread
                                                    │
                              whispr-inject ◄───────┘ ydotool → focused window
```

Three decisions are worth knowing about, because each is load-bearing:

**The daemon does not grab keys.** Only 1 of 26 `/dev/input/event*` nodes is
readable by an ordinary user, and joining the `input` group would let any
process on the machine keylog the session. The window manager owns the hotkey
and calls a tiny client over a Unix socket. That is also what makes this
portable — the daemon never learns which compositor it is running under.

**Modifier-free hotkeys.** With a modified key like `SUPER+grave`, you release
the combo and the daemon starts typing while `SUPER` is still physically down,
so every character becomes a window-manager shortcut. F13–F24 exist for exactly
this and nothing else binds them.

**The model loads when recording starts, not when it ends.** Loading overlaps
with you speaking, so the wait after you release the key is decode time only.
It is dropped again after 10 minutes idle to give the memory back.

## Accuracy

`whisper.initial_prompt` primes the decoder with your vocabulary and is the
cheapest lever available. On the bundled `jargon` fixture:

| | WER |
|---|---|
| no prompt | 0.42 — *"Hyperland on cacheOS with Pipewire YDO tool ... Vulcan back end"* |
| with prompt | 0.16 — *"Hyperland on CachyOS with PipeWire ydotool ... Vulkan backend"* |

Put your own terms in `whisper.initial_prompt`. Beyond that: `ggml-small.en.bin`,
then Parakeet.

## Tests

```sh
./tests/run-tests.sh
```

Transcribes recorded fixtures and asserts word error rate stays under a
threshold. WER, not string equality — ASR never matches byte-for-byte — and
normalised first, so `"sixty four"` versus `"64"` is not counted as an error.

Thresholds are a **ratchet: tighten only**. A threshold loosened to make a
failing run go green catches nothing ever again.

Record your own fixtures with `./tests/record-fixtures.sh`.

One fixture is silence. Whisper reliably emits `[BLANK_AUDIO]`, `[Music]` or
subtitle credits over room tone, and in this tool that text would be typed
straight into whatever you had focused — so stripping non-speech markers is
tested, not assumed.

## Troubleshooting

**Nothing is typed.** `systemctl --user show-environment | grep WAYLAND_DISPLAY`
— if absent the daemon cannot reach the compositor and injection silently does
nothing. Fix with
`systemctl --user import-environment WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR`.

**Text lands on the clipboard instead.** `ydotoold` is not running, or
`/dev/uinput` is not writable. `systemctl --user status ydotoold`.

**Recording never stops.** It always does, after `max_record_seconds`. If the
release event is being lost, check the key actually reports as F13 with `wev`.

## Licence

MIT. Model weights follow their own licences.
