-- whispr: local voice dictation
--
-- F13 held  = push-to-talk (release transcribes and types)
-- F14 tap   = toggle on/off for longer dictation
-- SUPER+F14 = discard without typing
--
-- F13/F14 are deliberately modifier-free. With a modified key you release the
-- combo and the daemon starts typing while the modifier is still physically
-- down, turning every character into a window-manager shortcut.
--
-- Map two unused keys to F13/F14 in VIA/QMK first, and confirm with `wev` that
-- they arrive as F13/F14 before relying on these.

local whispr = "whispr"

hl.bind("F13", hl.dsp.exec_cmd(whispr .. " start"))
hl.bind("F13", hl.dsp.exec_cmd(whispr .. " stop"), { release = true })
hl.bind("F14", hl.dsp.exec_cmd(whispr .. " toggle"))
hl.bind("SUPER + F14", hl.dsp.exec_cmd(whispr .. " cancel"))
