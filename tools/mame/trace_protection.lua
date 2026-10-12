-- Trace of the mslugx protection (0x2FFFE0-0x2FFFEF) and check against the model
-- documented in docs/ROM_MAP.md. Usage:
--   mame mslugx -bios us -autoboot_script tools/mame/trace_protection.lua ...
-- On exit writes a summary to the file named by the PD_TRACE environment variable
-- (traza_proteccion.txt in the current directory by default).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]

-- Independent model (from the documentation, not MAME's device)
local model = { command = 0, counter = 0 }
local stats = { writes = {}, reads = 0, ok = 0, bad = 0, cmds = {}, pcs = {}, examples = {} }

local function table_bit(index)
    local byte = mem:read_u8(0xDEDD2 + ((index >> 3) & 0xFFF))
    return (byte >> (7 - (index & 7))) & 1
end

local function expected()
    if model.command == 0x0001 then
        local r = table_bit(model.counter)
        model.counter = model.counter + 1
        return r
    elseif model.command == 0x0FFF then
        return table_bit(mem:read_u16(0x10F00A) - 1)
    end
    return 0
end

wtap = mem:install_write_tap(0x2FFFE0, 0x2FFFEF, "pd_prot_w", function(offset, data, mask)
    local off = offset - 0x2FFFE0
    stats.writes[off] = (stats.writes[off] or 0) + 1
    if off == 0x0 then model.command = 0
    elseif off == 0x2 or off == 0x4 then model.command = model.command | data
    elseif off == 0xA then model.counter = 0; model.command = 0 end
end)

rtap = mem:install_read_tap(0x2FFFE0, 0x2FFFEF, "pd_prot_r", function(offset, data, mask)
    stats.reads = stats.reads + 1
    local cmd = model.command
    stats.cmds[cmd] = (stats.cmds[cmd] or 0) + 1
    local pc = cpu.state["PC"].value
    stats.pcs[pc] = (stats.pcs[pc] or 0) + 1
    local e = expected()
    if (data & 1) == e then stats.ok = stats.ok + 1
    else
        stats.bad = stats.bad + 1
        if #stats.examples < 10 then
            table.insert(stats.examples, string.format("pc=%06X cmd=%04X leido=%d esperado=%d", pc, cmd, data & 1, e))
        end
    end
end)

-- The subscription must be stored in a global variable; otherwise Lua collects it and it is never called.
-- The output text below is kept as is (trace file format).
protection_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "traza_proteccion.txt", "w")
    f:write(string.format("lecturas=%d coinciden=%d difieren=%d\n", stats.reads, stats.ok, stats.bad))
    for off, n in pairs(stats.writes) do f:write(string.format("escrituras en +%X: %d\n", off, n)) end
    for c, n in pairs(stats.cmds) do f:write(string.format("lecturas con command=%04X: %d\n", c, n)) end
    local npc = 0
    for pc, n in pairs(stats.pcs) do
        npc = npc + 1
        if npc <= 15 then f:write(string.format("lectura desde pc=%06X: %d\n", pc, n)) end
    end
    f:write(string.format("PCs distintos que leen: %d\n", npc))
    for _, s in ipairs(stats.examples) do f:write("DIFERENCIA " .. s .. "\n") end
    f:close()
end)
