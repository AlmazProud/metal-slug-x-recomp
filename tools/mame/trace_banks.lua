-- Traza de cambios de bank de P2 (escrituras en 0x2FFFF0-0x2FFFFF).
-- Uso: mame mslugx -bios us -autoboot_script tools/mame/trace_banks.lua ...
-- Al salir escribe el resumen en $PD_TRAZA (por defecto traza_bancos.txt).
-- Opcional: PD_JUGAR=1 inserta una moneda y pulsa START para entrar en partida.

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local stats = { total = 0, valores = {}, pcs = {}, secuencia = {} }

btap = mem:install_write_tap(0x2FFFF0, 0x2FFFFF, "pd_bank_w", function(offset, data, mask)
    local pc = cpu.state["PC"].value
    stats.total = stats.total + 1
    local clave = string.format("%02X", data & 0xFF)
    stats.valores[clave] = (stats.valores[clave] or 0) + 1
    stats.pcs[pc] = (stats.pcs[pc] or 0) + 1
    if #stats.secuencia < 40 then
        table.insert(stats.secuencia, string.format("frame=%d pc=%06X off=%X dato=%04X",
            manager.machine.screens[":screen"]:frame_number(), pc, offset - 0x2FFFF0, data))
    end
end)

-- Entrada automática opcional (moneda + start) para salir del attract mode.
if os.getenv("PD_JUGAR") == "1" then
    local campos = {}
    for _, puerto in pairs(manager.machine.ioport.ports) do
        for name, campo in pairs(puerto.fields) do campos[name] = campo end
    end
    local script = { [600] = "Coin 1", [700] = "1 Player Start", [900] = "1 Player Start",
                    [1100] = "1 Player Start", [1300] = "P1 Button 1" }
    local soltar = {}
    sub_frame = emu.add_machine_frame_notifier(function()
        local f = manager.machine.screens[":screen"]:frame_number()
        local n = script[f]
        if n and campos[n] then campos[n]:set_value(1); soltar[f + 6] = campos[n] end
        if soltar[f] then soltar[f]:set_value(0); soltar[f] = nil end
    end)
end

notificador_fin = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "traza_bancos.txt", "w")
    f:write(string.format("escrituras de bank: %d\n", stats.total))
    for v, n in pairs(stats.valores) do f:write(string.format("dato=%s: %d\n", v, n)) end
    for pc, n in pairs(stats.pcs) do f:write(string.format("desde pc=%06X: %d\n", pc, n)) end
    for _, s in ipairs(stats.secuencia) do f:write(s .. "\n") end
    f:close()
end)
