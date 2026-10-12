-- Vuelca el estado del 68000 y de la memoria de trabajo en un frame dado y cierra MAME.
-- Lo usa la prueba de lockstep para arrancar desde un punto real del juego (en vez del reset).
--
-- Variables de entorno: PD_FRAME (frame en el que volcar), PD_ESTADO (archivo de salida).
-- Formato (big-endian): "PDEST1", 19 u32 (D0-D7, A0-A6, SP, USP, SR, PC), 128 bytes de la zona
-- de vectores (0x000000), 16 bytes de la ventana de P2 (0x200000, para deducir el banco),
-- 64 KB de RAM (0x100000), 64 KB de SRAM (0xD00000), 8 KB de paleta (0x400000).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local pantalla = manager.machine.screens[":screen"]
local objetivo = tonumber(os.getenv("PD_FRAME") or "3000")
local salida = os.getenv("PD_ESTADO") or "estado.bin"

local function bloque(base, n)
    local t = {}
    for i = 0, n - 1 do t[#t + 1] = string.char(mem:read_u8(base + i)) end
    return table.concat(t)
end

sub_volcado = emu.add_machine_frame_notifier(function()
    if pantalla:frame_number() ~= objetivo then return end
    local f = io.open(salida, "wb")
    f:write("PDEST1")
    for _, r in ipairs({"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
                        "A0", "A1", "A2", "A3", "A4", "A5", "A6", "SP", "USP", "SR", "PC"}) do
        f:write(string.pack(">I4", cpu.state[r].value & 0xFFFFFFFF))
    end
    f:write(bloque(0x000000, 128))
    f:write(bloque(0x200000, 16))
    f:write(bloque(0x100000, 0x10000))
    f:write(bloque(0xD00000, 0x10000))
    f:write(bloque(0x400000, 0x2000))
    f:close()
    print(string.format("PD_VOLCADO frame=%d pc=%06X sr=%04X", objetivo,
                        cpu.state["PC"].value, cpu.state["SR"].value))
    manager.machine:exit()
end)
