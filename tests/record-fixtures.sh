#!/usr/bin/env bash
# Record voice fixtures for the whispr test suite.
# Press Enter to start a take, speak, press Enter to stop. Redo any take you dislike.
set -uo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fixtures"
mkdir -p "$DIR"
RATE=16000

# Decide the recorder ONCE, as an argv array. It must be exec'd directly in the
# background so that $! is the recorder's own PID -- wrapping it in a shell
# function makes $! the subshell's PID, and the kill then leaves the recorder
# orphaned and still writing. That bug produced 200-second fixtures.
if command -v pw-record >/dev/null 2>&1; then
    REC=(pw-record --rate="$RATE" --channels=1 --format=s16)
elif command -v parecord >/dev/null 2>&1; then
    REC=(parecord --rate="$RATE" --channels=1 --format=s16le --file-format=wav)
else
    echo "no pw-record or parecord found" >&2; exit 1
fi

REC_PID=""
cleanup() { [ -n "$REC_PID" ] && kill -KILL "$REC_PID" 2>/dev/null; }
trap cleanup EXIT INT TERM

stop_rec() { # stop_rec <pid> -- stop and WAIT for the file to be finalized
    local pid="$1" i
    kill -INT "$pid" 2>/dev/null
    for i in $(seq 1 20); do kill -0 "$pid" 2>/dev/null || { REC_PID=""; return 0; }; sleep 0.1; done
    kill -TERM "$pid" 2>/dev/null; sleep 0.5
    kill -KILL "$pid" 2>/dev/null
    REC_PID=""
}

duration() { ffprobe -v error -show_entries format=duration -of csv=p=0 "$1" 2>/dev/null; }
peak()     { ffmpeg -i "$1" -af volumedetect -f null - 2>&1 | awk '/max_volume/{print $5}'; }

take() { # take <name> <sentence>
    local name="$1" text="$2" wav="$DIR/$1.wav" dur pk ans
    while :; do
        echo
        echo "──────────────────────────────────────────────────────────────"
        echo "  Fixture: $name"
        echo
        if [ -n "$text" ]; then
            echo "  Read this aloud at your normal dictation pace:"
            echo
            echo "      \"$text\""
        else
            echo "  Say NOTHING -- about 3 seconds of room tone. This checks that"
            echo "  an accidental key tap doesn't inject hallucinated text."
        fi
        echo
        read -rp "  Press Enter to START... " _
        rm -f "$wav"
        "${REC[@]}" "$wav" &
        REC_PID=$!
        sleep 0.4
        echo "  RECORDING -- press Enter to STOP."
        read -r _
        stop_rec "$REC_PID"

        dur=$(duration "$wav"); pk=$(peak "$wav")
        printf "  saved %s  duration=%.1fs  peak=%s\n" "$name.wav" "${dur:-0}" "${pk:-?}"

        # Sanity gates -- catch a broken take here, not three phases later.
        if [ -z "$dur" ] || awk "BEGIN{exit !(${dur:-0} > 60)}"; then
            echo "  !! Take is >60s or unreadable -- the recorder did not stop. Redoing."
            continue
        fi
        if [ -n "$text" ] && awk "BEGIN{exit !(${dur:-0} < 0.6)}"; then
            echo "  !! Take is under 0.6s -- too short to contain the sentence. Redoing."
            continue
        fi
        case "${pk:-}" in
            0.0*|-0.[0-9]*) echo "  !! Peak is at/near 0 dBFS -- clipping. Back off the mic or lower gain." ;;
        esac

        read -rp "  [Enter] keep  /  [r] redo  /  [p] play back : " ans
        case "$ans" in
            r|R) continue ;;
            p|P) pw-play "$wav" 2>/dev/null || paplay "$wav" 2>/dev/null
                 read -rp "  [Enter] keep  /  [r] redo : " ans
                 [ "$ans" = "r" ] || [ "$ans" = "R" ] && continue ;;
        esac
        printf '%s' "$text" > "$DIR/$name.txt"
        break
    done
}

echo
echo "whispr -- recording test fixtures"
echo "Input: $(pactl get-default-source 2>/dev/null || echo default)"

take short   "Commit and push."
take plain   "Please open the configuration file and increase the buffer size to sixty four kilobytes, then restart the service and check the logs."
take jargon  "I'm running Hyprland on CachyOS with PipeWire and ydotool, and the whisper dot cpp build uses the Vulkan backend."
take silence ""

echo
echo "──────────────────────────────────────────────────────────────"
echo "Done."
for w in "$DIR"/*.wav; do printf "  %-12s %5.1fs  peak %s\n" "$(basename "$w")" "$(duration "$w")" "$(peak "$w")"; done
echo
echo "No recorders should remain:"; pgrep -af 'pw-record|parecord' | grep -v grep || echo "  clean."
