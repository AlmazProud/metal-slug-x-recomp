-- Trace of the Z80's NEO-ZMC bank switching in MAME: reads of I/O ports 0x08-0x0B, where the high
-- byte of the port address selects the bank of each M1 window.
-- Output ($PD_TRACE, trace_z80_banks.txt by default), when MAME stops:
--   <port low byte> <high byte> <count>     one line per distinct pair
--   first <n>: <frame> <pc> <port>          the first 200 accesses
-- Optional: PD_PLAY=1, as in trace_sma.lua (coin, start, then right + fire).

local z80 = manager.machine.devices[":audiocpu"]
local io_space = z80.spaces["io"]
local scr = manager.machine.screens[":screen"]
local pairs_seen, first = {}, {}

z80_btap = io_space:install_read_tap(0x0000, 0xFFFF, "pd_z80_bank", function(offset, data, mask)
    local low = offset & 0xFF
    if low < 0x08 or low > 0x0B then return end
    local key = string.format("%02X %02X", low, offset >> 8)
    pairs_seen[key] = (pairs_seen[key] or 0) + 1
    if #first < 200 then
        first[#first + 1] = string.format("first %d: %d %04X %04X", #first + 1, scr:frame_number(),
            z80.state["PC"].value, offset)
    end
end)

if os.getenv("PD_PLAY") == "1" then
    local fields = {}
    for _, port in pairs(manager.machine.ioport.ports) do
        for name, field in pairs(port.fields) do fields[name] = field end
    end
    local script = { [2600] = "Coin 1", [2800] = "1 Player Start", [3100] = "1 Player Start",
                     [3400] = "P1 Button 1", [3700] = "P1 Button 1" }
    local release = {}
    z80_play = emu.add_machine_frame_notifier(function()
        local f = scr:frame_number()
        local n = script[f]
        if n and fields[n] then fields[n]:set_value(1); release[f + 6] = fields[n] end
        if release[f] then release[f]:set_value(0); release[f] = nil end
        if f > 4000 and fields["P1 Right"] then
            fields["P1 Right"]:set_value(1)
            if fields["P1 Button 1"] then fields["P1 Button 1"]:set_value((f // 8) % 2) end
        end
    end)
end

z80_stop = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "trace_z80_banks.txt", "w")
    local keys = {}
    for k in pairs(pairs_seen) do keys[#keys + 1] = k end
    table.sort(keys)
    for _, k in ipairs(keys) do f:write(string.format("%s %d\n", k, pairs_seen[k])) end
    for _, l in ipairs(first) do f:write(l, "\n") end
    f:close()
end)
