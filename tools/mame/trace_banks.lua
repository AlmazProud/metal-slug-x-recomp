-- Trace of P2 bank changes (writes to 0x2FFFF0-0x2FFFFF).
-- Usage: mame mslugx -bios us -autoboot_script tools/mame/trace_banks.lua ...
-- On exit writes the summary to $PD_TRACE (traza_bancos.txt by default).
-- Optional: PD_PLAY=1 inserts a coin and presses START to enter a game.

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local stats = { total = 0, values = {}, pcs = {}, sequence = {} }

btap = mem:install_write_tap(0x2FFFF0, 0x2FFFFF, "pd_bank_w", function(offset, data, mask)
    local pc = cpu.state["PC"].value
    stats.total = stats.total + 1
    local key = string.format("%02X", data & 0xFF)
    stats.values[key] = (stats.values[key] or 0) + 1
    stats.pcs[pc] = (stats.pcs[pc] or 0) + 1
    if #stats.sequence < 40 then
        table.insert(stats.sequence, string.format("frame=%d pc=%06X off=%X dato=%04X",
            manager.machine.screens[":screen"]:frame_number(), pc, offset - 0x2FFFF0, data))
    end
end)

-- Optional automatic input (coin + start) to leave attract mode.
if os.getenv("PD_PLAY") == "1" then
    local fields = {}
    for _, port in pairs(manager.machine.ioport.ports) do
        for name, field in pairs(port.fields) do fields[name] = field end
    end
    local script = { [600] = "Coin 1", [700] = "1 Player Start", [900] = "1 Player Start",
                    [1100] = "1 Player Start", [1300] = "P1 Button 1" }
    local release = {}
    sub_frame = emu.add_machine_frame_notifier(function()
        local f = manager.machine.screens[":screen"]:frame_number()
        local n = script[f]
        if n and fields[n] then fields[n]:set_value(1); release[f + 6] = fields[n] end
        if release[f] then release[f]:set_value(0); release[f] = nil end
    end)
end

-- The output text below is kept as is (trace file format).
banks_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "traza_bancos.txt", "w")
    f:write(string.format("escrituras de bank: %d\n", stats.total))
    for v, n in pairs(stats.values) do f:write(string.format("dato=%s: %d\n", v, n)) end
    for pc, n in pairs(stats.pcs) do f:write(string.format("desde pc=%06X: %d\n", pc, n)) end
    for _, s in ipairs(stats.sequence) do f:write(s .. "\n") end
    f:close()
end)
