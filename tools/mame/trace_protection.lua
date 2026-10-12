-- Traza de la protección de mslugx (0x2FFFE0-0x2FFFEF) y verificación contra el modelo
-- documentado en docs/ROM_MAP.md. Uso:
--   mame mslugx -bios us -autoboot_script tools/mame/trace_protection.lua ...
-- Al salir escribe un resumen en el archivo indicado por la variable de entorno PD_TRAZA
-- (por defecto traza_proteccion.txt en el directorio actual).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]

-- Modelo independiente (documentación, no el dispositivo de MAME)
local modelo = { command = 0, counter = 0 }
local stats = { escrituras = {}, lecturas = 0, ok = 0, mal = 0, cmds = {}, pcs = {}, ejemplos = {} }

local function bit_tabla(indice)
    local byte = mem:read_u8(0xDEDD2 + ((indice >> 3) & 0xFFF))
    return (byte >> (7 - (indice & 7))) & 1
end

local function esperado()
    if modelo.command == 0x0001 then
        local r = bit_tabla(modelo.counter)
        modelo.counter = modelo.counter + 1
        return r
    elseif modelo.command == 0x0FFF then
        return bit_tabla(mem:read_u16(0x10F00A) - 1)
    end
    return 0
end

wtap = mem:install_write_tap(0x2FFFE0, 0x2FFFEF, "pd_prot_w", function(offset, data, mask)
    local off = offset - 0x2FFFE0
    stats.escrituras[off] = (stats.escrituras[off] or 0) + 1
    if off == 0x0 then modelo.command = 0
    elseif off == 0x2 or off == 0x4 then modelo.command = modelo.command | data
    elseif off == 0xA then modelo.counter = 0; modelo.command = 0 end
end)

rtap = mem:install_read_tap(0x2FFFE0, 0x2FFFEF, "pd_prot_r", function(offset, data, mask)
    stats.lecturas = stats.lecturas + 1
    local cmd = modelo.command
    stats.cmds[cmd] = (stats.cmds[cmd] or 0) + 1
    local pc = cpu.state["PC"].value
    stats.pcs[pc] = (stats.pcs[pc] or 0) + 1
    local e = esperado()
    if (data & 1) == e then stats.ok = stats.ok + 1
    else
        stats.mal = stats.mal + 1
        if #stats.ejemplos < 10 then
            table.insert(stats.ejemplos, string.format("pc=%06X cmd=%04X leido=%d esperado=%d", pc, cmd, data & 1, e))
        end
    end
end)

-- La suscripción debe guardarse en una variable global; si no, Lua la recolecta y nunca se llama.
notificador_fin = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "traza_proteccion.txt", "w")
    f:write(string.format("lecturas=%d coinciden=%d difieren=%d\n", stats.lecturas, stats.ok, stats.mal))
    for off, n in pairs(stats.escrituras) do f:write(string.format("escrituras en +%X: %d\n", off, n)) end
    for c, n in pairs(stats.cmds) do f:write(string.format("lecturas con command=%04X: %d\n", c, n)) end
    local npc = 0
    for pc, n in pairs(stats.pcs) do
        npc = npc + 1
        if npc <= 15 then f:write(string.format("lectura desde pc=%06X: %d\n", pc, n)) end
    end
    f:write(string.format("PCs distintos que leen: %d\n", npc))
    for _, s in ipairs(stats.ejemplos) do f:write("DIFERENCIA " .. s .. "\n") end
    f:close()
end)
