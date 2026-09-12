#!/usr/bin/env bash
# Operator-driven acceptance test.
#
# Runs the daemon with --no-inject on the real socket, so the hotkeys drive it
# but transcripts print to a log instead of being typed into whatever window has
# focus. Injection is checked separately at the end, deliberately last.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG=/tmp/whispr-accept.log
RESULTS=/tmp/whispr-accept-results.txt
PASS=0; FAIL=0

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
note() { printf '  %s\n' "$*"; }

transcript() {
    grep -vE '^whisper_|^ggml_|^register_|^whispr:|^load_backend|^build:|^main:|^error:|^silero|^vad|^$' "$LOG" \
        | tail -1 | sed 's/^ *//;s/ *$//'
}

cleanup() {
    [ -n "${DP:-}" ] && kill -TERM "$DP" 2>/dev/null
    sleep 1
    systemctl --user start whispr.service 2>/dev/null
    printf '\n  (normal service restarted)\n'
}
trap cleanup EXIT INT TERM

cat <<'EOF'

  whispr acceptance test
  ════════════════════════════════════════════════════════════
  Before starting:
    - silence any other audio (video on another machine included;
      the mic hears the room, and Whisper will faithfully
      transcribe anything audible in it)
    - stay in this terminal; nothing will be typed into it

  Keys:  circle = hold to talk    triangle = toggle    X = cancel
  ════════════════════════════════════════════════════════════
EOF
read -rp "  Enter when ready... " _

systemctl --user stop whispr.service 2>/dev/null
sleep 1
: > "$LOG"
{ printf 'whispr acceptance run %s\n\n' "$(date '+%Y-%m-%d %H:%M:%S')"; } > "$RESULTS"
"$ROOT/build/whispr" daemon --no-inject -v > "$LOG" 2>&1 &
DP=$!
sleep 2
kill -0 "$DP" 2>/dev/null || { echo "  daemon failed to start; see $LOG"; exit 1; }

step() { # step <title> <instruction> <expectation: EMPTY|TEXT> [expected text]
    local title="$1" instr="$2" want="$3" expect="${4:-}"
    say "$title"
    note "$instr"
    read -rp "  Enter when you have finished... " _
    sleep 4
    local got; got="$(transcript)"
    note ""
    note "got     : \"$got\""
    [ -n "$expect" ] && note "expected: \"$expect\""
    local verdict
    if [ "$want" = EMPTY ]; then
        if [ -z "$got" ]; then verdict="PASS -- nothing was produced"; PASS=$((PASS+1))
        else verdict="FAIL -- produced text from no speech"; FAIL=$((FAIL+1)); fi
    else
        if [ -n "$got" ]; then verdict="PASS -- speech was transcribed"; PASS=$((PASS+1))
        else verdict="FAIL -- speech produced nothing"; FAIL=$((FAIL+1)); fi
    fi
    note "$verdict"

    # The per-step log is truncated so each step reads only its own output, so
    # the transcripts have to be preserved here or the whole run is lost to the
    # scrollback the moment the terminal is closed.
    printf '%s\n  got     : "%s"\n  expected: "%s"\n  %s\n\n' \
        "$title" "$got" "$expect" "$verdict" >> "$RESULTS"
    : > "$LOG"
}

step "1/5  Silence" \
     "HOLD circle for ~3 seconds. Say NOTHING. Release." EMPTY

step "2/5  Speech, push-to-talk" \
     "HOLD circle, say: \"the quick brown fox jumps over the lazy dog\", release." \
     TEXT "the quick brown fox jumps over the lazy dog"

step "3/5  First word not clipped" \
     "HOLD circle and start speaking IMMEDIATELY: \"alpha bravo charlie delta\". Release." \
     TEXT "alpha bravo charlie delta"

step "4/5  Toggle mode" \
     "TAP triangle, say \"testing one two three\", TAP triangle again." \
     TEXT "testing one two three"

step "5/5  Cancel" \
     "HOLD circle, say \"this should be discarded\", tap X, then release circle." EMPTY

say "── result ──────────────────────────────────────────────"
note "$PASS passed, $FAIL failed"
printf '%s passed, %s failed\n' "$PASS" "$FAIL" >> "$RESULTS"
note "full results saved to $RESULTS"
note ""
note "Step 3 is the one to watch: if the first word is missing, PipeWire"
note "source resume plus skip_start_ms is eating it, and that is fixable."
note ""
note "If all five pass, run this for the real injection check:"
note "    whispr toggle   (in a scratch editor, speak, toggle again)"
