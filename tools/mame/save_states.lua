-- Saves MAME save states at given frames (research: reading a chip's internal state, e.g. the
-- YM2610 SSG noise generator, from the state file). PD_SAVE_FRAMES: comma-separated frames
-- (screen:frame_number()); the states go to MAME's -state_directory as pd_fNNNNNN.sta.
-- Meant as PD_EXTRA_SCRIPT of tools/mame_run.py (which guards against the soft reset rerun).
local scr = manager.machine.screens[":screen"]
local frames = {}
for n in string.gmatch(os.getenv("PD_SAVE_FRAMES") or "", "%d+") do frames[tonumber(n)] = true end
save_states_sub = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if frames[f] then manager.machine:save(string.format("pd_f%06d", f)) end
end)
