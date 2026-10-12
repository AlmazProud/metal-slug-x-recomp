-- When the machine stops: prints the frame number and a hash of the 68000 work RAM
-- (0x100000-0x10FFFF) and saves a snapshot. Used to verify that an input playback
-- (-playback) is deterministic: two runs must give the same hash.

local mem = manager.machine.devices[":maincpu"].spaces["program"]

final_state_sub = emu.add_machine_stop_notifier(function()
    -- 32-bit FNV-1a over the RAM, word by word
    local h = 0x811C9DC5
    for a = 0x100000, 0x10FFFE, 2 do
        local w = mem:read_u16(a)
        h = ((h ~ (w >> 8)) * 0x01000193) & 0xFFFFFFFF
        h = ((h ~ (w & 0xFF)) * 0x01000193) & 0xFFFFFFFF
    end
    local f = manager.machine.screens[":screen"]:frame_number()
    print(string.format("PD_STATE frame=%d ram_fnv=%08X", f, h))
    manager.machine.video:snapshot()
end)
