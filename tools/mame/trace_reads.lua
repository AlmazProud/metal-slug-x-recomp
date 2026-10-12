-- Lecturas del 68000 en los puertos de entrada (0x300000-0x39FFFF) dentro de una window de
-- tiempo: hour (us), PC, dirección, value y máscara. Mismo formato que la opción del port
-- PD_TRAZA_LECTURAS. Variables: PD_DESDE_US, PD_HASTA_US, PD_TRAZA.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local desde = tonumber(os.getenv("PD_DESDE_US") or "0")
local hasta = tonumber(os.getenv("PD_HASTA_US") or "1e12")
local lineas = {}
rtap_lect = mem:install_read_tap(0x300000, 0x39FFFF, "pd_lect", function(offset, data, mask)
    local t = manager.machine.time:as_double() * 1e6
    if t >= desde and t <= hasta and (offset & 0xFE0000) ~= 0x320000 then
        table.insert(lineas, string.format("%.3f pc=%06X %06X %04X/%04X", t, cpu.state["PC"].value, offset, data & mask, mask))
    end
end)
notificador_fin_lect = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "traza_lecturas.txt", "w")
    for _, s in ipairs(lineas) do f:write(s .. "\n") end
    f:close()
end)
