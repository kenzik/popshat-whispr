#!/usr/bin/env bash
# Find out what a physical key actually sends, so you can bind it.
#
# This exists because the obvious name is often wrong. A keyboard's F13 arrives
# through xkb as the keysym XF86Tools, so a binding written as "F13" silently
# never fires -- no error, no log, nothing to debug. The only reliable answer is
# to press the key and look.
#
# Works on Wayland (via wev) and X11 (via xev). Neither is required to be
# installed in advance; the script says which one it wants.
set -uo pipefail

LOG="${TMPDIR:-/tmp}/whispr-keyprobe.log"
SECS="${1:-20}"

# ------------------------------------------------------------------ prereqs
if [ -n "${WAYLAND_DISPLAY:-}" ] && command -v wev >/dev/null 2>&1; then
    MODE=wayland
elif [ -n "${DISPLAY:-}" ] && command -v xev >/dev/null 2>&1; then
    MODE=x11
else
    echo "Need a key-event viewer." >&2
    if [ -n "${WAYLAND_DISPLAY:-}" ]; then
        echo "  Wayland session: install 'wev'" >&2
    else
        echo "  X11 session: install 'xev' (usually in xorg-xev or x11-utils)" >&2
    fi
    echo >&2
    echo "  Arch:    sudo pacman -S wev        # or xorg-xev" >&2
    echo "  Debian:  sudo apt install wev      # or x11-utils" >&2
    echo "  Fedora:  sudo dnf install wev      # or xorg-x11-utils" >&2
    exit 1
fi

cat <<EOF

  whispr key probe  (${MODE})
  ────────────────────────────────────────────────────────────
  A window opens and takes keyboard focus. Press the keys you
  are thinking of binding, one at a time, about a second apart.

  Pick keys that carry NO modifier. With a modified hotkey you
  release the combo and whispr types while the modifier is
  still held, turning every character into a WM shortcut.

  Good candidates: F13-F24, spare macro keys, media/launch
  keys, a remapped Caps Lock, Menu, Right Ctrl.

  Closes by itself after ${SECS}s. Do not click away.
  ────────────────────────────────────────────────────────────

EOF
read -rp "  Press Enter to begin... " _

: > "$LOG"
# Line-buffered: with stdout redirected, glibc block-buffers and a killed
# viewer takes its entire capture with it.
if [ "$MODE" = wayland ]; then
    stdbuf -oL wev -f wl_keyboard:key > "$LOG" 2>&1 &
else
    stdbuf -oL xev -event keyboard > "$LOG" 2>&1 &
fi
WP=$!
sleep 2

# The viewer emits nothing until its window holds keyboard focus, so an
# unfocused window looks exactly like a key that sends nothing. Focus it
# explicitly where the compositor gives us a way to.
if command -v hyprctl >/dev/null 2>&1 && command -v jq >/dev/null 2>&1; then
    ADDR=$(hyprctl clients -j 2>/dev/null | jq -r '.[] | select(.class|test("wev|xev";"i")) | .address' | head -1)
    [ -n "${ADDR:-}" ] && hyprctl dispatch focuswindow "address:$ADDR" >/dev/null 2>&1
elif command -v swaymsg >/dev/null 2>&1; then
    swaymsg '[app_id="wev"] focus' >/dev/null 2>&1 || true
fi

echo "  recording keys now..."
SLEPT=2
while [ $SLEPT -lt "$SECS" ] && kill -0 $WP 2>/dev/null; do sleep 1; SLEPT=$((SLEPT+1)); done
kill -INT $WP 2>/dev/null; sleep 0.5; kill -9 $WP 2>/dev/null

echo
echo "  ── results ─────────────────────────────────────────────"

if [ "$MODE" = wayland ]; then
    mapfile -t rows < <(awk '
      /state: 1 \(pressed\)/ {
        code=$0; sub(/.*key: /, "", code); sub(/;.*/, "", code); want=1; next
      }
      want && /sym:/ {
        s=$0; sub(/.*sym: /, "", s); sub(/ *\(.*/, "", s)
        gsub(/^[ \t]+|[ \t]+$/, "", s)
        print s "\t" code; want=0
      }' "$LOG" | awk '!seen[$0]++')
else
    # xev: "KeyPress event, ... keycode 191 (keysym 0x1008ff81, XF86Tools)"
    # X11 keycodes are evdev + 8; report the evdev number to match Wayland.
    mapfile -t rows < <(awk '
      /^KeyPress event/ { want=1; next }
      want && /keycode/ {
        code=$0; sub(/.*keycode  */, "", code); sub(/ .*/, "", code)
        s=$0; sub(/.*keysym [^,]*, */, "", s); sub(/\).*/, "", s)
        gsub(/^[ \t]+|[ \t]+$/, "", s)
        print s "\t" (code - 8); want=0
      }' "$LOG" | awk '!seen[$0]++')
fi

if [ ${#rows[@]} -eq 0 ]; then
    echo "  Nothing captured."
    echo
    echo "  Either the window never held focus, or these keys emit no HID"
    echo "  keycode at all -- some vendor keys are handled entirely in"
    echo "  firmware and never reach the host. A QMK/VIA remap is the only"
    echo "  fix for that; try a different key first."
    echo "  Raw log: $LOG"
    exit 0
fi

printf "  %-3s %-22s %s\n" "#" "KEYSYM" "EVDEV CODE"
i=1
for r in "${rows[@]}"; do
    printf "  %-3s %-22s %s\n" "$i" "${r%%$'\t'*}" "${r##*$'\t'}"
    i=$((i+1))
done

FIRST="${rows[0]%%$'\t'*}"

cat <<EOF

  ── bind the first one for push-to-talk ─────────────────────

  Bind by KEYSYM, not keycode. Keycode forms vary by compositor
  and some config parsers store them without ever matching.

  Hyprland (hyprlang):
      bind  = , $FIRST, exec, \$HOME/.local/bin/whispr start
      bindr = , $FIRST, exec, \$HOME/.local/bin/whispr stop

  sway / i3:
      bindsym --no-repeat $FIRST exec \$HOME/.local/bin/whispr start
      bindsym --release   $FIRST exec \$HOME/.local/bin/whispr stop

  sxhkd (X11):
      $FIRST
          \$HOME/.local/bin/whispr start
      @$FIRST
          \$HOME/.local/bin/whispr stop

  Absolute paths on purpose: window managers usually run with the
  login-session PATH, which does not include ~/.local/bin, and a
  bind that cannot find its program fails silently.

  More compositors: wm/README.md
  Raw log: $LOG

EOF
