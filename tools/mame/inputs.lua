-- Inputs of a playback (-playback) as CHANGES OVER TIME, for the port (Phase 4).
--
-- For each input port it records when the value read by the 68000 changes:
--   C <us of the last read with the old value> <us of the first one with the new value> <port> <value>
-- ports: P1 (high byte of 300000), P2 (340000), SIS (380000), MON (bits 0-5 of 320001),
-- TEST (300081). The port applies each change between those two instants (--inputs option); since
-- it performs the same reads at the same cycles, it reads exactly what MAME read.
-- (Tying the inputs to frame_number() does not work: inside a tap it does not advance like the frame.)
-- Variable: PD_INPUTS (output).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local output = io.open(os.getenv("PD_INPUTS") or "entradas_mame.txt", "w")
local last_value = { P1 = 0xFF, P2 = 0xFF, SIS = 0xFF, MON = 0x1F, TEST = 0x80 }
local last_t = { P1 = 0, P2 = 0, SIS = 0, MON = 0, TEST = 0 }

local function now() return manager.machine.time:as_double() * 1e6 end

local function record(p, v)
    local t = now()
    if v ~= last_value[p] then
        output:write(string.format("C %.3f %.3f %s %02X\n", last_t[p], t, p, v))
        last_value[p] = v
    end
    last_t[p] = t
end

rtap_ent1 = mem:install_read_tap(0x300000, 0x31FFFF, "pd_ent1", function(offset, data, mask)
    if offset & 0x80 ~= 0 then
        if mask & 0x00FF ~= 0 then record("TEST", data & 0xFF) end
    elseif mask & 0xFF00 ~= 0 then
        record("P1", (data >> 8) & 0xFF)
    end
end)
rtap_ent2 = mem:install_read_tap(0x340000, 0x35FFFF, "pd_ent2", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then record("P2", (data >> 8) & 0xFF) end
end)
rtap_ent3 = mem:install_read_tap(0x380000, 0x39FFFF, "pd_ent3", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then record("SIS", (data >> 8) & 0xFF) end
end)
rtap_ent4 = mem:install_read_tap(0x320000, 0x33FFFF, "pd_ent4", function(offset, data, mask)
    if mask & 0x00FF ~= 0 then record("MON", data & 0x3F) end
end)

inputs_stop_sub = emu.add_machine_stop_notifier(function() output:close() end)
