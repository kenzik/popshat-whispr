-- whispr: local voice dictation
--
-- Bound to the four symbol keys above the numpad on a Keychron V6:
--
--   circle   XF86Tools     hold = push-to-talk; release transcribes and types
--   triangle XF86Launch5   tap  = toggle on/off for longer dictation
--   X        XF86Launch7   tap  = discard without typing
--   square   XF86Launch6   left free
--
-- The firmware sends F13-F16, but xkb reports them as XF86Tools/XF86Launch5-7,
-- so binding "F13" here silently never fires. The keycode form ("code:191") is
-- no good either: this Lua DSL stores it verbatim with keycode 0 and the bind
-- never matches. Keysym names are what work. Verified by synthesising the keys
-- with ydotool and watching `whispr status`; see scripts/probe-keys.sh.
--
-- These keys carry no modifier, which is the point. With a modified hotkey you
-- release the combo and the daemon types while the modifier is still physically
-- down, turning every character into a window-manager shortcut.

-- Absolute path deliberately: Hyprland's PATH is the login-session one
-- (/usr/local/sbin:/usr/local/bin:/usr/bin:...) and does not include
-- ~/.local/bin, so a bare "whispr" is not found when a bind fires.
local whispr = os.getenv("HOME") .. "/.local/bin/whispr"

hl.bind("XF86Tools",   hl.dsp.exec_cmd(whispr .. " start"))
hl.bind("XF86Tools",   hl.dsp.exec_cmd(whispr .. " stop"), { release = true })
hl.bind("XF86Launch5", hl.dsp.exec_cmd(whispr .. " toggle"))
hl.bind("XF86Launch7", hl.dsp.exec_cmd(whispr .. " cancel"))
