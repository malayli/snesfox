#include "bus.hpp"
#include <fstream>

Bus::Bus(const std::vector<uint8_t>& rom, const std::string& savePath)
    : m_rom(rom), m_savePath(savePath) {
    m_mapMode = HeaderParser::detect(m_rom);

    const size_t sramOffset = (m_mapMode == RomMapping::HiROM) ? 0xFFD8 : 0x7FD8;
    if (sramOffset < m_rom.size()) {
        static const size_t kSramTable[] = { 0, 2*1024, 8*1024, 32*1024, 128*1024 };
        const uint8_t raw = m_rom[sramOffset];
        m_sramBytes = (raw < 5) ? kSramTable[raw] : 0;
    }

    if (m_sramBytes > 0) {
        m_sram.assign(m_sramBytes, 0x00);
        if (!m_savePath.empty()) {
            std::ifstream f(m_savePath, std::ios::binary);
            if (f) {
                f.read(reinterpret_cast<char*>(m_sram.data()),
                       static_cast<std::streamsize>(m_sram.size()));
            }
        }
    }

    reset();
}

Bus::~Bus() {
    if (m_sram.empty() || m_savePath.empty()) return;
    std::ofstream f(m_savePath, std::ios::binary);
    if (f) {
        f.write(reinterpret_cast<const char*>(m_sram.data()),
                static_cast<std::streamsize>(m_sram.size()));
    }
}

void Bus::reset() {
    m_wram.fill(0);
    m_apu.reset();
}

static constexpr uint64_t CYCLES_PER_SCANLINE = 114; // ~30000 / 262
static constexpr uint16_t H_TOTAL   = 340;
static constexpr uint16_t V_TOTAL   = 262;
static constexpr uint16_t VBLANK_START = 225;

bool Bus::stepPeripherals(uint64_t totalCycles) {
    const uint64_t delta = totalCycles - m_lastCycles;
    m_lastCycles = totalCycles;

    const uint16_t prevV = m_vCounter;
    const uint16_t prevH = m_hCounter;

    m_cycleAccum += delta;
    while (m_cycleAccum >= CYCLES_PER_SCANLINE) {
        m_cycleAccum -= CYCLES_PER_SCANLINE;
        m_vCounter++;
        if (m_vCounter >= V_TOTAL) {
            m_vCounter = 0;
            m_irqVMatch = false;
        }
    }
    m_hCounter = static_cast<uint16_t>((m_cycleAccum * H_TOTAL) / CYCLES_PER_SCANLINE);

    // IRQ condition
    if (m_irqMode != 0) {
        const bool newScanline = (m_vCounter != prevV);
        if (newScanline) {
            m_irqVMatch = (m_vCounter == m_vtime);
            if (m_irqMode == 2 && m_irqVMatch) {   // V-only: fire at scanline start
                m_irqFlag = true;
                m_irqPending = true;
            }
        }
        const bool hEdge = (prevH < m_htime && m_hCounter >= m_htime);
        if (hEdge) {
            if (m_irqMode == 1)                     // H-only
                { m_irqFlag = true; m_irqPending = true; }
            if (m_irqMode == 3 && m_irqVMatch)     // H+V
                { m_irqFlag = true; m_irqPending = true; }
        }
    }

    // VBlank / NMI
    const bool nowVBlank = (m_vCounter >= VBLANK_START);
    const bool vBlankEdge = nowVBlank && !m_inVBlank;
    m_inVBlank = nowVBlank;

    if (vBlankEdge) {
        m_nmiFlag = true;
        if (m_nmiEnabled) {
            m_apu.step();
            return true;
        }
    }

    m_apu.step();
    return false;
}

void Bus::setJoy1(uint16_t state) { m_joy1 = state; }

bool Bus::takePendingIrq() {
    const bool pending = m_irqPending;
    m_irqPending = false;
    return pending;
}

RomMapping Bus::mapMode() const { return m_mapMode; }
size_t Bus::sramBytes() const { return m_sramBytes; }


bool Bus::isLoRomArea(uint8_t bank, uint16_t addr) const {
    (void)bank;
    return addr >= 0x8000;
}

uint32_t Bus::loRomToFileOffset(uint8_t bank, uint16_t addr) const {
    return (static_cast<uint32_t>(bank & 0x7F) << 15)
         | static_cast<uint32_t>(addr - 0x8000);
}

bool Bus::isHiRomArea(uint8_t bank, uint16_t addr) const {
    if (bank >= 0xC0)                        return true; // $C0-$FF full banks
    if (bank >= 0x40 && bank <= 0x7D)        return true; // $40-$7D full banks (large ROMs)
    if (addr >= 0x8000)                      return true; // $00-$3F / $80-$BF upper half
    return false;
}

uint32_t Bus::hiRomToFileOffset(uint8_t bank, uint16_t addr) const {
    return static_cast<uint32_t>(bank & 0x3F) * 0x10000
         + static_cast<uint32_t>(addr);
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

    // TIMEUP — IRQ flag, cleared on read
    if (addr == 0x4211) {
        const uint8_t result = m_irqFlag ? 0x80 : 0x00;
        m_irqFlag = false;
        return result;
    }

    // HVBJOY — bit 7 = VBlank, bit 6 = HBlank, bit 0 = auto-joypad busy
    if (addr == 0x4212) {
        uint8_t status = 0x00;
        if (m_vCounter >= VBLANK_START) status |= 0x80;
        if (m_hCounter >= 274)          status |= 0x40;
        return status;
    }

    // Auto-joypad read results
    if (addr == 0x4218) return static_cast<uint8_t>(m_joy1 & 0xFF);
    if (addr == 0x4219) return static_cast<uint8_t>(m_joy1 >> 8);
    if (addr == 0x421A || addr == 0x421B) return 0x00; // controller 2

    // DMA channel registers $4300-$437F (read)
    if (addr >= 0x4300 && addr <= 0x437F) {
        const uint8_t ch  = static_cast<uint8_t>((addr - 0x4300) >> 4);
        const uint8_t reg = static_cast<uint8_t>((addr - 0x4300) & 0x0F);
        return (reg < 8) ? m_dma.readReg(ch, reg) : 0xFF;
    }

    // Multiply/divide result registers
    if (addr == 0x4214) return static_cast<uint8_t>(m_rddiv & 0xFF);
    if (addr == 0x4215) return static_cast<uint8_t>(m_rddiv >> 8);
    if (addr == 0x4216) return static_cast<uint8_t>(m_rdmpy & 0xFF);
    if (addr == 0x4217) return static_cast<uint8_t>(m_rdmpy >> 8);

    // ------------------------------------------------------------
    // SRAM
    // ------------------------------------------------------------
    if (!m_sram.empty()) {
        if (m_mapMode == RomMapping::LoROM &&
            bank >= 0x70 && bank <= 0x7D && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank - 0x70) * 0x8000 + addr) % m_sramBytes;
            return m_sram[off];
        }
        if (m_mapMode == RomMapping::HiROM &&
            ((bank >= 0x20 && bank <= 0x3F) || (bank >= 0xA0 && bank <= 0xBF)) &&
            addr >= 0x6000 && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank & 0x1F) * 0x2000 + (addr - 0x6000)) % m_sramBytes;
            return m_sram[off];
        }
    }

    // ------------------------------------------------------------
    // ROM area — LoROM or HiROM
    // ------------------------------------------------------------
    if (m_mapMode == RomMapping::HiROM) {
        if (isHiRomArea(bank, addr)) {
            const uint32_t offset = hiRomToFileOffset(bank, addr);
            if (offset < m_rom.size()) return m_rom[offset];
            return 0xFF;
        }
    } else {
        if (isLoRomArea(bank, addr)) {
            const uint32_t offset = loRomToFileOffset(bank, addr);
            if (offset < m_rom.size()) return m_rom[offset];
            return 0xFF;
        }
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
        m_irqMode    = (value >> 4) & 0x3;
        return;
    }

    // IRQ timer targets
    if (addr == 0x4207) { m_htime = (m_htime & 0x0100) | value; return; }
    if (addr == 0x4208) { m_htime = (m_htime & 0x00FF) | ((value & 0x01) << 8); return; }
    if (addr == 0x4209) { m_vtime = (m_vtime & 0x0100) | value; return; }
    if (addr == 0x420A) { m_vtime = (m_vtime & 0x00FF) | ((value & 0x01) << 8); return; }

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
    // DMA trigger ($420B)
    // ------------------------------------------------------------
    if (addr == 0x420B) {
        if (value) m_dma.trigger(value, *this);
        return;
    }

    // DMA channel registers $4300-$437F
    if (addr >= 0x4300 && addr <= 0x437F) {
        const uint8_t ch  = static_cast<uint8_t>((addr - 0x4300) >> 4);
        const uint8_t reg = static_cast<uint8_t>((addr - 0x4300) & 0x0F);
        if (reg < 8) m_dma.writeReg(ch, reg, value);
        return;
    }

    // SRAM
    if (!m_sram.empty()) {
        if (m_mapMode == RomMapping::LoROM &&
            bank >= 0x70 && bank <= 0x7D && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank - 0x70) * 0x8000 + addr) % m_sramBytes;
            m_sram[off] = value;
            return;
        }
        if (m_mapMode == RomMapping::HiROM &&
            ((bank >= 0x20 && bank <= 0x3F) || (bank >= 0xA0 && bank <= 0xBF)) &&
            addr >= 0x6000 && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank & 0x1F) * 0x2000 + (addr - 0x6000)) % m_sramBytes;
            m_sram[off] = value;
            return;
        }
    }

    // ROM area ignored on write
}