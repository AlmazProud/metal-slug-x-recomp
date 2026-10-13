-- Per-frame fingerprints for differential verification (Phase 4).
--
-- For each frame N (screen:frame_number() in the frame notifier) writes:
--   N <RAM hash> <palette hash>
-- with 32-bit FNV-1a over the bytes of 0x100000-0x10FFFF and 0x400000-0x401FFF (visible bank),
-- in address order. The port (--fingerprints) computes the same thing.
--
-- It also records the inputs the 68000 read in each frame (in MAME they do not change within a
-- frame): E <port frame> <P1> <P2> <SYSTEM> <COINS> <TEST>, in hexadecimal, the bytes
-- as the bus sees them (active low). The port frame is frame_number() + 1: MAME
-- labels N the image that is completed when the port runs frame N, and during that run
-- frame_number() is N - 1 (verified with screenshots and traces, docs/PHASE4_VERIFICATION.md).
--
-- Also, on every VBlank acknowledge (write to 0x3C000C with bit 2, on entry to the
-- handler) writes "V <n> <RAM hash> <palette hash>", with n = order of appearance since
-- power-on. There both sides are on an instruction boundary: there are no half-done
-- writes, unlike the frame boundary, where MAME can be in the middle of an instruction.
--
-- With PD_PIXELS=1, also, for each frame N: "F <N> <image hash>" (320x224 32-bit pixels
-- from screen:pixels(); hash h = h*31 + 64-bit word, with natural overflow).
--
-- Variables: PD_FINGERPRINTS (output), PD_FROM / PD_TO (frames with a fingerprint; all by default),
-- PD_DUMP (list of frames whose whole RAM is saved to PD_DUMP_DIR/ram_mame_NNNNNN.bin).

-- PD_INPUTS_SCRIPT: path of inputs.lua, to also record the input changes.
if os.getenv("PD_INPUTS_SCRIPT") then dofile(os.getenv("PD_INPUTS_SCRIPT")) end

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local scr = manager.machine.screens[":screen"]
local output = io.open(os.getenv("PD_FINGERPRINTS") or "huellas_mame.txt", "w")
local first = tonumber(os.getenv("PD_FROM") or "0")
local last = tonumber(os.getenv("PD_TO") or "1e12")
local dump = {}
for n in string.gmatch(os.getenv("PD_DUMP") or "", "%d+") do dump[tonumber(n)] = true end
local dump_dir = os.getenv("PD_DUMP_DIR") or "."

local function fnv(s)
    local h = 0x811C9DC5
    for i = 1, #s, 4 do
        local a, b, c, d = string.byte(s, i, i + 3)
        h = ((h ~ a) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ b) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ c) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ d) * 0x01000193) & 0xFFFFFFFF
    end
    return h
end

-- Inputs read in the current frame.
local current = { -1, -1, -1, -1, -1 }
local previous = { 0xFF, 0xFF, 0xFF, 0x1F, 0x80 }
local inputs_frame = -1

local function close_inputs()
    if inputs_frame < 0 then return end
    for i = 1, 5 do
        if current[i] >= 0 then previous[i] = current[i] end
        current[i] = -1
    end
    output:write(string.format("E %d %02X %02X %02X %02X %02X\n", inputs_frame + 1,
        previous[1], previous[2], previous[3], previous[4], previous[5]))
end

local function record(i, v)
    local f = scr:frame_number()
    if f ~= inputs_frame then
        close_inputs()
        inputs_frame = f
    end
    if current[i] < 0 then current[i] = v end
end

rtap_e1 = mem:install_read_tap(0x300000, 0x31FFFF, "pd_e1", function(offset, data, mask)
    if offset & 0x80 ~= 0 then
        if mask & 0x00FF ~= 0 then record(5, data & 0xFF) end
    elseif mask & 0xFF00 ~= 0 then
        record(1, (data >> 8) & 0xFF)
    end
end)
rtap_e2 = mem:install_read_tap(0x340000, 0x35FFFF, "pd_e2", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then record(2, (data >> 8) & 0xFF) end
end)
rtap_e3 = mem:install_read_tap(0x380000, 0x39FFFF, "pd_e3", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then record(3, (data >> 8) & 0xFF) end
end)
rtap_e4 = mem:install_read_tap(0x320000, 0x33FFFF, "pd_e4", function(offset, data, mask)
    if mask & 0x00FF ~= 0 then record(4, data & 0x3F) end      -- without the clock's TP and DATA OUT
end)

local vblanks = 0
local dump_v = {}
for n in string.gmatch(os.getenv("PD_DUMP_V") or "", "%d+") do dump_v[tonumber(n)] = true end
wtap_ack = mem:install_write_tap(0x3C000C, 0x3C000D, "pd_ack", function(offset, data, mask)
    if (data & mask & 0x0004) == 0 then return end
    vblanks = vblanks + 1
    pd_lua_read = true                     -- read taps that alter data must skip this read
    local ram = mem:read_range(0x100000, 0x10FFFF, 8)
    local pal = mem:read_range(0x400000, 0x401FFF, 8)
    pd_lua_read = false
    output:write(string.format("V %d %08X %08X f=%d pc=%06X\n", vblanks, fnv(ram), fnv(pal), scr:frame_number(), cpu.state["PC"].value))
    if dump_v[vblanks] then
        local v = io.open(string.format("%s/ram_mame_v%06d.bin", dump_dir, vblanks), "wb")
        v:write(ram)
        v:close()
    end
end)

fingerprints_sub = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if f < first or f > last then return end
    pd_lua_read = true                     -- read taps that alter data must skip this read
    local ram = mem:read_range(0x100000, 0x10FFFF, 8)
    local pal = mem:read_range(0x400000, 0x401FFF, 8)
    pd_lua_read = false
    output:write(string.format("%d %08X %08X\n", f, fnv(ram), fnv(pal)))
    if os.getenv("PD_PIXELS") then
        local px = scr:pixels()
        local h = 0
        for i = 1, #px, 8 do h = h * 31 + string.unpack("<i8", px, i) end
        output:write(string.format("F %d %016X\n", f, h))
    end
    if os.getenv("PD_REGISTERS") then
        local s = cpu.state
        local r = {}
        for _, n in ipairs({"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "SP"}) do
            table.insert(r, string.format("%08X", s[n].value))
        end
        output:write(string.format("R %d %06X %04X %s\n", f, s["PC"].value, s["SR"].value, table.concat(r, " ")))
    end
    if dump[f] then
        local v = io.open(string.format("%s/ram_mame_%06d.bin", dump_dir, f), "wb")
        v:write(ram)
        v:close()
    end
end)

fingerprints_stop_sub = emu.add_machine_stop_notifier(function()
    close_inputs()
    output:close()
end)
