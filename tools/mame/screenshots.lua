-- MAME screenshots at specific frames, to compare them with the port.
-- Environment variables: PD_SCREENSHOT_FRAMES = comma-separated list; PD_PREFIX = file name prefix
-- (the images end up in the -snapshot_directory as <prefix>_NNNNNN.png).

local scr = manager.machine.screens[":screen"]
local requested = {}
for n in string.gmatch(os.getenv("PD_SCREENSHOT_FRAMES") or "", "%d+") do requested[tonumber(n)] = true end
local prefix = os.getenv("PD_PREFIX") or "mame"

screenshots_sub = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if requested[f] then
        scr:snapshot(string.format("%s_%06d.png", prefix, f))
    end
end)
