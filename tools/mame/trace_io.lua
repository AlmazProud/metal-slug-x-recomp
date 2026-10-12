-- Traza de escrituras del 68000 en E/S (0x300000-0x3FFFFF) y palette (0x400000-0x7FFFFF) dentro
-- de una window de tiempo, con la hour de la máquina (us), el PC y el dato. Sirve para encontrar la primera divergencia
-- de temporización contra el port (src/neogeo, opción --traza-es).
-- Variables: PD_DESDE_US, PD_HASTA_US, PD_TRAZA (output).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local desde = tonumber(os.getenv("PD_DESDE_US") or "0")
local hasta = tonumber(os.getenv("PD_HASTA_US") or "1e12")
local lineas = {}

local function anotar_es(offset, data, mask)
    local t = manager.machine.time:as_double() * 1e6
    if t >= desde and t <= hasta then
        table.insert(lineas, string.format("%.3f pc=%06X %06X %04X/%04X", t, cpu.state["PC"].value, offset, data & mask, mask))
    end
end

wtap_es = mem:install_write_tap(0x300000, 0x3FFFFF, "pd_es_w", anotar_es)
wtap_pal = mem:install_write_tap(0x400000, 0x7FFFFF, "pd_pal_w", anotar_es)

notificador_fin_es = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "traza_es.txt", "w")
    for _, s in ipairs(lineas) do f:write(s .. "\n") end
    f:close()
end)
