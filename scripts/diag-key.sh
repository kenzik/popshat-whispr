#!/usr/bin/env bash
# Why isn't a bound key firing?
#
# Watches two things at once while you press the key:
#   wev     -- did the keystroke reach a client?
#   status  -- did the daemon change state?
#
# Hyprland consumes a key it has a bind for, so the combination is decisive:
#   wev sees it + no state change  -> the bind is NOT matching this key
#   wev silent  + no state change  -> bind matched, the exec failed
#   wev silent  + state change     -> working
#
# HYPRLAND ONLY. The whole trick depends on Hyprland consuming a key it has a
# bind for; a compositor that forwards the key regardless makes the two cases
# indistinguishable. Elsewhere, answer the same question by hand: if
# "whispr toggle" in a terminal works but the key does nothing, the bind is the
# problem -- and it is nearly always PATH or the keysym name.
set -uo pipefail

WLOG=/tmp/whispr-diag-wev.log
SLOG=/tmp/whispr-diag-state.log
SECS="${1:-20}"
WHISPR="$HOME/.local/bin/whispr"

command -v hyprctl >/dev/null 2>&1 || {
    echo "diag-key.sh only works under Hyprland." >&2
    echo "Elsewhere: run 'whispr toggle' in a terminal. If that works, the" >&2
    echo "daemon is fine and the binding is the problem -- check the keysym" >&2
    echo "with scripts/probe-keys.sh and use an absolute path in the bind." >&2
    exit 1
}

: > "$WLOG"; : > "$SLOG"

cat <<EOF

  whispr key diagnostic
  ────────────────────────────────────────────────────────────
  A window opens and takes focus. For the next ${SECS}s:

      HOLD your push-to-talk key for ~2 seconds, release.
      Do that twice.

  Do not click away.
  ────────────────────────────────────────────────────────────

EOF
read -rp "  Press Enter to begin... " _

stdbuf -oL wev -f wl_keyboard:key > "$WLOG" 2>&1 &
WP=$!
sleep 2
if command -v jq >/dev/null 2>&1; then
    ADDR=$(hyprctl clients -j 2>/dev/null | jq -r '.[] | select(.class|test("wev";"i")) | .address' | head -1)
    [ -n "${ADDR:-}" ] && hyprctl dispatch focuswindow "address:$ADDR" >/dev/null 2>&1
fi

echo "  watching..."
END=$((SECONDS + SECS))
while [ $SECONDS -lt $END ]; do
    printf '%s %s\n' "$SECONDS" "$(timeout 2 "$WHISPR" status 2>&1 | tr -d '\n')" >> "$SLOG"
    sleep 0.3
done
kill -INT $WP 2>/dev/null; sleep 0.5; kill -9 $WP 2>/dev/null

echo
echo "  ── did the keystroke reach a client? ────────────────────"
if grep -q 'sym:' "$WLOG"; then
    grep -oP 'sym: \K[^ ]+' "$WLOG" | sort | uniq -c | sed 's/^/    /'
    SAW=yes
else
    echo "    nothing -- Hyprland consumed it (or the window lost focus)"
    SAW=no
fi

echo
echo "  ── did the daemon change state? ─────────────────────────"
if grep -qvE 'idle|daemon not running' "$SLOG"; then
    grep -vE ' idle' "$SLOG" | head -8 | sed 's/^/    t=/'
    CHANGED=yes
else
    echo "    never left idle"
    CHANGED=no
fi

echo
echo "  ── verdict ──────────────────────────────────────────────"
if [ "$CHANGED" = yes ]; then
    echo "    WORKING -- the key reached the daemon."
elif [ "$SAW" = yes ]; then
    echo "    The bind is NOT matching this key: Hyprland passed it"
    echo "    through to the client instead of consuming it."
    echo "    -> the keysym above is not what we bound."
else
    echo "    Hyprland consumed the key but the daemon never saw it:"
    echo "    the bind matched and the exec failed."
    echo "    -> check the command path in your Hyprland config;"
    echo "       binds need an absolute path to whispr."
fi
echo
echo "  logs: $WLOG  $SLOG"
