#!/usr/bin/env python3
"""Word error rate between a reference and a hypothesis. Stdlib only.

Normalisation matters as much as the edit distance here: we are measuring
recognition errors, not formatting choices. "64" vs "sixty four" and "don't"
vs "dont" are the same words spoken, so they must not count against the model.
"""
import re
import sys

_UNITS = {
    "zero": 0, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
    "seventeen": 17, "eighteen": 18, "nineteen": 19,
}
_TENS = {"twenty": 20, "thirty": 30, "forty": 40, "fifty": 50,
         "sixty": 60, "seventy": 70, "eighty": 80, "ninety": 90}


def _collapse_numbers(words):
    """Fold spelled-out numbers into digits so "sixty four" == "64"."""
    out, i = [], 0
    while i < len(words):
        w = words[i]
        if w in _TENS:
            val = _TENS[w]
            if i + 1 < len(words) and words[i + 1] in _UNITS and 0 < _UNITS[words[i + 1]] < 10:
                val += _UNITS[words[i + 1]]
                i += 1
            out.append(str(val))
        elif w in _UNITS:
            out.append(str(_UNITS[w]))
        else:
            out.append(w)
        i += 1
    return out


def normalize(text):
    text = text.lower()
    text = text.replace("’", "'")
    # Strip punctuation but keep intra-word apostrophes out of the picture
    # entirely: "i'm" -> "im" on both sides, so contraction style never counts.
    text = re.sub(r"[^a-z0-9\s']", " ", text)
    text = text.replace("'", "")
    words = text.split()
    return _collapse_numbers(words)


def wer(ref, hyp):
    r, h = normalize(ref), normalize(hyp)
    if not r:
        # Empty reference: any output at all is a total miss (the silence case).
        return (0.0, 0, 0, 0, 0) if not h else (1.0, 0, len(h), 0, len(h))
    # Levenshtein over words, two rows.
    prev = list(range(len(h) + 1))
    # Track operations via a full matrix only when needed for the breakdown.
    d = [[0] * (len(h) + 1) for _ in range(len(r) + 1)]
    for j in range(len(h) + 1):
        d[0][j] = j
    for i in range(1, len(r) + 1):
        d[i][0] = i
        for j in range(1, len(h) + 1):
            cost = 0 if r[i - 1] == h[j - 1] else 1
            d[i][j] = min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost)
    # Backtrack for sub/del/ins counts.
    i, j, sub, dele, ins = len(r), len(h), 0, 0, 0
    while i > 0 or j > 0:
        if i > 0 and j > 0 and d[i][j] == d[i - 1][j - 1] + (0 if r[i - 1] == h[j - 1] else 1):
            if r[i - 1] != h[j - 1]:
                sub += 1
            i, j = i - 1, j - 1
        elif i > 0 and d[i][j] == d[i - 1][j] + 1:
            dele += 1
            i -= 1
        else:
            ins += 1
            j -= 1
    return (d[len(r)][len(h)] / len(r), sub, dele, ins, len(r))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: wer.py <reference-file> <hypothesis-file>", file=sys.stderr)
        sys.exit(2)
    ref = open(sys.argv[1]).read()
    hyp = open(sys.argv[2]).read()
    rate, sub, dele, ins, n = wer(ref, hyp)
    print(f"{rate:.4f} sub={sub} del={dele} ins={ins} refwords={n}")
