#!/usr/bin/env bash
# Transcribe each fixture and assert WER is under its threshold.
#
# Thresholds are a ratchet: they may only ever be tightened. A loose threshold
# that passes today still catches a regression tomorrow; a threshold loosened to
# make a failing run go green catches nothing ever again.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIX="$ROOT/tests/fixtures"
# Prefer the project build: it is the one configured the way the daemon is
# (Vulkan where available), so the suite measures the real backend.
CLI="${WHISPER_CLI:-$ROOT/build/bin/whisper-cli}"
[ -x "$CLI" ] || CLI="$ROOT/vendor/whisper.cpp/build/bin/whisper-cli"
MODEL="${WHISPER_MODEL:-$HOME/.local/share/whispr/models/ggml-base.en.bin}"
# The vocabulary spoken in the fixtures. whispr itself ships NO default prompt
# -- priming only helps for words the speaker actually says, so shipping one
# person's vocabulary would bias every other user's decoder. This list is the
# equivalent of a user filling in whisper.initial_prompt for their own jargon,
# and it is what the thresholds below were set against: it takes jargon WER
# from 0.42 to 0.16 on base.en. Set WHISPER_PROMPT="" for the unprimed baseline.
PROMPT="${WHISPER_PROMPT-CachyOS, Hyprland, PipeWire, ydotool, whisper.cpp, Vulkan, systemd, Keychron}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# fixture:max_wer  -- silence is special-cased (any output at all is a failure)
THRESHOLDS="short:0.35 plain:0.15 jargon:0.20"

# whisper-cli is an upstream example target, and install.sh only builds the
# whispr binary -- so on a fresh install this is simply not there yet. Build it
# rather than failing with an error that reads like something is broken.
if [ ! -x "$CLI" ] && [ -d "$ROOT/build" ]; then
    echo "building whisper-cli (one-off)..." >&2
    cmake --build "$ROOT/build" -j"$(nproc 2>/dev/null || echo 4)" --target whisper-cli >/dev/null 2>&1 || true
    CLI="${WHISPER_CLI:-$ROOT/build/bin/whisper-cli}"
fi

[ -x "$CLI" ]    || { echo "missing whisper-cli at $CLI -- run scripts/install.sh first" >&2; exit 1; }
[ -f "$MODEL" ]  || { echo "missing model at $MODEL -- run scripts/install.sh first" >&2; exit 1; }

# whisper.cpp reports non-speech as bracketed markers -- [BLANK_AUDIO], [Music],
# (silence). Those are status, not words, and the daemon must strip them or they
# get typed into the focused window. Strip here too so tests match the daemon.
strip_markers() { sed -E 's/\[[^]]*\]//g; s/\([^)]*\)//g' | tr -s '[:space:]' ' ' | sed 's/^ *//;s/ *$//'; }

transcribe() { # transcribe <wav> -> text on stdout
    local args=(-m "$MODEL" -f "$1" -nt -np --output-txt --output-file "$OUT/out")
    [ -n "$PROMPT" ] && args+=(--prompt "$PROMPT")
    "$CLI" "${args[@]}" >/dev/null 2>&1
    cat "$OUT/out.txt" 2>/dev/null | strip_markers
}

pass=0 fail=0
printf "%-9s %-8s %-8s %s\n" FIXTURE WER LIMIT RESULT
printf -- "----------------------------------------------------------------\n"

for entry in $THRESHOLDS; do
    name="${entry%%:*}"; limit="${entry##*:}"
    wav="$FIX/$name.wav"
    [ -f "$wav" ] || { echo "$name: MISSING FIXTURE"; fail=$((fail+1)); continue; }
    hyp="$(transcribe "$wav")"
    printf '%s' "$hyp" > "$OUT/$name.hyp"
    read -r rate rest <<<"$(python3 "$ROOT/tests/wer.py" "$FIX/$name.txt" "$OUT/$name.hyp")"
    if awk "BEGIN{exit !($rate <= $limit)}"; then
        printf "%-9s %-8s %-8s PASS\n" "$name" "$rate" "$limit"; pass=$((pass+1))
    else
        printf "%-9s %-8s %-8s FAIL\n" "$name" "$rate" "$limit"; fail=$((fail+1))
        echo "    ref: $(cat "$FIX/$name.txt")"
        echo "    hyp: $hyp"
        echo "    $rest"
    fi
done

# Silence: the model must not invent words. Whisper is notorious for emitting
# "Thank you." or subtitle credits over room tone, which in this tool would be
# typed straight into whatever window is focused.
if [ -f "$FIX/silence.wav" ]; then
    hyp="$(transcribe "$FIX/silence.wav")"
    clean="$(printf '%s' "$hyp" | tr -d '[:space:][:punct:]')"
    if [ -z "$clean" ]; then
        printf "%-9s %-8s %-8s PASS\n" silence "-" "empty"; pass=$((pass+1))
    else
        printf "%-9s %-8s %-8s FAIL\n" silence "-" "empty"; fail=$((fail+1))
        echo "    hallucinated: \"$hyp\""
    fi
fi

printf -- "----------------------------------------------------------------\n"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
