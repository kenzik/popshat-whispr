# Third-party components

whispr itself is MIT (see [LICENSE](LICENSE)). It does not bundle any of the
following — each is fetched from its own upstream at build or install time —
but redistributing a built whispr, or the models alongside it, means carrying
these terms too.

Licences below were read from each project's own repository. Where whispr only
reads a file the system already provides, that is noted rather than claimed.

## Code

| Component | Where | Licence |
|---|---|---|
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (incl. `ggml`, `libwhisper`, `libparakeet`) | `vendor/whisper.cpp`, git submodule pinned at **v1.9.4** | MIT — Copyright (c) 2023-2026 The ggml authors |

whispr compiles against `whisper.h` and `parakeet.h` and links the libraries
built from that submodule. Nothing is vendored by copy; `git submodule update
--init --recursive` fetches it.

## Models

None are redistributed by this repository. `scripts/install.sh` downloads the
first two from Hugging Face; the third is opt-in.

| Model | Source | Licence |
|---|---|---|
| `ggml-base.en.bin` (141 MB) | [`ggerganov/whisper.cpp`](https://huggingface.co/ggerganov/whisper.cpp) | MIT — a ggml conversion of [OpenAI Whisper](https://github.com/openai/whisper) (MIT) |
| `ggml-silero-v5.1.2.bin` (865 KB, VAD) | [`ggml-org/whisper-vad`](https://huggingface.co/ggml-org/whisper-vad) | MIT — a ggml conversion of [silero-vad](https://github.com/snakers4/silero-vad) |
| `ggml-parakeet-tdt-0.6b-v3-f16.bin` (1.2 GB, optional) | [`ggml-org/parakeet-GGUF`](https://huggingface.co/ggml-org/parakeet-GGUF) | Conversion repo is MIT; the **original weights are [`nvidia/parakeet-tdt-0.6b-v3`](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3) under CC-BY-4.0** |

**If you enable `engine = parakeet`, CC-BY-4.0 applies to those weights** and
attribution to NVIDIA is required in anything you redistribute. This is the one
component here that is not MIT, which is part of why it is neither the default
nor downloaded unless you ask for it.

## Runtime dependencies

Called as external programs, not linked, and installed from your distribution:

- **PipeWire** (`pw-record`), **PulseAudio** (`parecord`) or **ALSA**
  (`arecord`) — capture. The first one found is used.
- **ydotool** / **ydotoold**, **wtype**, **xdotool**, **wl-clipboard** /
  **xclip** — text injection, tried in that order.
- **libnotify** (`notify-send`) — optional on-screen feedback.

`whispr-notify` plays start and stop cues from
`/usr/share/sounds/freedesktop/stereo` **if that directory exists**. Those files
belong to the freedesktop sound theme as packaged by your distribution; whispr
ships no audio of its own and skips the cue silently when the theme is absent.
