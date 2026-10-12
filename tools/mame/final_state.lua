-- Al terminar la máquina: imprime el número de frame y un hash de la RAM de trabajo del 68000
-- (0x100000-0x10FFFF) y guarda una captura. Sirve para verificar que una reproducción de
-- inputs (-playback) es determinista: dos corridas deben dar el mismo hash.

local mem = manager.machine.devices[":maincpu"].spaces["program"]

fin_estado = emu.add_machine_stop_notifier(function()
    -- FNV-1a de 32 bits sobre la RAM, palabra por palabra
    local h = 0x811C9DC5
    for a = 0x100000, 0x10FFFE, 2 do
        local w = mem:read_u16(a)
        h = ((h ~ (w >> 8)) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ (w & 0xFF)) * 0x01000193) & 0xFFFFFFFF
    end
    local f = manager.machine.screens[":screen"]:frame_number()
    print(string.format("PD_ESTADO frame=%d ram_fnv=%08X", f, h))
    manager.machine.video:snapshot()
end)
