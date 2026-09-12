#!/usr/bin/env bash
# Build and install whispr.
#
# Root is needed for exactly one thing (the udev rule for /dev/uinput) and is
# asked for explicitly at that point, never up front. Everything else lands
# under $PREFIX (default ~/.local), so an ordinary user can install and remove
# whispr without touching the system.
#
# Distribution-agnostic by construction: the daemon needs a POSIX system, a
# capture program (PipeWire, PulseAudio or ALSA -- whichever you have) and a way
# to type into a window. Package names differ, so this script detects the distro
# family and prints the right command rather than assuming pacman.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
MODELDIR="${WHISPR_MODEL_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/whispr/models}"
CONFDIR="${XDG_CONFIG_HOME:-$HOME/.config}/whispr"
UNITDIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"

# Sizes are checked after download. A truncated or rate-limited fetch otherwise
# surfaces much later as an unhelpful "failed to load model".
WHISPER_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin"
WHISPER_BIN="ggml-base.en.bin"
WHISPER_SIZE=147964211

# Not optional in practice. The default config enables VAD, and whisper.cpp
# fails the whole transcription if the VAD model is missing -- so an install
# without this file produces an error on every single utterance.
VAD_URL="https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin"
VAD_BIN="ggml-silero-v5.1.2.bin"
VAD_SIZE=885098

# Opt-in: 1.2 GB, and its weights are CC-BY-4.0 rather than MIT. See THIRD-PARTY.md.
PARAKEET_URL="https://huggingface.co/ggml-org/parakeet-GGUF/resolve/main/ggml-parakeet-tdt-0.6b-v3-f16.bin"
PARAKEET_BIN="ggml-parakeet-tdt-0.6b-v3-f16.bin"
PARAKEET_SIZE=1255897319

WITH_PARAKEET=0
WANT_GPU=1
SKIP_MODELS=0
SKIP_SERVICE=0

say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33m==> %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m==> %s\033[0m\n' "$*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
usage: scripts/install.sh [options]

  --with-parakeet   also fetch the Parakeet model (1.2 GB, CC-BY-4.0 weights)
  --no-gpu          skip the Vulkan probe and build for CPU
  --skip-models     do not download anything; assume the models are in place
  --no-service      install the files but do not touch systemd
  --prefix DIR      install under DIR instead of ~/.local
  -h, --help        this

Environment: PREFIX, WHISPR_MODEL_DIR, XDG_CONFIG_HOME, XDG_DATA_HOME.
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --with-parakeet) WITH_PARAKEET=1 ;;
        --no-gpu)        WANT_GPU=0 ;;
        --skip-models)   SKIP_MODELS=1 ;;
        --no-service)    SKIP_SERVICE=1 ;;
        --prefix)        PREFIX="${2:?--prefix needs a directory}"; shift ;;
        -h|--help)       usage; exit 0 ;;
        *)               usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

# ------------------------------------------------------------ distro families
# Package names are the only genuinely distro-specific thing here. ID_LIKE
# catches derivatives (CachyOS/EndeavourOS -> arch, Mint/Pop -> debian) without
# enumerating them.
family() {
    local id="" like=""
    [ -r /etc/os-release ] && . /etc/os-release && id="${ID:-}" && like="${ID_LIKE:-}"
    case " $id $like " in
        *" arch "*|*cachyos*|*manjaro*)  echo arch ;;
        *" debian "*|*" ubuntu "*)       echo debian ;;
        *" fedora "*|*" rhel "*|*centos*) echo fedora ;;
        *suse*)                          echo suse ;;
        *" alpine "*)                    echo alpine ;;
        *" void "*)                      echo void ;;
        *" gentoo "*)                    echo gentoo ;;
        *" nixos "*)                     echo nixos ;;
        *)                               echo unknown ;;
    esac
}
FAMILY="$(family)"

# pkg_hint <build|vulkan|inject> -- a copy-pasteable install command, or "" if
# we have nothing trustworthy to suggest for this distro.
#
# Only the Arch line is verified on a running system; the rest come from each
# distribution's own package index. Wrong package names are a papercut, and
# claiming certainty we do not have would be worse -- so unknown distros get
# the dependency list in plain words instead of a command that may not work.
pkg_hint() {
    case "$FAMILY:$1" in
        arch:build)    echo "sudo pacman -S --needed base-devel cmake git curl" ;;
        arch:vulkan)   echo "sudo pacman -S --needed vulkan-headers spirv-headers glslang shaderc" ;;
        arch:inject)   echo "sudo pacman -S --needed ydotool libnotify" ;;

        debian:build)  echo "sudo apt install build-essential cmake git curl" ;;
        debian:vulkan) echo "sudo apt install libvulkan-dev spirv-headers glslang-tools libshaderc-dev" ;;
        debian:inject) echo "sudo apt install ydotool libnotify-bin" ;;

        fedora:build)  echo "sudo dnf install gcc gcc-c++ cmake git curl" ;;
        fedora:vulkan) echo "sudo dnf install vulkan-headers spirv-headers-devel glslang-devel libshaderc-devel" ;;
        fedora:inject) echo "sudo dnf install ydotool libnotify" ;;

        suse:build)    echo "sudo zypper install gcc gcc-c++ cmake git curl" ;;
        suse:vulkan)   echo "sudo zypper install vulkan-devel spirv-headers glslang-devel shaderc-devel" ;;
        suse:inject)   echo "sudo zypper install ydotool libnotify-tools" ;;

        alpine:build)  echo "sudo apk add build-base cmake git curl" ;;
        alpine:vulkan) echo "sudo apk add vulkan-headers spirv-headers glslang-dev shaderc-dev" ;;
        alpine:inject) echo "sudo apk add wtype xdotool libnotify   # no ydotool in Alpine" ;;

        void:build)    echo "sudo xbps-install -S base-devel cmake git curl" ;;
        void:vulkan)   echo "sudo xbps-install -S Vulkan-Headers SPIRV-Headers glslang-devel shaderc" ;;
        void:inject)   echo "sudo xbps-install -S wtype xdotool libnotify   # no ydotool in Void" ;;

        *) echo "" ;;
    esac
}

need() { # need <group> <plain-english fallback>
    local hint; hint="$(pkg_hint "$1")"
    if [ -n "$hint" ]; then echo "  $hint"
    else echo "  install: $2"; fi
}

# ---------------------------------------------------------------- dependencies
say "Checking dependencies (detected: ${FAMILY})"

missing=()
for c in cmake git; do command -v "$c" >/dev/null || missing+=("$c"); done
command -v cc >/dev/null || command -v gcc >/dev/null || command -v clang >/dev/null || missing+=(c-compiler)
command -v c++ >/dev/null || command -v g++ >/dev/null || command -v clang++ >/dev/null || missing+=(c++-compiler)
command -v curl >/dev/null || command -v wget >/dev/null || missing+=(curl-or-wget)

if [ ${#missing[@]} -gt 0 ]; then
    warn "Missing build tools: ${missing[*]}"
    need build "a C and C++ compiler, cmake, git, curl"
    exit 1
fi

# ggml needs 3.14; this project asks for 3.16. Debian oldstable and Ubuntu LTS
# have shipped older, and the failure mode is an obscure configure error.
cmake_ver="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
if [ "$(printf '%s\n3.16\n' "$cmake_ver" | sort -V | head -1)" != "3.16" ]; then
    die "cmake $cmake_ver is too old; 3.16 or newer is required."
fi

# Capture: any one of the three is enough. audio.c tries them in this order.
if   command -v pw-record >/dev/null; then CAPTURE=pw-record
elif command -v parecord  >/dev/null; then CAPTURE=parecord
elif command -v arecord   >/dev/null; then CAPTURE=arecord
else
    warn "No capture program found (pw-record, parecord or arecord)."
    warn "  Install PipeWire, PulseAudio or alsa-utils -- whichever your system uses."
    exit 1
fi
say "Capture: $CAPTURE"

# Injection backends, in the order whispr-inject tries them. ydotool is the
# best of them (it types into anything and needs no compositor protocol), but
# it is not packaged everywhere -- Alpine and Void have neither. wtype and
# xdotool are perfectly good substitutes and need no daemon at all.
if   command -v ydotool >/dev/null; then INJECT=ydotool
elif command -v wtype   >/dev/null; then INJECT=wtype
elif command -v xdotool >/dev/null; then INJECT=xdotool
else INJECT=""
fi

if [ -n "$INJECT" ]; then
    say "Injection: $INJECT"
else
    warn "No way to type into a window -- transcripts will land on the clipboard."
    need inject "ydotool, or wtype (Wayland) / xdotool (X11)"
fi

# ---------------------------------------------------------------------- build
say "Fetching whisper.cpp submodule"
git -C "$ROOT" submodule update --init --recursive

# Probe for Vulkan by attempting the configure rather than sniffing for a
# header. ggml's Vulkan backend needs vulkan-headers, spirv-headers, glslang and
# shaderc, and checking for one of them only moves the failure later -- as a
# vulkan.h check did, which passed and then died on missing SPIRV-Headers.
# GPU support is an optimisation, so failing to get it must never fail install.
if [ "$WANT_GPU" = 1 ]; then
    CFGLOG="$(mktemp)"
    say "Configuring with Vulkan"
    if cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_VULKAN=ON >"$CFGLOG" 2>&1; then
        say "Vulkan enabled"
    else
        warn "Vulkan unavailable -- falling back to CPU."
        if grep -q 'SPIRV-Headers' "$CFGLOG"; then
            warn "  Missing SPIRV-Headers."
        else
            warn "  Missing one of: vulkan headers, spirv-headers, glslang, shaderc"
            warn "  Details: $CFGLOG"
        fi
        need vulkan "the Vulkan and SPIR-V development headers, glslang and shaderc"
        warn "  CPU is fully functional -- install those and re-run for GPU."
        rm -rf "$ROOT/build"
        cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_VULKAN=OFF
    fi
    rm -f "$CFGLOG"
else
    say "Configuring for CPU (--no-gpu)"
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_VULKAN=OFF
fi

# Static: the binary is copied to $PREFIX/bin and must keep working after
# this clone is deleted. A shared build leaves a RUNPATH pointing here.
say "Building"
cmake --build "$ROOT/build" -j"$(nproc 2>/dev/null || echo 4)" --target whispr

# --------------------------------------------------------------------- models
fetch() { # fetch <url> <dest> <size> <label>
    local url="$1" dest="$2" size="$3" label="$4" got
    if [ "$(stat -c%s "$dest" 2>/dev/null || echo 0)" = "$size" ]; then
        say "$label already present"
        return 0
    fi
    say "Fetching $label"
    if command -v curl >/dev/null; then
        curl -L --fail --progress-bar -o "$dest.part" "$url"
    else
        wget -O "$dest.part" "$url"
    fi
    got="$(stat -c%s "$dest.part")"
    # Verify before moving into place. A truncated download is indistinguishable
    # from a corrupt model at load time, and that error says nothing useful.
    [ "$got" = "$size" ] || { rm -f "$dest.part"; die "$label is $got bytes, expected $size"; }
    mv "$dest.part" "$dest"
}

if [ "$SKIP_MODELS" = 0 ]; then
    mkdir -p "$MODELDIR"
    fetch "$WHISPER_URL" "$MODELDIR/$WHISPER_BIN" "$WHISPER_SIZE" "Whisper base.en (141 MB)"
    # Required, not optional: with whisper.vad = true and this file absent,
    # whisper_full fails and every utterance reports an error.
    fetch "$VAD_URL"     "$MODELDIR/$VAD_BIN"     "$VAD_SIZE"     "Silero VAD (865 KB)"
    if [ "$WITH_PARAKEET" = 1 ]; then
        fetch "$PARAKEET_URL" "$MODELDIR/$PARAKEET_BIN" "$PARAKEET_SIZE" "Parakeet tdt-0.6b-v3 (1.2 GB)"
        warn "Parakeet weights are CC-BY-4.0, not MIT. See THIRD-PARTY.md."
    fi
fi

# -------------------------------------------------------------------- install
say "Installing to $PREFIX/bin"
install -Dm755 "$ROOT/build/whispr"            "$PREFIX/bin/whispr"
install -Dm755 "$ROOT/scripts/whispr-inject"   "$PREFIX/bin/whispr-inject"
install -Dm755 "$ROOT/scripts/whispr-notify"   "$PREFIX/bin/whispr-notify"
install -Dm755 "$ROOT/scripts/whispr-doctor"   "$PREFIX/bin/whispr-doctor"

mkdir -p "$CONFDIR"
if [ ! -f "$CONFDIR/config" ]; then
    install -Dm644 "$ROOT/examples/config" "$CONFDIR/config"
    say "Wrote $CONFDIR/config"
else
    say "Keeping existing $CONFDIR/config"
fi

# ------------------------------------------------------------------ udev rule
# /dev/uinput is often already user-accessible, but via a rule shipped by some
# other package (Steam, for one). Depending on that means text injection breaks
# the day it is uninstalled, so whispr declares its own.
UDEV_RULE='KERNEL=="uinput", SUBSYSTEM=="misc", TAG+="uaccess", OPTIONS+="static_node=uinput"'
if [ ! -f /etc/udev/rules.d/99-whispr-uinput.rules ]; then
    if command -v sudo >/dev/null; then
        say "Installing udev rule for /dev/uinput (needs sudo)"
        echo "$UDEV_RULE" | sudo tee /etc/udev/rules.d/99-whispr-uinput.rules >/dev/null
        sudo udevadm control --reload-rules
        # The node exists via static_node= even with the module unloaded, so a
        # trigger before first open reports ENODEV ("No such device") and looks
        # like a failure when nothing is wrong. Load it explicitly instead, and
        # make it survive reboot rather than relying on autoload-on-open.
        sudo modprobe uinput 2>/dev/null || true
        if [ -d /etc/modules-load.d ]; then
            echo uinput | sudo tee /etc/modules-load.d/whispr-uinput.conf >/dev/null
        fi
        sudo udevadm trigger /dev/uinput 2>/dev/null || true
    else
        warn "No sudo. To enable typing, run as root:"
        warn "  echo '$UDEV_RULE' > /etc/udev/rules.d/99-whispr-uinput.rules"
        warn "  udevadm control --reload-rules && modprobe uinput"
    fi
fi

if [ ! -w /dev/uinput ]; then
    warn "/dev/uinput is not writable -- text will land on the clipboard instead."
    warn "  A re-login usually applies the new rule."
fi

# ------------------------------------------------------------------- services
# systemd is the common case, not a requirement. On a system without it the
# daemon is just a long-running process: start it from the compositor's autostart.
if [ "$SKIP_SERVICE" = 0 ] && command -v systemctl >/dev/null && [ -d /run/systemd/system ]; then
    say "Enabling user services"
    install -Dm644 "$ROOT/systemd/whispr.service"   "$UNITDIR/whispr.service"
    install -Dm644 "$ROOT/systemd/ydotoold.service" "$UNITDIR/ydotoold.service"
    systemctl --user daemon-reload
    command -v ydotoold >/dev/null && systemctl --user enable --now ydotoold.service || true
    systemctl --user enable --now whispr.service || true

    # The daemon inherits its environment from the systemd user manager. Without
    # WAYLAND_DISPLAY it starts fine and then silently fails to inject, which is
    # a miserable thing to debug -- so check and say so now.
    if ! systemctl --user show-environment | grep -qE '^(WAYLAND_DISPLAY|DISPLAY)='; then
        warn "WAYLAND_DISPLAY/DISPLAY missing from the systemd user environment."
        warn "Injection will silently do nothing. Fix with:"
        warn "  systemctl --user import-environment WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR"
    fi
else
    say "No systemd user manager (or --no-service): skipping units"
    say "Start these from your compositor's autostart instead:"
    say "  ydotoold --socket-path=\$XDG_RUNTIME_DIR/.ydotool_socket --socket-own=\$(id -u):\$(id -g) &"
    say "  $PREFIX/bin/whispr daemon &"
fi

case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) warn "$PREFIX/bin is not on your PATH. Keybinds should use the absolute path." ;;
esac

cat <<DONE

==> Installed.

Next:
  1. Find a key to bind -- one that sends no modifier:
         $ROOT/scripts/probe-keys.sh
  2. Add the binding for your window manager: see $ROOT/wm/README.md
  3. Hold the key, speak, release.

  whispr status                     check state
  whispr-doctor                     environment report (attach this to bug reports)
  journalctl --user -u whispr -f    watch it work
DONE
