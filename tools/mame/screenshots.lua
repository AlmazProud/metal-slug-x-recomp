-- Capturas de pantalla de MAME en cuadros concretos, para compararlas con el port.
-- Variables de entorno: PD_CUADROS = lista separada por comas; PD_PREFIJO = prefijo del nombre
-- (las imágenes quedan en el -snapshot_directory como <prefijo>_NNNNNN.png).

local scr = manager.machine.screens[":screen"]
local pedidos = {}
for n in string.gmatch(os.getenv("PD_CUADROS") or "", "%d+") do pedidos[tonumber(n)] = true end
local prefijo = os.getenv("PD_PREFIJO") or "mame"

suscripcion_capturas = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if pedidos[f] then
        scr:snapshot(string.format("%s_%06d.png", prefijo, f))
    end
end)
