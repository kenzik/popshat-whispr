# Keybindings

whispr never grabs a key itself. Your window manager owns the hotkey and runs
`whispr start` / `whispr stop`, which talk to the daemon over a Unix socket.

Two reasons:

- **Security.** Reading keys directly means reading `/dev/input/event*`, and
  joining the `input` group to do it lets *every* process you run keylog the
  session. A dictation tool does not deserve that.
- **Portability.** The daemon never learns which compositor it is under, so it
  runs anywhere. This file is the only part that differs between systems.

---

## Step 1: pick a key, and find out what it really sends

**Do not skip this.** The name you think a key has is frequently not the name
your compositor will match.

A keyboard's F13 arrives through xkb as `XF86Tools`, and F14–F16 as
`XF86Launch5`–`XF86Launch7`. A binding written as `F13` silently never fires:
no error, no log, nothing to debug.

```sh
scripts/probe-keys.sh
```

Press the keys you want to use; it prints the keysym and keycode each one
actually emits, and a ready-to-paste line for common compositors.

### The key must carry no modifier

This is the single most important constraint in whispr, and it is not obvious.

With a modified hotkey (`SUPER+grave`, say) you release the combo and whispr
starts typing while `SUPER` is still physically held down — so every character
of your transcript becomes a window-manager shortcut. Windows close. Workspaces
switch. It is memorably bad.

Good candidates, in rough order of preference:

| Candidate | Notes |
|---|---|
| F13–F24 | Exist for exactly this. Nothing else binds them. Needs a programmable keyboard (QMK/VIA) or `xkb` remapping |
| Extra/macro keys | Any key your keyboard already has spare |
| Media or "launch" keys | `XF86Launch*`, `XF86Tools` — usually unbound |
| Caps Lock, remapped | Available on every keyboard; remap it with xkb or `keyd` |
| Right Ctrl / Menu | Modifier-free in practice if you never use them |

No spare key at all? Use `whispr toggle` on any binding you like, including a
modified one — toggle is a tap, so nothing is held down when typing begins.

---

## Step 2: bind it

Push-to-talk needs **both** a press and a release binding. A compositor that
cannot bind releases can still run `whispr toggle` (tap on, tap off) or
`whispr dictate` (tap, speak, and it stops itself once you go quiet) — both are
fully supported, not consolation prizes.

Replace `XF86Tools` below with whatever `probe-keys.sh` printed for your key.

> Only the Hyprland snippets are verified on a running system. The rest are
> written from each project's documentation. Corrections are very welcome —
> that is exactly the kind of issue worth filing.

### Hyprland (hyprlang, the default config format)

```
bind  = , XF86Tools,   exec, ~/.local/bin/whispr start
bindr = , XF86Tools,   exec, ~/.local/bin/whispr stop
bind  = , XF86Launch6, exec, ~/.local/bin/whispr dictate
bind  = , XF86Launch5, exec, ~/.local/bin/whispr toggle
bind  = , XF86Launch7, exec, ~/.local/bin/whispr cancel
```

`bindr` is the release binding. **Use the absolute path** — Hyprland runs with
the login-session `PATH`, which does not include `~/.local/bin`, and a bind
that cannot find its program fails completely silently.

### Hyprland (Lua config)

Copy [`hyprland/whispr.lua`](hyprland/whispr.lua) to
`~/.config/hypr/config/whispr.lua` and add to `~/.config/hypr/hyprland.lua`:

```lua
require("config.whispr")
```

### sway / i3

```
bindsym --no-repeat XF86Tools exec ~/.local/bin/whispr start
bindsym --release   XF86Tools exec ~/.local/bin/whispr stop
bindsym XF86Launch6 exec ~/.local/bin/whispr dictate
bindsym XF86Launch5 exec ~/.local/bin/whispr toggle
```

`--no-repeat` matters: without it, holding the key re-fires `start` at the
keyboard repeat rate.

### river

```sh
riverctl map          normal None XF86Tools spawn '~/.local/bin/whispr start'
riverctl map -release normal None XF86Tools spawn '~/.local/bin/whispr stop'
riverctl map          normal None XF86Launch6 spawn '~/.local/bin/whispr dictate'
```

### KDE Plasma

System Settings → Shortcuts → Add Command. Plasma has no key-release trigger,
so bind **`whispr dictate`** (hands-free) or `whispr toggle` rather than
push-to-talk.

### GNOME

Settings → Keyboard → Custom Shortcuts, or:

```sh
gsettings set org.gnome.settings-daemon.plugins.media-keys custom-keybindings \
  "['/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/']"
```

No key-release trigger either — use `whispr dictate` or `whispr toggle`.

### X11, any window manager (sxhkd)

sxhkd gives push-to-talk on X11, which most X11 window managers cannot do alone.
`@` marks the release:

```
XF86Tools
    ~/.local/bin/whispr start

@XF86Tools
    ~/.local/bin/whispr stop

XF86Launch6
    ~/.local/bin/whispr dictate
```

### X11 (xbindkeys)

```
"~/.local/bin/whispr start"
    XF86Tools

"~/.local/bin/whispr stop"
    Release + XF86Tools
```

### Anything else

If your environment can run a command on a key press, it can drive whispr.
Bind `whispr dictate` to one key and you have a working setup; add a release
binding for push-to-talk if the compositor supports one. A snippet for a
compositor not listed here is a welcome pull request.

---

## Step 3: check it fired

```sh
whispr status        # idle | recording | transcribing
```

Press the key and run that. If it stays `idle`, the bind is not reaching the
daemon. On Hyprland, `scripts/diag-key.sh` tells you which half is broken —
whether the compositor matched the bind at all, or matched it and failed to
launch the program.

Everywhere else, the same question answered by hand:

```sh
~/.local/bin/whispr toggle     # works? then the daemon is fine and the bind is wrong
```

The overwhelmingly common cause is `PATH`. Use absolute paths in bindings.
