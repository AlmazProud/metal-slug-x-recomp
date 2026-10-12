-- Huellas por cuadro para la verificación diferencial (Fase 4).
--
-- Por cada cuadro N (screen:frame_number() en el notificador de cuadro) escribe:
--   N <hash RAM> <hash paleta>
-- con FNV-1a de 32 bits sobre los bytes de 0x100000-0x10FFFF y 0x400000-0x401FFF (banco visible),
-- en orden de dirección. El port (--huellas) calcula lo mismo.
--
-- Además registra las entradas que el 68000 leyó en cada cuadro (en MAME no cambian dentro de un
-- cuadro): E <cuadro del port> <P1> <P2> <SISTEMA> <MONEDAS> <TEST>, en hexadecimal, los bytes
-- tal como los ve el bus (activos en bajo). El cuadro del port es frame_number() + 1: MAME
-- etiqueta N a la imagen que se completa al ejecutar el cuadro N del port, y durante esa ejecución
-- frame_number() vale N - 1 (verificado con capturas y trazas, docs/FASE4_VERIFICACION.md).
--
-- Además, en cada reconocimiento de la VBlank (escritura en 0x3C000C con el bit 2, al entrar al
-- manejador) escribe "V <n> <hash RAM> <hash paleta>", con n = orden de aparición desde el
-- encendido. Ahí ambos lados están en el límite de una instrucción: no hay escrituras a medio
-- hacer, a diferencia del límite del cuadro, donde MAME puede quedar a mitad de una instrucción.
--
-- Con PD_PIXELES=1, además, por cada cuadro N: "F <N> <hash de la imagen>" (320x224 píxeles de
-- 32 bits de screen:pixels(); hash h = h*31 + palabra de 64 bits, con desborde natural).
--
-- Variables: PD_HUELLAS (salida), PD_DESDE / PD_HASTA (cuadros con huella; por defecto todos),
-- PD_VOLCAR (lista de cuadros cuya RAM se guarda entera en PD_DIR_VOLCADOS/ram_mame_NNNNNN.bin).

-- PD_SCRIPT_ENTRADAS: ruta de entradas.lua para registrar también los cambios de entradas.
if os.getenv("PD_SCRIPT_ENTRADAS") then dofile(os.getenv("PD_SCRIPT_ENTRADAS")) end

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local scr = manager.machine.screens[":screen"]
local salida = io.open(os.getenv("PD_HUELLAS") or "huellas_mame.txt", "w")
local desde = tonumber(os.getenv("PD_DESDE") or "0")
local hasta = tonumber(os.getenv("PD_HASTA") or "1e12")
local volcar = {}
for n in string.gmatch(os.getenv("PD_VOLCAR") or "", "%d+") do volcar[tonumber(n)] = true end
local dir_volcados = os.getenv("PD_DIR_VOLCADOS") or "."

local function fnv(s)
    local h = 0x811C9DC5
    for i = 1, #s, 4 do
        local a, b, c, d = string.byte(s, i, i + 3)
        h = ((h ~ a) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ b) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ c) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ d) * 0x01000193) & 0xFFFFFFFF
    end
    return h
end

-- Entradas leídas en el cuadro en curso.
local actual = { -1, -1, -1, -1, -1 }
local previo = { 0xFF, 0xFF, 0xFF, 0x1F, 0x80 }
local cuadro_entradas = -1

local function cerrar_entradas()
    if cuadro_entradas < 0 then return end
    for i = 1, 5 do
        if actual[i] >= 0 then previo[i] = actual[i] end
        actual[i] = -1
    end
    salida:write(string.format("E %d %02X %02X %02X %02X %02X\n", cuadro_entradas + 1,
        previo[1], previo[2], previo[3], previo[4], previo[5]))
end

local function anotar(i, v)
    local f = scr:frame_number()
    if f ~= cuadro_entradas then
        cerrar_entradas()
        cuadro_entradas = f
    end
    if actual[i] < 0 then actual[i] = v end
end

rtap_e1 = mem:install_read_tap(0x300000, 0x31FFFF, "pd_e1", function(offset, data, mask)
    if offset & 0x80 ~= 0 then
        if mask & 0x00FF ~= 0 then anotar(5, data & 0xFF) end
    elseif mask & 0xFF00 ~= 0 then
        anotar(1, (data >> 8) & 0xFF)
    end
end)
rtap_e2 = mem:install_read_tap(0x340000, 0x35FFFF, "pd_e2", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then anotar(2, (data >> 8) & 0xFF) end
end)
rtap_e3 = mem:install_read_tap(0x380000, 0x39FFFF, "pd_e3", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then anotar(3, (data >> 8) & 0xFF) end
end)
rtap_e4 = mem:install_read_tap(0x320000, 0x33FFFF, "pd_e4", function(offset, data, mask)
    if mask & 0x00FF ~= 0 then anotar(4, data & 0x3F) end      -- sin TP ni DATA OUT del reloj
end)

local vblanks = 0
local volcar_v = {}
for n in string.gmatch(os.getenv("PD_VOLCAR_V") or "", "%d+") do volcar_v[tonumber(n)] = true end
wtap_ack = mem:install_write_tap(0x3C000C, 0x3C000D, "pd_ack", function(offset, data, mask)
    if (data & mask & 0x0004) == 0 then return end
    vblanks = vblanks + 1
    local ram = mem:read_range(0x100000, 0x10FFFF, 8)
    local pal = mem:read_range(0x400000, 0x401FFF, 8)
    salida:write(string.format("V %d %08X %08X f=%d pc=%06X\n", vblanks, fnv(ram), fnv(pal), scr:frame_number(), cpu.state["PC"].value))
    if volcar_v[vblanks] then
        local v = io.open(string.format("%s/ram_mame_v%06d.bin", dir_volcados, vblanks), "wb")
        v:write(ram)
        v:close()
    end
end)

suscripcion_huellas = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if f < desde or f > hasta then return end
    local ram = mem:read_range(0x100000, 0x10FFFF, 8)
    local pal = mem:read_range(0x400000, 0x401FFF, 8)
    salida:write(string.format("%d %08X %08X\n", f, fnv(ram), fnv(pal)))
    if os.getenv("PD_PIXELES") then
        local px = scr:pixels()
        local h = 0
        for i = 1, #px, 8 do h = h * 31 + string.unpack("<i8", px, i) end
        salida:write(string.format("F %d %016X\n", f, h))
    end
    if os.getenv("PD_REGISTROS") then
        local s = cpu.state
        local r = {}
        for _, n in ipairs({"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "SP"}) do
            table.insert(r, string.format("%08X", s[n].value))
        end
        salida:write(string.format("R %d %06X %04X %s\n", f, s["PC"].value, s["SR"].value, table.concat(r, " ")))
    end
    if volcar[f] then
        local v = io.open(string.format("%s/ram_mame_%06d.bin", dir_volcados, f), "wb")
        v:write(ram)
        v:close()
    end
end)

notificador_fin_huellas = emu.add_machine_stop_notifier(function()
    cerrar_entradas()
    salida:close()
end)
