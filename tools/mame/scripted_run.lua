-- Scripted run for the per-mission verification (Phase 8): drives the inputs from a script file
-- and reproduces the port's debug-mode forced reads, then records fingerprints and input changes
-- (fingerprints.lua + inputs.lua) like a playback.
--
-- Environment variables:
--   PD_RUN_SCRIPT        script in the port's headless format: "<from> <to> token..." per line,
--                        frames as screen:frame_number(); tokens up down left right a b c d
--                        start1 coin1 select1 (comments with #)
--   PD_DEVMODE=1         BIOS development mode (0x10FE80 and the game's copy 0x10C2BE read as 1
--                        by cartridge code, PC < 0xC00000), as src/port/cheats.c does
--   PD_DIPS=hex          debug dips (word 0x10F000) forced on for cartridge code, as the port does
--   PD_FINGERPRINTS_SCRIPT  path of fingerprints.lua (run afterwards; it loads inputs.lua through
--                        PD_INPUTS_SCRIPT)
--   PD_EXTRA_SCRIPT      optional: another script to run too (cycles.lua needs --debug)
--   PD_SCREENSHOTS_SCRIPT  optional: screenshots.lua (PD_SCREENSHOT_FRAMES, -snapshot_directory)
--
-- The MVS edge connector has no SELECT button in MAME: select1 clears the P1 SELECT bit
-- (0x380000, bit 1 of the high byte) in the 68000's reads, before inputs.lua records them.

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local scr = manager.machine.screens[":screen"]

-- ---- script
local spans = {}
for line in io.lines(os.getenv("PD_RUN_SCRIPT")) do
    if not line:match("^%s*#") then
        local from, to, rest = line:match("^%s*(%d+)%s+(%d+)%s*(.*)$")
        if from then
            local s = { from = tonumber(from), to = tonumber(to), keys = {} }
            for t in rest:gmatch("%S+") do s.keys[t] = true end
            table.insert(spans, s)
        end
    end
end

local FIELD = {
    up = "P1 Up", down = "P1 Down", left = "P1 Left", right = "P1 Right",
    a = "P1 A", b = "P1 B", c = "P1 C", d = "P1 D", start1 = "1 Player Start", coin1 = "Coin 1",
}
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for name, field in pairs(port.fields) do fields[name] = field end
end
local select_held = false

script_frame_sub = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    local on = {}
    for _, s in ipairs(spans) do
        if f >= s.from and f <= s.to then for k in pairs(s.keys) do on[k] = true end end
    end
    for token, name in pairs(FIELD) do
        if fields[name] then fields[name]:set_value(on[token] and 1 or 0) end
    end
    select_held = on.select1 == true
end)

script_select_tap = mem:install_read_tap(0x380000, 0x380001, "pd_select", function(offset, data, mask)
    if select_held and (mask & 0xFF00) ~= 0 then return data & ~0x0200 end
end)

-- ---- forced reads (cartridge code only)
-- (not for fingerprints.lua's own RAM reads, flagged with pd_lua_read)
local function cart() return not pd_lua_read and cpu.state["PC"].value < 0xC00000 end
if os.getenv("PD_DEVMODE") then
    script_dev_tap = mem:install_read_tap(0x10FE80, 0x10FE81, "pd_devmode", function(offset, data, mask)
        if cart() and (mask & 0xFF00) ~= 0 then return (data & 0x00FF) | 0x0100 end
    end)
    script_dev_copy_tap = mem:install_read_tap(0x10C2BE, 0x10C2BF, "pd_devcopy", function(offset, data, mask)
        if cart() and (mask & 0xFF00) ~= 0 then return (data & 0x00FF) | 0x0100 end
    end)
end
local dips = tonumber(os.getenv("PD_DIPS") or "0", 16)
if dips ~= 0 then
    script_dips_tap = mem:install_read_tap(0x10F000, 0x10F001, "pd_dips", function(offset, data, mask)
        if cart() then return data | (dips & mask) end
    end)
end

dofile(os.getenv("PD_FINGERPRINTS_SCRIPT"))
if os.getenv("PD_SCREENSHOTS_SCRIPT") then dofile(os.getenv("PD_SCREENSHOTS_SCRIPT")) end
if os.getenv("PD_EXTRA_SCRIPT") then dofile(os.getenv("PD_EXTRA_SCRIPT")) end      -- e.g. cycles.lua
