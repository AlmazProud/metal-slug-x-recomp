-- Dumps MAME memory regions to files and exits, right after the machine starts (the cartridge
-- decryption already done). Used to observe what MAME builds from a given set of ROM files.
--
-- PD_REGIONS: list of "tag=file" separated by ";", e.g.
--   ":cslot1:maincpu=C:\out\maincpu.bin;:cslot1:fixed=C:\out\fixed.bin"
-- With PD_LIST=1 it also prints every region tag and size.
-- Bytes are written in the order of the ROM files MAME loaded into the region. In a big-endian
-- region (the 68000 program) read_u32 composes two 16-bit words big-endian and each word is
-- written back low byte first (checked: the NEO-SMA chip ROM, loaded unencrypted at 0xC0000,
-- comes out byte-identical to its file). Little-endian regions are written as read (checked:
-- the L0 zoom table comes out identical to 000-lo.lo).

local regions = manager.machine.memory.regions

if os.getenv("PD_LIST") == "1" then
    for tag, r in pairs(regions) do print(string.format("REGION %s size=%d", tag, r.size)) end
end

for item in string.gmatch(os.getenv("PD_REGIONS") or "", "[^;]+") do
    local tag, path = string.match(item, "^(.-)=(.+)$")
    local r = regions[tag]
    if not r then
        print("PD_DUMP_REGION missing " .. tag)
    else
        local f = assert(io.open(path, "wb"))
        local chunk = {}
        local big = r.endianness == "big"
        for off = 0, r.size - 1, 4 do
            local v = r:read_u32(off)
            if big then
                chunk[#chunk + 1] = string.pack("<I2<I2", v >> 16, v & 0xFFFF)
            else
                chunk[#chunk + 1] = string.pack("<I4", v)
            end
            if #chunk == 16384 then f:write(table.concat(chunk)); chunk = {} end
        end
        f:write(table.concat(chunk))
        f:close()
        print(string.format("PD_DUMP_REGION %s %d bytes -> %s", tag, r.size, path))
    end
end
manager.machine:exit()
