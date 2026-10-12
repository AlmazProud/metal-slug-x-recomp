-- Trace of the 68000 <-> Z80 communication (sound latch at 0x320000) to document what
-- the BIOS and the game expect. Usage:
--   mame mslugx -bios us -autoboot_script tools/mame/trace_sound.lua ...
-- Writes to PD_TRACE (traza_sonido.txt by default): every command written by the 68000 and every
-- change of the value the 68000 reads back as the reply (high byte of 0x320000), with frame and PC.

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local scr = manager.machine.screens[":screen"]
local lines = {}
local last_reply = -1

local function frame() return scr:frame_number() end

wtap_snd = mem:install_write_tap(0x320000, 0x320001, "pd_snd_w", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then
        table.insert(lines, string.format("f=%d pc=%06X CMD %02X", frame(), cpu.state["PC"].value, (data >> 8) & 0xFF))
    end
end)

rtap_snd = mem:install_read_tap(0x320000, 0x320001, "pd_snd_r", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then
        local v = (data >> 8) & 0xFF
        if v ~= last_reply then
            table.insert(lines, string.format("f=%d pc=%06X RESP %02X", frame(), cpu.state["PC"].value, v))
            last_reply = v
        end
    end
end)

sound_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "traza_sonido.txt", "w")
    for _, s in ipairs(lines) do f:write(s .. "\n") end
    f:close()
end)
