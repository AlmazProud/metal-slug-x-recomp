-- YM2610 probe: drives the chip directly, with the sound CPU stopped, to learn its behaviour as
-- a black box (the output is recorded with -wavwrite). Needs -debug -debugger none.
--
-- PD_PROBE: script, one write per line, "<frame> <bank> <register> <value>" (hex register and
-- value, bank 0 or 1); "<frame> suspend" stops the Z80 (the debugger's suspend command).
-- PD_LOG: for every frame with writes, "<frame> <machine time in us> <Z80 PC>", to place the
-- writes in the recording.

local dbg = manager.machine.debugger
local scr = manager.machine.screens[":screen"]
local z80 = manager.machine.devices[":audiocpu"]
local ports = z80.spaces["io"]
local log = io.open(os.getenv("PD_LOG"), "w")

local steps = {}
local last = 0
for line in io.lines(os.getenv("PD_PROBE")) do
    local f, rest = line:match("^%s*(%d+)%s+(.*)$")
    if f then
        f = tonumber(f)
        steps[f] = steps[f] or {}
        table.insert(steps[f], rest)
        if f > last then last = f end
    end
end

probe_sub = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    local list = steps[f]
    if list then
        log:write(string.format("%d %.3f %04X\n", f, manager.machine.time:as_double() * 1e6, z80.state["PC"].value))
        log:flush()
        for _, s in ipairs(list) do
            if s == "suspend" then
                dbg:command("suspend " .. z80.tag)
            else
                local bank, reg, val = s:match("(%d)%s+(%x+)%s+(%x+)")
                bank, reg, val = tonumber(bank), tonumber(reg, 16), tonumber(val, 16)
                ports:write_u8(4 + 2 * bank, reg)
                ports:write_u8(5 + 2 * bank, val)
            end
        end
    end
    if f > last + 120 then manager.machine:exit() end
end)
