# Keybindings

The daemon never grabs keys itself: only 1 of 26 `/dev/input/event*` nodes is
readable by an ordinary user, and joining the `input` group would let any
process keylog the session. So the window manager owns the hotkey and calls
`whispr`, which is what keeps the daemon portable — only this snippet changes
between compositors.

Push-to-talk needs both a press and a release binding. A WM that cannot bind
key releases can still use `whispr toggle`.

## Hyprland (Lua config)

Copy `hyprland/whispr.lua` to `~/.config/hypr/config/whispr.lua` and add to
`~/.config/hypr/hyprland.lua`:

```lua
require("config.whispr")
```

## Hyprland (hyprlang config)

```
bind  = , F13, exec, whispr start
bindr = , F13, exec, whispr stop
bind  = , F14, exec, whispr toggle
bind  = SUPER, F14, exec, whispr cancel
```

## sway / i3

```
bindsym --no-repeat F13 exec whispr start
bindsym --release   F13 exec whispr stop
bindsym             F14 exec whispr toggle
bindsym $mod+F14        exec whispr cancel
```

## KDE Plasma

System Settings > Shortcuts > Custom Shortcuts. Plasma has no release trigger,
so bind `whispr toggle` to F14 and use toggle mode.

## GNOME

`Settings > Keyboard > Custom Shortcuts`, or:

```sh
gsettings set org.gnome.settings-daemon.plugins.media-keys custom-keybindings \
  "['/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/']"
```

GNOME has no release trigger either — use toggle mode.

## X11, any WM (sxhkd)

sxhkd is the only one of these that gives push-to-talk on X11:

```
F13
    whispr start
@F13
    whispr stop
F14
    whispr toggle
```
