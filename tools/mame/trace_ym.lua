-- Escrituras del Z80 en sus puertos (YM2610 y respuesta al 68000) dentro de una ventana de tiempo:
-- hora (us), puerto (byte bajo) y dato. Mismo formato que PD_TRAZA_YM del port.
-- Variables: PD_DESDE_US, PD_HASTA_US, PD_TRAZA.
local z80 = manager.machine.devices[":audiocpu"]
local io_z = z80.spaces["io"]
local desde = tonumber(os.getenv("PD_DESDE_US") or "0")
local hasta = tonumber(os.getenv("PD_HASTA_US") or "1e12")
local lineas = {}
wtap_ym = io_z:install_write_tap(0x0000, 0xFFFF, "pd_ym", function(offset, data, mask)
    local t = manager.machine.time:as_double() * 1e6
    if t >= desde and t <= hasta then
        table.insert(lineas, string.format("%.3f %02X %02X", t, offset & 0xFF, data & 0xFF))
    end
end)
notificador_fin_ym = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "traza_ym.txt", "w")
    for _, s in ipairs(lineas) do f:write(s .. "\n") end
    f:close()
end)
