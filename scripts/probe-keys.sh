#!/usr/bin/env bash
# Identify what a physical key actually sends.
#
# Reports the keycode alongside the keysym because the keysym is not always
# bindable as its obvious name: evdev KEY_F13 arrives as sym XF86Tools, so
# binding "F13" silently never fires. Hyprland's code:NNN form sidesteps the
# naming entirely and is what this prints as the recommended bind.
set -uo pipefail

LOG=/tmp/whispr-keyprobe.log
SECS="${1:-30}"

command -v wev >/dev/null || { echo "wev not installed: sudo pacman -S wev" >&2; exit 1; }

cat <<EOF

  whispr key probe
  ────────────────────────────────────────────────────────────
  A window opens and is focused automatically. Press these one
  at a time, pausing ~1s between each:

      1. circle      (above numpad)
      2. triangle
      3. square
      4. X
      5. the dead third key next to the volume knob

  Closes by itself after ${SECS}s. Do not click away.
  ────────────────────────────────────────────────────────────

EOF
read -rp "  Press Enter to begin... " _

: > "$LOG"
# Line-buffered: with stdout redirected, glibc block-buffers and a killed wev
# takes its entire capture with it.
stdbuf -oL wev -f wl_keyboard:key > "$LOG" 2>&1 &
WP=$!
sleep 2

# wev emits nothing until its window holds keyboard focus, so an unfocused
# window looks exactly like a key that sends nothing. Focus it explicitly.
if command -v hyprctl >/dev/null 2>&1 && command -v jq >/dev/null 2>&1; then
    ADDR=$(hyprctl clients -j 2>/dev/null | jq -r '.[] | select(.class|test("wev";"i")) | .address' | head -1)
    [ -n "${ADDR:-}" ] && hyprctl dispatch focuswindow "address:$ADDR" >/dev/null 2>&1
fi

echo "  recording keys now..."
SLEPT=2
while [ $SLEPT -lt "$SECS" ] && kill -0 $WP 2>/dev/null; do sleep 1; SLEPT=$((SLEPT+1)); done
kill -INT $WP 2>/dev/null; sleep 0.5; kill -9 $WP 2>/dev/null

echo
echo "  ── results ─────────────────────────────────────────────"

mapfile -t rows < <(awk '
  /state: 1 \(pressed\)/ {
    code=$0; sub(/.*key: /, "", code); sub(/;.*/, "", code); want=1; next
  }
  want && /sym:/ {
    s=$0; sub(/.*sym: /, "", s); sub(/ *\(.*/, "", s)
    gsub(/^[ \t]+|[ \t]+$/, "", s)
    print s "\t" code; want=0
  }' "$LOG")

if [ ${#rows[@]} -eq 0 ]; then
    echo "  Nothing captured -- these keys emit no HID keycode at all."
    echo "  Only a VIA/QMK remap can give them one."
    echo "  Raw log: $LOG"
    exit 0
fi

printf "  %-3s %-20s %-8s %s\n" "#" "KEYSYM" "CODE" "SUGGESTED BIND"
i=1
for r in "${rows[@]}"; do
    sym="${r%%$'\t'*}"; code="${r##*$'\t'}"
    printf "  %-3s %-20s %-8s code:%s\n" "$i" "$sym" "$code" "$code"
    i=$((i+1))
done

echo
echo "  Raw log: $LOG"
