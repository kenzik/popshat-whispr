#!/usr/bin/env bash
# Build and install whispr. Root is needed for exactly one thing (the udev
# rule) and is asked for explicitly at that point, never up front.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
MODELDIR="$HOME/.local/share/whispr/models"
CONFDIR="${XDG_CONFIG_HOME:-$HOME/.config}/whispr"
UNITDIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin"
MODEL_SIZE=147964211

say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33m==> %s\033[0m\n' "$*"; }

# ---------------------------------------------------------------- dependencies
say "Checking dependencies"
missing=()
for c in cmake gcc g++ git; do command -v "$c" >/dev/null || missing+=("$c"); done
command -v pw-record >/dev/null || command -v parecord >/dev/null || missing+=(pipewire-audio-or-pulseaudio)
command -v ydotool   >/dev/null || warn "ydotool not installed -- text will land on the clipboard instead of being typed"

if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing: ${missing[*]}" >&2
    echo "On Arch/CachyOS:  sudo pacman -S --needed cmake base-devel git ydotool \\" >&2
    echo "                       vulkan-headers spirv-headers glslang shaderc" >&2
    exit 1
fi

# ---------------------------------------------------------------------- build
say "Building"
git -C "$ROOT" submodule update --init --recursive

# Probe for Vulkan by attempting the configure rather than sniffing for a
# header. ggml's Vulkan backend needs vulkan-headers, spirv-headers, glslang and
# shaderc, and checking for one of them only moves the failure later -- as a
# vulkan.h check did, which passed and then died on missing SPIRV-Headers.
# GPU support is an optimisation, so failing to get it must never fail install.
CFGLOG="$(mktemp)"
say "Configuring with Vulkan"
if cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=ON >"$CFGLOG" 2>&1; then
    say "Vulkan enabled"
else
    warn "Vulkan unavailable -- falling back to CPU."
    if grep -q 'SPIRV-Headers' "$CFGLOG"; then
        warn "  Missing SPIRV-Headers. On Arch/CachyOS:  sudo pacman -S spirv-headers"
    else
        warn "  Missing one of: vulkan-headers spirv-headers glslang shaderc"
        warn "  Details: $CFGLOG"
    fi
    warn "  CPU is fully functional -- install the package and re-run for GPU."
    rm -rf "$ROOT/build"
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=OFF
fi
rm -f "$CFGLOG"

cmake --build "$ROOT/build" -j"$(nproc)" --target whispr

# ---------------------------------------------------------------------- model
mkdir -p "$MODELDIR"
if [ "$(stat -c%s "$MODELDIR/ggml-base.en.bin" 2>/dev/null || echo 0)" != "$MODEL_SIZE" ]; then
    say "Fetching model (141 MB)"
    curl -L --fail -o "$MODELDIR/ggml-base.en.bin.part" "$MODEL_URL"
    got=$(stat -c%s "$MODELDIR/ggml-base.en.bin.part")
    # Verify before moving into place: a truncated or rate-limited download
    # otherwise fails later as an unhelpful model-load error.
    [ "$got" = "$MODEL_SIZE" ] || { echo "model download is $got bytes, expected $MODEL_SIZE" >&2; exit 1; }
    mv "$MODELDIR/ggml-base.en.bin.part" "$MODELDIR/ggml-base.en.bin"
else
    say "Model already present"
fi

# -------------------------------------------------------------------- install
say "Installing to $PREFIX/bin"
install -Dm755 "$ROOT/build/whispr"            "$PREFIX/bin/whispr"
install -Dm755 "$ROOT/scripts/whispr-inject"   "$PREFIX/bin/whispr-inject"
install -Dm755 "$ROOT/scripts/whispr-notify"   "$PREFIX/bin/whispr-notify"

mkdir -p "$CONFDIR"
if [ ! -f "$CONFDIR/config" ]; then
    install -Dm644 "$ROOT/examples/config" "$CONFDIR/config"
    say "Wrote $CONFDIR/config"
else
    say "Keeping existing $CONFDIR/config"
fi

install -Dm644 "$ROOT/systemd/whispr.service"   "$UNITDIR/whispr.service"
install -Dm644 "$ROOT/systemd/ydotoold.service" "$UNITDIR/ydotoold.service"

# ------------------------------------------------------------------ udev rule
# /dev/uinput is typically already user-accessible, but via a rule shipped by
# Steam. Depending on another package's rule means text injection breaks the day
# it is uninstalled, so whispr declares its own.
if [ ! -f /etc/udev/rules.d/99-whispr-uinput.rules ]; then
    say "Installing udev rule for /dev/uinput (needs sudo)"
    echo 'KERNEL=="uinput", SUBSYSTEM=="misc", TAG+="uaccess", OPTIONS+="static_node=uinput"' \
        | sudo tee /etc/udev/rules.d/99-whispr-uinput.rules >/dev/null
    sudo udevadm control --reload-rules
    # The node exists via static_node= even with the module unloaded, so a
    # trigger before first open reports ENODEV ("No such device") and looks like
    # a failure when nothing is wrong. Load it explicitly instead, and make it
    # survive reboot rather than relying on autoload-on-open.
    sudo modprobe uinput 2>/dev/null || true
    echo uinput | sudo tee /etc/modules-load.d/whispr-uinput.conf >/dev/null
    sudo udevadm trigger /dev/uinput 2>/dev/null || true
fi

if [ ! -w /dev/uinput ]; then
    warn "/dev/uinput is not writable -- text will land on the clipboard instead."
    warn "  A re-login usually applies the new rule."
fi

# ------------------------------------------------------------------- services
say "Enabling user services"
systemctl --user daemon-reload
command -v ydotoold >/dev/null && systemctl --user enable --now ydotoold.service || true
systemctl --user enable --now whispr.service || true

# The daemon inherits its environment from the systemd user manager. Without
# WAYLAND_DISPLAY it starts fine and then silently fails to inject, which is a
# miserable thing to debug -- so check and say so now.
if ! systemctl --user show-environment | grep -qE '^(WAYLAND_DISPLAY|DISPLAY)='; then
    warn "WAYLAND_DISPLAY/DISPLAY missing from the systemd user environment."
    warn "Injection will silently do nothing. Fix with:"
    warn "  systemctl --user import-environment WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR"
fi

cat <<'DONE'

==> Installed.

Next:
  1. Map two unused keys to F13 and F14 in VIA/QMK, verify with: wev
  2. cp wm/hyprland/whispr.lua ~/.config/hypr/config/whispr.lua
     and add  require("config.whispr")  to ~/.config/hypr/hyprland.lua
  3. Hold F13, speak, release.

  whispr status        check state
  journalctl --user -u whispr -f    watch it work
DONE
