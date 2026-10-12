-- Traza de la comunicación 68000 <-> Z80 (latch de sonido en 0x320000) para documentar qué
-- espera el BIOS y el juego. Uso:
--   mame mslugx -bios us -autoboot_script herramientas/mame/traza_sonido.lua ...
-- Escribe en PD_TRAZA (por defecto traza_sonido.txt): cada comando escrito por el 68000 y cada
-- cambio del valor que el 68000 lee como respuesta (byte alto de 0x320000), con cuadro y PC.

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local scr = manager.machine.screens[":screen"]
local lineas = {}
local ultima = -1

local function cuadro() return scr:frame_number() end

wtap_snd = mem:install_write_tap(0x320000, 0x320001, "pd_snd_w", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then
        table.insert(lineas, string.format("f=%d pc=%06X CMD %02X", cuadro(), cpu.state["PC"].value, (data >> 8) & 0xFF))
    end
end)

rtap_snd = mem:install_read_tap(0x320000, 0x320001, "pd_snd_r", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then
        local v = (data >> 8) & 0xFF
        if v ~= ultima then
            table.insert(lineas, string.format("f=%d pc=%06X RESP %02X", cuadro(), cpu.state["PC"].value, v))
            ultima = v
        end
    end
end)

notificador_fin_snd = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "traza_sonido.txt", "w")
    for _, s in ipairs(lineas) do f:write(s .. "\n") end
    f:close()
end)
