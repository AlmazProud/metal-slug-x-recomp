-- Scripted run for the per-mission verification (Phase 8): drives the inputs from a script file
-- and reproduces the port's debug-mode forced reads, then records fingerprints and input changes
-- (fingerprints.lua + inputs.lua) like a playback.
--
-- Environment variables:
--   PD_RUN_SCRIPT        script in the port's headless format: "<from> <to> token..." per line,
--                        frames as screen:frame_number(); tokens up down left right a b c d
--                        start1 coin1 select1 (comments with #)
--   PD_DEVMODE=1         BIOS development mode (0x10FE80 and the game's copy, PD_DEVMODE_COPY, read as 1
--                        by cartridge code, PC < 0xC00000), as src/port/cheats.c does
--   PD_DEVMODE_COPY=hex  the game's copy of the mode (default 10C2BE, Metal Slug X; Metal Slug 3: 10F002;
--                        0: none, Metal Slug)
--   PD_DIPS=hex          debug dips (word PD_DIPS_WORD, default 0x10F000; Metal Slug 0x100000) forced on
--                        for cartridge code, as the port does
--   PD_BLOOD=word,mask   the BLOOD soft dip read as on (the port's red blood option)
--   PD_FINGERPRINTS_SCRIPT  path of fingerprints.lua (run afterwards; it loads inputs.lua through
--                        PD_INPUTS_SCRIPT)
--   PD_EXTRA_SCRIPT      optional: another script to run too (cycles.lua needs --debug)
--   PD_SCREENSHOTS_SCRIPT  optional: screenshots.lua (PD_SCREENSHOT_FRAMES, -snapshot_directory)
--
-- The MVS edge connector has no SELECT button in MAME: select1 clears the P1 SELECT bit
-- (0x380000, bit 1 of the high byte) in the 68000's reads, before inputs.lua records them.

-- MAME runs the autoboot script again after a soft reset (the watchdog): the taps and notifiers
-- of the first run stay installed, so a second run must do nothing (it would also truncate the
-- output files).
if pd_scripted_run_loaded then return end
pd_scripted_run_loaded = true

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

-- PD_POKE="frame,address,value[;...]" (hex address and value): writes a work RAM byte in the frame
-- notifier of that frame (its end), as the port's PD_POKE does (src/port/headless.c).
local pokes = {}
for f, a, v in string.gmatch(os.getenv("PD_POKE") or "", "(%d+),(%x+),(%x+)") do
    table.insert(pokes, { frame = tonumber(f), addr = tonumber(a, 16), value = tonumber(v, 16) })
end

-- PD_KILL_ENEMIES="maxlife,from": from that frame on, the port's headless kill_enemies (Metal Slug's
-- fast-forward study, src/port/headless.c): every object in the list but the players and the
-- vehicle whose life word is 1..maxlife gets the damage routine's death mark (life 0, dead bit),
-- on every VBlank acknowledge after its fingerprint (fingerprints.lua calls pd_after_vblank; the
-- port's frames end at another point than MAME's frame notifier, sometimes in the middle of an
-- object's creation). Metal Slug's layout: the list from 0x100080 ($8 next), life $66, dead bit 0
-- of $13, players 0x100440 / 0x1004E0, vehicle 0x100580.
local kill_max, kill_from = string.match(os.getenv("PD_KILL_ENEMIES") or "", "(%d+),(%d+)")
kill_max, kill_from = tonumber(kill_max), tonumber(kill_from)
local KILL_HEAD, KILL_SKIP = 0x100080, { [0x100440] = true, [0x1004E0] = true, [0x100580] = true }
local function kill_enemies(f)
    if not kill_max or kill_max == 0 or f + 1 < kill_from then return end
    pd_lua_read = true
    local a = KILL_HEAD
    for _ = 1, 400 do
        a = mem:read_u32(a + 8)
        if a == KILL_HEAD or a < 0x100000 or a >= 0x10F000 then break end
        if not KILL_SKIP[a] then
            local v = mem:read_u16(a + 0x66)
            if v >= 0x8000 then v = v - 0x10000 end
            if v >= 1 and v <= kill_max then
                mem:write_u16(a + 0x66, 0)
                mem:write_u8(a + 0x13, mem:read_u8(a + 0x13) | 0x01)
            end
        end
    end
    pd_lua_read = false
end

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
    for _, p in ipairs(pokes) do
        if p.frame == f then mem:write_u8(p.addr, p.value) end
    end
end)
-- (in _G: the autoboot script has its own globals, the scripts it runs with dofile do not)
-- PD_KILL_FLAG="address,from[,until]" (hex address): from that frame on (and before `until`), 0xFF
-- is written to that byte on every VBlank acknowledge, as the port's headless PD_KILL_FLAG does.
-- Metal Slug X: its kill-everything request 0x1067E3, which every enemy's hit check obeys
-- (docs/BOSS_RUSH.md).
local flag_addr, flag_from, flag_until = string.match(os.getenv("PD_KILL_FLAG") or "", "(%x+),(%d+),?(%d*)")
flag_addr, flag_from = tonumber(flag_addr or "", 16), tonumber(flag_from)
flag_until = tonumber(flag_until) or math.huge
local function kill_flag(f)
    if flag_addr and f + 1 >= flag_from and f + 1 < flag_until then mem:write_u8(flag_addr, 0xFF) end
end
_G.pd_after_vblank = function() kill_enemies(scr:frame_number()); kill_flag(scr:frame_number()) end

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
    local copy = tonumber(os.getenv("PD_DEVMODE_COPY") or "10C2BE", 16)
    if copy ~= 0 then                       -- 0: the game keeps no copy (Metal Slug)
        script_dev_copy_tap = mem:install_read_tap(copy, copy + 1, "pd_devcopy", function(offset, data, mask)
            if cart() and (mask & 0xFF00) ~= 0 then return (data & 0x00FF) | 0x0100 end
        end)
    end
end
-- PD_BLOOD="word,mask" (hex): the BLOOD soft dip byte under the mask reads 1 (the port's red blood).
local blood_word, blood_mask = string.match(os.getenv("PD_BLOOD") or "", "(%x+),(%x+)")
if blood_word then
    blood_word, blood_mask = tonumber(blood_word, 16), tonumber(blood_mask, 16)
    script_blood_tap = mem:install_read_tap(blood_word, blood_word + 1, "pd_blood", function(offset, data, mask)
        if cart() and (mask & blood_mask) ~= 0 then return (data & ~blood_mask) | (blood_mask & 0x0101) end
    end)
end
local dips = tonumber(os.getenv("PD_DIPS") or "0", 16)
local dips_word = tonumber(os.getenv("PD_DIPS_WORD") or "10F000", 16)    -- Metal Slug: 100000
if dips ~= 0 then
    script_dips_tap = mem:install_read_tap(dips_word, dips_word + 1, "pd_dips", function(offset, data, mask)
        if cart() then return data | (dips & mask) end
    end)
end

dofile(os.getenv("PD_FINGERPRINTS_SCRIPT"))
if os.getenv("PD_SCREENSHOTS_SCRIPT") then dofile(os.getenv("PD_SCREENSHOTS_SCRIPT")) end
if os.getenv("PD_EXTRA_SCRIPT") then dofile(os.getenv("PD_EXTRA_SCRIPT")) end      -- e.g. cycles.lua
