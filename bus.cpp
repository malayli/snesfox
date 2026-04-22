#include "bus.hpp"

Bus::Bus(const std::vector<uint8_t>& rom)
    : m_rom(rom) {
    m_mapMode = HeaderParser::detect(m_rom);

    const size_t sramOffset = (m_mapMode == RomMapping::HiROM) ? 0xFFD8 : 0x7FD8;
    if (sramOffset < m_rom.size()) {
        static const size_t kSramTable[] = { 0, 2*1024, 8*1024, 32*1024, 128*1024 };
        const uint8_t raw = m_rom[sramOffset];
        m_sramBytes = (raw < 5) ? kSramTable[raw] : 0;
    }

    reset();
}

void Bus::reset() {
    m_wram.fill(0);
    m_apu.reset();
}

void Bus::stepPeripherals(uint64_t totalCycles) {
    (void)totalCycles;
    m_apu.step();
}

RomMapping Bus::mapMode() const { return m_mapMode; }
size_t Bus::sramBytes() const { return m_sramBytes; }

bool Bus::onVBlank() {
    m_nmiFlag = true;
    return m_nmiEnabled;
}

bool Bus::isLoRomArea(uint8_t bank, uint16_t addr) const {
    (void)bank;
    return addr >= 0x8000;
}

uint32_t Bus::loRomToFileOffset(uint8_t bank, uint16_t addr) const {
    return (static_cast<uint32_t>(bank & 0x7F) << 15)
         | static_cast<uint32_t>(addr - 0x8000);
}

uint8_t Bus::read(uint8_t bank, uint16_t addr) const {
    // ------------------------------------------------------------
    // WRAM full banks
    // ------------------------------------------------------------
    if (bank == 0x7E) {
        return m_wram[addr];
    }

    if (bank == 0x7F) {
        return m_wram[0x10000 + addr];
    }

    // ------------------------------------------------------------
    // WRAM mirrors in low banks
    // ------------------------------------------------------------
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) && addr <= 0x1FFF) {
        return m_wram[addr];
    }

    // ------------------------------------------------------------
    // Hardware patches / temporary bring-up hacks
    // ------------------------------------------------------------

    // APU I/O ports ($2140-$2143) ignored for now
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) &&
        addr >= 0x2140 && addr <= 0x2143) {
        return 0x00;
    }

    // NMI flag — bit 7 set at VBlank, cleared on read; bits 3:0 = CPU version (2)
    if (addr == 0x4210) {
        const uint8_t result = (m_nmiFlag ? 0x80 : 0x00) | 0x02;
        m_nmiFlag = false;
        return result;
    }

    // VBlank / HVBJOY status
    if (addr == 0x4212) {
        return 0x80;
    }

    // Joypad registers
    if (addr == 0x4218 || addr == 0x4219) {
        return 0x00;
    }

    // ------------------------------------------------------------
    // LoROM ROM area
    // ------------------------------------------------------------
    if (isLoRomArea(bank, addr)) {
        const uint32_t offset = loRomToFileOffset(bank, addr);
        if (offset < m_rom.size()) {
            return m_rom[offset];
        }
        return 0xFF;
    }

    return 0x00;
}

void Bus::write(uint8_t bank, uint16_t addr, uint8_t value) {
    // ------------------------------------------------------------
    // WRAM full banks
    // ------------------------------------------------------------
    if (bank == 0x7E) {
        m_wram[addr] = value;
        return;
    }

    if (bank == 0x7F) {
        m_wram[0x10000 + addr] = value;
        return;
    }

    // ------------------------------------------------------------
    // WRAM mirrors
    // ------------------------------------------------------------
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) && addr <= 0x1FFF) {
        m_wram[addr] = value;
        return;
    }

    // ------------------------------------------------------------
    // APU I/O ports
    // ------------------------------------------------------------
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) &&
        addr >= 0x2140 && addr <= 0x2143) {
        m_apu.writePort(addr, value);
        return;
    }

    // ------------------------------------------------------------
    // NMITIMEN — NMI/IRQ/auto-joypad enable
    // ------------------------------------------------------------
    if (addr == 0x4200) {
        m_nmiEnabled = (value >> 7) & 1;
        return;
    }

    // ------------------------------------------------------------
    // DMA trigger ignored for now
    // ------------------------------------------------------------
    if (addr == 0x420B) {
        return;
    }

    // ROM area ignored on write
}