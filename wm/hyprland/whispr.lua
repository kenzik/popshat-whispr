-- whispr: local voice dictation
--
-- An EXAMPLE, not a default. The keysyms below are what one particular
-- keyboard sends -- a Keychron V6 whose four symbol keys above the numpad are
-- programmed to F13-F16. Run scripts/probe-keys.sh to find out what YOUR keys
-- send, and replace them here. Copy to ~/.config/hypr/config/whispr.lua and
-- add require("config.whispr") to ~/.config/hypr/hyprland.lua.
--
--   hold  = push-to-talk; release transcribes and types
--   tap   = hands-free; stops on its own after you go quiet
--   tap   = toggle on/off manually
--   tap   = discard without typing
--
-- Bound by KEYSYM, not keycode. The firmware sends F13-F16, but xkb reports
-- them as XF86Tools/XF86Launch5-7, so binding "F13" silently never fires -- and
-- the "code:NNN" form is stored verbatim by this Lua DSL with keycode 0 and
-- never matches either. Verified with scripts/probe-keys.sh.
--
-- Whatever keys you choose, they must carry NO modifier. With a modified hotkey
-- you release the combo and whispr types while the modifier is still physically
-- down, turning every character of your transcript into a window-manager
-- shortcut.

-- Absolute path deliberately: Hyprland's PATH is the login-session one
-- (/usr/local/sbin:/usr/local/bin:/usr/bin:...) and does not include
-- ~/.local/bin, so a bare "whispr" is not found when a bind fires -- and the
-- failure is completely silent.
local whispr = os.getenv("HOME") .. "/.local/bin/whispr"

hl.bind("XF86Tools",   hl.dsp.exec_cmd(whispr .. " start"))
hl.bind("XF86Tools",   hl.dsp.exec_cmd(whispr .. " stop"), { release = true })
hl.bind("XF86Launch6", hl.dsp.exec_cmd(whispr .. " dictate"))
hl.bind("XF86Launch5", hl.dsp.exec_cmd(whispr .. " toggle"))
hl.bind("XF86Launch7", hl.dsp.exec_cmd(whispr .. " cancel"))
