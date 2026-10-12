-- Ciclos reales del 68000 de MAME por instrucción (requiere -debug -debugger none).
--
-- Activa la traza del depurador entre los cuadros PD_DESDE y PD_HASTA. Cada línea lleva
-- delante el contador total de ciclos de la CPU ("C=<totalcycles>"), así la diferencia entre
-- dos líneas consecutivas es el costo real de la instrucción (más la entrada a una interrupción
-- cuando el PC salta al manejador). Salida: PD_TRAZA (archivo de traza del depurador).

local dbg = manager.machine.debugger
local scr = manager.machine.screens[":screen"]
local desde = tonumber(os.getenv("PD_DESDE") or "100")
local hasta = tonumber(os.getenv("PD_HASTA") or "102")
local ruta = os.getenv("PD_TRAZA") or "ciclos.txt"
local cpu_traza = os.getenv("PD_CPU") or "maincpu"   -- "audiocpu" para el Z80
local activa = false

-- PD_DESDE=-1: traza desde el encendido (el script corre antes de la primera instrucción).
if desde < 0 then
    dbg:command(string.format('trace %s,%s,noloop,{tracelog "C=%%d ",totalcycles}', ruta, cpu_traza))
    activa = true
end

suscripcion_ciclos = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if not activa and desde >= 0 and f >= desde and f < hasta then
        dbg:command("focus " .. cpu_traza)           -- tracelog escribe en la CPU con el foco
        dbg:command(string.format('trace %s,%s,noloop,{tracelog "C=%%d ",totalcycles}', ruta, cpu_traza))
        activa = true
    elseif activa and f >= hasta then
        dbg:command("trace off," .. cpu_traza)
        activa = false
        manager.machine:exit()
    end
end)
