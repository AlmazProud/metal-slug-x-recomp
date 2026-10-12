-- Entradas de una reproducción (-playback) como CAMBIOS EN EL TIEMPO, para el port (Fase 4).
--
-- Por cada puerto de entrada registra cuándo cambia el value que lee el 68000:
--   C <us de la última lectura con el value anterior> <us de la primera con el nuevo> <puerto> <value>
-- puertos: P1 (byte alto de 300000), P2 (340000), SIS (380000), MON (bits 0-5 de 320001),
-- TEST (300081). El port aplica cada cambio entre esos dos instantes (opción --inputs); como
-- ejecuta las mismas lecturas en los mismos cycles, lee exactamente lo mismo que MAME.
-- (Asociar las inputs a frame_number() no sirve: dentro de un tap no avanza como el cuadro.)
-- Variable: PD_ENTRADAS (output).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local output = io.open(os.getenv("PD_ENTRADAS") or "entradas_mame.txt", "w")
local ultimo = { P1 = 0xFF, P2 = 0xFF, SIS = 0xFF, MON = 0x1F, TEST = 0x80 }
local ultima_t = { P1 = 0, P2 = 0, SIS = 0, MON = 0, TEST = 0 }

local function ahora() return manager.machine.time:as_double() * 1e6 end

local function anotar(p, v)
    local t = ahora()
    if v ~= ultimo[p] then
        output:write(string.format("C %.3f %.3f %s %02X\n", ultima_t[p], t, p, v))
        ultimo[p] = v
    end
    ultima_t[p] = t
end

rtap_ent1 = mem:install_read_tap(0x300000, 0x31FFFF, "pd_ent1", function(offset, data, mask)
    if offset & 0x80 ~= 0 then
        if mask & 0x00FF ~= 0 then anotar("TEST", data & 0xFF) end
    elseif mask & 0xFF00 ~= 0 then
        anotar("P1", (data >> 8) & 0xFF)
    end
end)
rtap_ent2 = mem:install_read_tap(0x340000, 0x35FFFF, "pd_ent2", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then anotar("P2", (data >> 8) & 0xFF) end
end)
rtap_ent3 = mem:install_read_tap(0x380000, 0x39FFFF, "pd_ent3", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then anotar("SIS", (data >> 8) & 0xFF) end
end)
rtap_ent4 = mem:install_read_tap(0x320000, 0x33FFFF, "pd_ent4", function(offset, data, mask)
    if mask & 0x00FF ~= 0 then anotar("MON", data & 0x3F) end
end)

notificador_fin_ent = emu.add_machine_stop_notifier(function() output:close() end)
