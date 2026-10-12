-- Cobertura dinámica del 68000 en MAME (requiere -debug -debugger none).
-- La lanza tools/coverage.py; no se usa sola.
--
-- Variables de entorno:
--   PD_TRAZA_DIR  carpeta (ruta SIN espacios, p. ej. 8.3) donde se escriben traza_NNNN.txt
--   PD_FRAMES     frames por archivo de traza (por defecto 300)
--   PD_JUGAR      1 = bot simple que inserta monedas y juega (por defecto 1)
--
-- Cada cambio de banco de P2 (escritura en 0x2FFFF0) se anota en la traza como
-- "BANK n" con tracelog, para que el analizador sepa qué banco está activo.

local dbg = manager.machine.debugger
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local pantalla = manager.machine.screens[":screen"]
local dir = os.getenv("PD_TRAZA_DIR")
local por_archivo = tonumber(os.getenv("PD_FRAMES") or "300")
local jugar = (os.getenv("PD_JUGAR") or "1") == "1"
local parte = 0

local function nueva_traza()
    dbg:command(string.format("trace %s\\traza_%04d.txt,maincpu,noloop", dir, parte))
    parte = parte + 1
end

banco_tap = mem:install_write_tap(0x2FFFF0, 0x2FFFFF, "pd_cov_bank", function(offset, data, mask)
    dbg:command(string.format('tracelog "BANK %d\\n"', data & 7))
end)

-- Lecturas sobre P1. Incluyen las búsquedas de instrucciones; coverage.py les resta los bytes
-- de las instrucciones ejecutadas para quedarse con las lecturas de datos.
local leidas = {}
lectura_tap = mem:install_read_tap(0x000000, 0x0FFFFF, "pd_cov_rd", function(offset, data, mask)
    leidas[offset & 0xFFFFE] = true
end)

-- Bot de entrada: no busca jugar bien, solo recorrer la mayor cantidad de código posible.
local campos = {}
for _, puerto in pairs(manager.machine.ioport.ports) do
    for nombre, campo in pairs(puerto.fields) do campos[nombre] = campo end
end
local function poner(nombre, v) if campos[nombre] then campos[nombre]:set_value(v) end end

cuadro_sub = emu.add_machine_frame_notifier(function()
    local f = pantalla:frame_number()
    if f % por_archivo == 0 then nueva_traza() end
    if not jugar or f < 600 then return end
    local c = f % 900                                  -- cada 15 s: moneda + START (continúa)
    poner("Coin 1", (c >= 0 and c < 6) and 1 or 0)
    poner("1 Player Start", (c >= 60 and c < 66) and 1 or 0)
    poner("P1 Right", (f % 240 < 200) and 1 or 0)     -- casi siempre a la derecha
    poner("P1 Left", (f % 240 >= 215) and 1 or 0)
    poner("P1 Up", (f % 500 >= 470) and 1 or 0)
    poner("P1 Button 1", (f % 6 < 3) and 1 or 0)      -- disparo
    poner("P1 Button 2", (f % 90 < 8) and 1 or 0)     -- salto
    poner("P1 Button 3", (f % 400 < 4) and 1 or 0)    -- granada
end)

fin_sub = emu.add_machine_stop_notifier(function()
    dbg:command("trace off,maincpu")
    local l = io.open(dir .. "\\lecturas_p1.txt", "w")
    for a in pairs(leidas) do l:write(string.format("%X\n", a)) end
    l:close()
    local f = io.open(dir .. "\\FIN", "w"); f:write(tostring(parte)); f:close()
end)
