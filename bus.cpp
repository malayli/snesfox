#include "bus.hpp"

Bus::Bus(const std::vector<uint8_t>& rom)
    : m_rom(rom) {
    reset();
}

void Bus::reset() {
    m_wram.fill(0);
    m_apu.reset();
}

void Bus::stepPeripherals() {
    m_apu.step();
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

    // NMI register
    if (addr == 0x4210) {
        return 0x80;
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
    // DMA trigger ignored for now
    // ------------------------------------------------------------
    if (addr == 0x420B) {
        return;
    }

    // ROM area ignored on write
}