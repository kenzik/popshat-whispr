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
# Ship the same vocabulary prompt the daemon uses, so the suite measures what
# production actually does. Empirically this took jargon WER 0.42 -> 0.16 on
# base.en. Set WHISPER_PROMPT="" to measure the unprimed baseline.
PROMPT="${WHISPER_PROMPT-CachyOS, Hyprland, PipeWire, ydotool, whisper.cpp, Vulkan, systemd, Keychron}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# fixture:max_wer  -- silence is special-cased (any output at all is a failure)
THRESHOLDS="short:0.35 plain:0.15 jargon:0.20"

[ -x "$CLI" ]    || { echo "missing whisper-cli at $CLI" >&2; exit 1; }
[ -f "$MODEL" ]  || { echo "missing model at $MODEL" >&2; exit 1; }

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
