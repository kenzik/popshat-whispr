# Installing whispr

whispr is distribution-agnostic by design. It needs:

| | |
|---|---|
| A Linux system | any init, any distro |
| A capture program | **PipeWire** (`pw-record`), **PulseAudio** (`parecord`) or **ALSA** (`arecord`) — whichever you already have |
| A way to type into a window | `ydotool`, or `wtype` (Wayland) / `xdotool` (X11), or a clipboard tool as last resort |
| A window manager that can run a command on a keypress | essentially all of them |
| A C and C++ compiler, CMake ≥ 3.16, git, curl | build only |

Nothing else is assumed. systemd is used if present and skipped if not. Wayland
and X11 both work. A GPU is optional — Vulkan is an optimization, and CPU is a
fully supported configuration, not a degraded one.

> **Verified on:** CachyOS (Arch), Hyprland, PipeWire, Vulkan on an NVIDIA GPU.
> That is the only system whispr has actually been built and run on.
>
> The package lists below were checked against each distribution's current
> package index, so the names exist — but nobody has installed them and built
> whispr there. If you do, an issue saying what happened, working or broken, is
> genuinely useful: see the *Platform report* issue template.

---

## Quick start (Arch and derivatives)

```sh
sudo pacman -S --needed base-devel cmake git curl ydotool libnotify \
                       vulkan-headers spirv-headers glslang shaderc

git clone --recursive https://github.com/kenzik/popshat-whispr.git
cd popshat-whispr
./scripts/install.sh
```

Then bind a key — see [../wm/README.md](../wm/README.md).

CachyOS, EndeavourOS, Manjaro and the rest are detected as Arch automatically.

---

## Other distributions

The installer detects your distribution and prints the right package command if
something is missing, so you can also just run it and follow what it says.

The Vulkan packages are **optional**. Skip them, or pass `--no-gpu`, and whispr
builds for CPU.

One of them catches people out: ggml needs the **`glslc` binary** to compile its
shaders, not just the Vulkan headers and the shaderc library. On Debian, Fedora
and Alpine that binary ships in its own package; Arch bundles it into `shaderc`.
Configuring with the headers present but `glslc` missing fails with
`Could NOT find Vulkan (missing: glslc)` — the installer recognizes that
specific error and says so.

### Debian, Ubuntu, Mint, Pop!_OS

```sh
sudo apt install build-essential cmake git curl ydotool libnotify-bin
# optional, for GPU:
sudo apt install libvulkan-dev spirv-headers glslang-tools libshaderc-dev glslc
```

Check `cmake --version` first — whispr needs 3.16 or newer, and some older LTS
releases ship less than that. `pip install cmake` or the Kitware apt repository
both solve it.

### Fedora, RHEL, CentOS

```sh
sudo dnf install gcc gcc-c++ cmake git curl ydotool libnotify
# optional, for GPU:
sudo dnf install vulkan-headers spirv-headers-devel glslang-devel libshaderc-devel glslc
```

### openSUSE

```sh
sudo zypper install gcc gcc-c++ cmake git curl ydotool libnotify-tools
# optional, for GPU:
sudo zypper install vulkan-devel spirv-headers glslang-devel shaderc-devel shaderc
```

These names could not be checked against openSUSE's index (its package search
refuses automated requests), so treat them as a starting point rather than a
recipe. `zypper search` will find the real ones.

### Alpine

```sh
sudo apk add build-base cmake git curl wtype xdotool libnotify
# optional, for GPU:
sudo apk add vulkan-headers spirv-headers glslang-dev shaderc-dev shaderc
```

Two Alpine-specific notes. **There is no `ydotool` package** — `wtype` (Wayland)
and `xdotool` (X11) are the injection backends instead; both work, need no
daemon, and skip the `/dev/uinput` setup entirely. And Alpine has no systemd,
so see [Without systemd](#without-systemd) below.

### Void

```sh
sudo xbps-install -S base-devel cmake git curl wtype xdotool libnotify
# optional, for GPU:
sudo xbps-install -S Vulkan-Headers SPIRV-Headers glslang-devel shaderc
```

Void does not package `ydotool` either — `wtype` and `xdotool` cover it. Void
uses runit rather than systemd; see [Without systemd](#without-systemd).

### NixOS, Guix, or anything else

The dependency list at the top of this page is the whole contract. Provide
those and `./scripts/install.sh --skip-models` works; fetch the models
separately (see [Models](#models)). A packaged expression would be a welcome
contribution.

---

## What the installer does

```sh
./scripts/install.sh [options]

  --with-parakeet   also fetch the Parakeet model (1.2 GB, CC-BY-4.0 weights)
  --no-gpu          skip the Vulkan probe and build for CPU
  --skip-models     download nothing; assume the models are in place
  --no-service      install the files but do not touch systemd
  --prefix DIR      install under DIR instead of ~/.local
```

1. **Builds** whisper.cpp and whispr. Vulkan is probed by *attempting the
   configure*, not by sniffing for a header — the backend needs four separate
   packages and checking one only moves the failure later. If it fails, the
   build falls back to CPU and says which packages would enable GPU. Failing to
   get a GPU never fails the install.
2. **Downloads models** to `~/.local/share/whispr/models/`, checking each one's
   exact byte size before moving it into place. A truncated download otherwise
   surfaces much later as an unhelpful model-load error.
3. **Installs** `whispr`, `whispr-inject`, `whispr-notify` and `whispr-doctor`
   to `~/.local/bin`, and a default config to `~/.config/whispr/config`. An
   existing config is never overwritten.
4. **Asks for sudo once**, for a udev rule that makes `/dev/uinput` accessible
   to the logged-in user. That node is what lets `ydotool` type. It is often
   already accessible — but via a rule shipped by some other package, which
   means typing breaks the day that package is removed. whispr declares its own.
   Without sudo, the installer prints the two commands to run as root.
5. **Enables user services** (`whispr`, `ydotoold`) if systemd is running.

The binary is linked statically against whisper.cpp, so `~/.local/bin/whispr`
keeps working after you delete the clone.

---

## Without systemd

whispr's daemon is an ordinary long-running process. Run `install.sh
--no-service`, then start these two from your compositor's autostart, your
`~/.xinitrc`, or whatever your init provides:

```sh
ydotoold --socket-path="$XDG_RUNTIME_DIR/.ydotool_socket" --socket-own="$(id -u):$(id -g)" &
~/.local/bin/whispr daemon &
```

`ydotoold` is only needed for the ydotool typing backend. `wtype` and `xdotool`
need no daemon, no udev rule and no `/dev/uinput` — which makes them the simpler
choice, and the only choice on distributions that do not package ydotool.

whispr-inject tries, in order: **ydotool** (types into anything, tunable speed),
**wtype** (Wayland only), **xdotool** (X11 only), then the clipboard. The
clipboard is a last resort rather than a default because clobbering it is a side
effect nobody asked for.

---

## Models

| File | Size | Needed? |
|---|---|---|
| `ggml-base.en.bin` | 141 MB | yes — the speech model |
| `ggml-silero-v5.1.2.bin` | 865 KB | **yes** — voice activity detection |
| `ggml-parakeet-tdt-0.6b-v3-f16.bin` | 1.2 GB | no — alternative engine |

They live in `~/.local/share/whispr/models/` (override with
`WHISPR_MODEL_DIR`). Licenses are in [../THIRD-PARTY.md](../THIRD-PARTY.md);
note that the Parakeet weights are CC-BY-4.0 rather than MIT.

**The VAD model is not optional.** The default config enables VAD, and
whisper.cpp fails the entire transcription when the model is absent — so a
system missing that 865 KB file reports an error on every single utterance.
Turn it off only if you mean to: without VAD, Whisper invents plausible speech
from room tone, and whispr types the result into whatever window has focus.

Larger speech models are drop-in. Fetch one and point `whisper.model` at it:

```sh
curl -L -o ~/.local/share/whispr/models/ggml-small.en.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin
```

Try `whisper.initial_prompt` before reaching for a bigger model — on the bundled
fixtures it is worth more than the jump from `base.en` to `small.en`.

---

## Building by hand

```sh
git clone --recursive https://github.com/kenzik/popshat-whispr.git
cd popshat-whispr
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=ON
cmake --build build -j"$(nproc)" --target whispr
```

Drop `-DGGML_VULKAN=ON` for a CPU build. The binary lands at `build/whispr` and
can be run straight from there:

```sh
./build/whispr daemon --no-inject -v
```

`--no-inject` prints transcripts to stdout instead of typing them into whatever
window has focus. Use it for anything automated.

---

## Verifying the install

```sh
whispr-doctor
```

One screen covering every layer that can silently fail: PATH, capture program,
`ydotoold`, `/dev/uinput`, models, config, daemon state, and whether the
systemd user manager knows about your display. Paste it into bug reports.

```sh
./tests/run-tests.sh      # transcribes the bundled fixtures, asserts WER
```

---

## Uninstalling

```sh
systemctl --user disable --now whispr.service ydotoold.service
rm -f  ~/.local/bin/whispr ~/.local/bin/whispr-{inject,notify,doctor}
rm -f  ~/.config/systemd/user/{whispr,ydotoold}.service
rm -rf ~/.local/share/whispr ~/.config/whispr
sudo rm -f /etc/udev/rules.d/99-whispr-uinput.rules /etc/modules-load.d/whispr-uinput.conf
```

---

## Troubleshooting

Start with `whispr-doctor` — it checks everything below at once.

**Nothing happens at all: no sound, no notification, no text.**
Almost always `PATH`. Window managers and the systemd user manager run with the
login-session `PATH`, which does not include `~/.local/bin`, so a bare `whispr`
in a keybind is never found and fails silently. Use absolute paths in bindings.

**The key does nothing, but `whispr toggle` in a terminal works.**
The binding is not matching. Your key probably does not send the keysym you
think — run `scripts/probe-keys.sh`. On Hyprland, `scripts/diag-key.sh` tells
you whether the compositor matched the bind at all.

**Text lands on the clipboard instead of being typed.**
`ydotoold` is not running, or `/dev/uinput` is not writable. A re-login usually
applies the udev rule. Check `systemctl --user status ydotoold`.

**Recording starts but nothing is ever transcribed.**
Check the VAD model exists (see [Models](#models)). Then check the microphone is
actually the default source, and that `min_rms_dbfs` is not gating your voice
out — `whispr daemon -v` logs the measured level.

**It transcribes things nobody said.**
The microphone hears the room, and Whisper transcribes anything audible in it —
a video on another machine, a call on speaker. Verify a genuinely silent room
before concluding the model hallucinated.

**Recording never stops.**
It always does, after `max_record_seconds` (120 by default). If the release
event is being lost, the press binding is firing without the matching release
one; check both halves exist.

**Injection silently does nothing under systemd.**
```sh
systemctl --user import-environment WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR
```
The daemon inherits its environment from the systemd user manager. Without a
display variable it starts fine and then cannot reach the compositor.
