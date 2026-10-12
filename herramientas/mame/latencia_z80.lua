-- Latencia del Z80: tiempo entre cada comando del 68000 (escritura en 0x320000) y cada
-- respuesta del Z80 (escritura en su puerto 0x0C), con la hora de la máquina en microsegundos.
-- Salida: PD_TRAZA (por defecto latencia_z80.txt).

local cpu = manager.machine.devices[":maincpu"]
local z80 = manager.machine.devices[":audiocpu"]
local mem = cpu.spaces["program"]
local io_z80 = z80.spaces["io"]
local scr = manager.machine.screens[":screen"]
local lineas = {}
local function vpos()
    local ok, v = pcall(function() return scr:vpos() end)
    return ok and v or -1
end

local function us()
    local ok, v = pcall(function() return manager.machine.time:as_double() * 1e6 end)
    if ok then return v end
    table.insert(lineas, "ERROR tiempo: " .. tostring(v))
    return -1
end

wtap_cmd = mem:install_write_tap(0x320000, 0x320001, "pd_cmd", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then
        table.insert(lineas, string.format("%.3f f=%d v=%d CMD %02X", us(), scr:frame_number(), vpos(), (data >> 8) & 0xFF))
    end
end)

wtap_resp = io_z80:install_write_tap(0x0000, 0xFFFF, "pd_resp", function(offset, data, mask)
    if offset & 0xFF ~= 0x0C then return end
    table.insert(lineas, string.format("%.3f f=%d v=%d RESP %02X", us(), scr:frame_number(), vpos(), data & 0xFF))
end)

-- Primera lectura del 68000 que ve cada valor nuevo de la respuesta (puede llegar más tarde que la
-- escritura del Z80 por los tramos de ejecución del planificador de MAME).
local visto = -1
rtap_resp = mem:install_read_tap(0x320000, 0x320001, "pd_resp_r", function(offset, data, mask)
    if mask & 0xFF00 == 0 then return end
    local v = (data >> 8) & 0xFF
    if v ~= visto then
        table.insert(lineas, string.format("%.3f f=%d v=%d VISTO %02X", us(), scr:frame_number(), vpos(), v))
        visto = v
    end
end)

notificador_fin_lat = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "latencia_z80.txt", "w")
    for _, s in ipairs(lineas) do f:write(s .. "\n") end
    f:close()
end)
