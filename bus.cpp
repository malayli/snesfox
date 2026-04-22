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

static constexpr uint64_t CYCLES_PER_SCANLINE = 114; // ~30000 / 262
static constexpr uint16_t H_TOTAL   = 340;
static constexpr uint16_t V_TOTAL   = 262;
static constexpr uint16_t VBLANK_START = 225;

void Bus::stepPeripherals(uint64_t totalCycles) {
    const uint64_t delta = totalCycles - m_lastCycles;
    m_lastCycles = totalCycles;

    m_cycleAccum += delta;
    while (m_cycleAccum >= CYCLES_PER_SCANLINE) {
        m_cycleAccum -= CYCLES_PER_SCANLINE;
        m_vCounter++;
        if (m_vCounter >= V_TOTAL) {
            m_vCounter = 0;
        }
    }
    m_hCounter = static_cast<uint16_t>((m_cycleAccum * H_TOTAL) / CYCLES_PER_SCANLINE);

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

    // V/H counter latch reads ($213C OPHCT, $213D OPVCT)
    if (addr == 0x213C) {
        const uint8_t result = m_hvcLatch ? static_cast<uint8_t>(m_hCounter >> 8) : static_cast<uint8_t>(m_hCounter & 0xFF);
        m_hvcLatch = !m_hvcLatch;
        return result;
    }
    if (addr == 0x213D) {
        const uint8_t result = m_hvcLatch ? static_cast<uint8_t>(m_vCounter >> 8) : static_cast<uint8_t>(m_vCounter & 0xFF);
        m_hvcLatch = !m_hvcLatch;
        return result;
    }
    if (addr == 0x213E) return 0x01;
    if (addr == 0x213F) { m_hvcLatch = false; return 0x02; }

    // NMI flag — bit 7 set at VBlank, cleared on read; bits 3:0 = CPU version (2)
    if (addr == 0x4210) {
        const uint8_t result = (m_nmiFlag ? 0x80 : 0x00) | 0x02;
        m_nmiFlag = false;
        return result;
    }

    // HVBJOY — bit 7 = VBlank, bit 6 = HBlank, bit 0 = auto-joypad busy
    if (addr == 0x4212) {
        uint8_t status = 0x00;
        if (m_vCounter >= VBLANK_START) status |= 0x80;
        if (m_hCounter >= 274)          status |= 0x40;
        return status;
    }

    // Joypad registers
    if (addr == 0x4218 || addr == 0x4219) {
        return 0x00;
    }

    // Multiply/divide result registers
    if (addr == 0x4214) return static_cast<uint8_t>(m_rddiv & 0xFF);
    if (addr == 0x4215) return static_cast<uint8_t>(m_rddiv >> 8);
    if (addr == 0x4216) return static_cast<uint8_t>(m_rdmpy & 0xFF);
    if (addr == 0x4217) return static_cast<uint8_t>(m_rdmpy >> 8);

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
    // Hardware multiply/divide unit
    // ------------------------------------------------------------
    if (addr == 0x4202) { m_wrmpya = value; return; }
    if (addr == 0x4203) {
        m_rdmpy = static_cast<uint16_t>(m_wrmpya) * static_cast<uint16_t>(value);
        m_rddiv = 0;
        return;
    }
    if (addr == 0x4204) { m_wrdiv = (m_wrdiv & 0xFF00) | value; return; }
    if (addr == 0x4205) { m_wrdiv = (m_wrdiv & 0x00FF) | (static_cast<uint16_t>(value) << 8); return; }
    if (addr == 0x4206) {
        if (value == 0) {
            m_rddiv = 0xFFFF;
            m_rdmpy = m_wrdiv;
        } else {
            m_rddiv = m_wrdiv / value;
            m_rdmpy = m_wrdiv % value;
        }
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