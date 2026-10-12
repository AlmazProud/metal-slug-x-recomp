-- Traza de lecturas de E/S del 68000 (0x300000-0x3FFFFF y 0x800000-0xBFFFFF): para cada
-- (dirección, máscara, value) cuenta cuántas veces se leyó y en qué cuadro apareció primero.
-- Sirve para fijar los valores de los puertos del runtime contra el oráculo.
-- Salida: PD_TRAZA (por defecto traza_puertos.txt).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local scr = manager.machine.screens[":screen"]
local vistos, orden = {}, {}

local function anotar(offset, data, mask)
    local k = string.format("%06X m=%04X v=%04X", offset, mask, data & mask)
    local e = vistos[k]
    if not e then
        e = { n = 0, f = scr:frame_number(), pc = cpu.state["PC"].value }
        vistos[k] = e
        table.insert(orden, k)
    end
    e.n = e.n + 1
end

rtap_io = mem:install_read_tap(0x300000, 0x3BFFFF, "pd_io_r", anotar)
rtap_mc = mem:install_read_tap(0x800000, 0xBFFFFF, "pd_mc_r", anotar)

notificador_fin_io = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRAZA") or "traza_puertos.txt", "w")
    for _, k in ipairs(orden) do
        local e = vistos[k]
        f:write(string.format("%s n=%d primer_cuadro=%d pc=%06X\n", k, e.n, e.f, e.pc))
    end
    f:close()
end)
