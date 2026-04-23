#include "ppu.hpp"

// -----------------------------------------------------------------------
// reset
// -----------------------------------------------------------------------
void Ppu::reset() {
    m_vram.fill(0);
    m_oam.fill(0);
    m_cgram.fill(0);

    m_forcedBlank = true;
    m_brightness  = 0x0F;
    m_obsel       = 0;

    m_oamBaseAddr = 0;
    m_oamAddr     = 0;
    m_oamPriority = false;
    m_oamLatch    = 0;

    m_bgMode      = 0;
    m_bg3Priority = false;
    m_mosaic      = 0;

    for (int i = 0; i < 4; ++i) { m_bgSC[i] = 0; m_bgHOFS[i] = 0; m_bgVOFS[i] = 0; }
    m_bgNBA[0] = m_bgNBA[1] = 0;
    m_bgOldByte = 0;

    m_vmain       = 0x80;
    m_vramAddr    = 0;
    m_vramReadBuf = 0;

    m_m7sel = 0;
    m_m7a = 0; m_m7b = 0; m_m7c = 0; m_m7d = 0x0100;
    m_m7x = 0; m_m7y = 0;
    m_m7latch = 0;

    m_cgramWordAddr = 0;
    m_cgramFlip     = false;
    m_cgramBuf      = 0;

    m_w12sel = m_w34sel = m_wobjsel = 0;
    m_wh[0] = m_wh[1] = m_wh[2] = m_wh[3] = 0;
    m_wbglog = m_wobjlog = 0;

    m_tm = m_ts = m_tmw = m_tsw = 0;

    m_cgswsel = m_cgadsub = 0;
    m_fixedR = m_fixedG = m_fixedB = 0;

    m_setini = 0;
}

// -----------------------------------------------------------------------
// VRAM helpers
// -----------------------------------------------------------------------
uint16_t Ppu::vramStep() const {
    static const uint16_t steps[4] = { 1, 32, 128, 128 };
    return steps[m_vmain & 0x03];
}

void Ppu::vramPrefetch() const {
    m_vramReadBuf = m_vram[m_vramAddr & 0x7FFF];
}

// -----------------------------------------------------------------------
// OAM helpers
// -----------------------------------------------------------------------
void Ppu::writeOam(uint8_t value) {
    if (m_oamAddr < 512) {
        // Main OAM: pair writes — low byte latched, committed on high byte
        if ((m_oamAddr & 1) == 0) {
            m_oamLatch = value;
        } else {
            m_oam[m_oamAddr - 1] = m_oamLatch;
            m_oam[m_oamAddr]     = value;
        }
    } else if (m_oamAddr < 544) {
        // Extra OAM table: direct byte write
        m_oam[m_oamAddr] = value;
    }
    m_oamAddr = (m_oamAddr + 1) % 544;
}

uint8_t Ppu::readOam() const {
    uint8_t val = 0xFF;
    if (m_oamAddr < 544) val = m_oam[m_oamAddr];
    m_oamAddr = (m_oamAddr + 1) % 544;
    return val;
}

// -----------------------------------------------------------------------
// writeReg — all PPU register writes ($2100-$213F)
// -----------------------------------------------------------------------
void Ppu::writeReg(uint16_t addr, uint8_t value) {
    switch (addr) {

    // --- $2100 INIDISP: forced blank + brightness ---
    case 0x2100:
        m_forcedBlank = (value >> 7) & 1;
        m_brightness  = value & 0x0F;
        break;

    // --- $2101 OBSEL: sprite size + CHR base ---
    case 0x2101:
        m_obsel = value;
        break;

    // --- $2102/$2103 OAMADDL/H: OAM address ---
    case 0x2102:
        m_oamBaseAddr = (m_oamBaseAddr & 0x100) | value;
        m_oamAddr     = m_oamBaseAddr * 2;
        break;
    case 0x2103:
        m_oamBaseAddr = (m_oamBaseAddr & 0x0FF) | ((value & 0x01) << 8);
        m_oamPriority = (value >> 7) & 1;
        m_oamAddr     = m_oamBaseAddr * 2;
        break;

    // --- $2104 OAMDATA ---
    case 0x2104:
        writeOam(value);
        break;

    // --- $2105 BGMODE ---
    case 0x2105:
        m_bgMode      = value & 0x07;
        m_bg3Priority = (value >> 3) & 1;
        break;

    // --- $2106 MOSAIC ---
    case 0x2106:
        m_mosaic = value;
        break;

    // --- $2107-$210A BG1-4 tilemap base + mirror size ---
    case 0x2107: m_bgSC[0] = value; break;
    case 0x2108: m_bgSC[1] = value; break;
    case 0x2109: m_bgSC[2] = value; break;
    case 0x210A: m_bgSC[3] = value; break;

    // --- $210B/$210C BG CHR base addresses ---
    case 0x210B: m_bgNBA[0] = value; break;
    case 0x210C: m_bgNBA[1] = value; break;

    // --- $210D-$2114 BG scroll (shared latch) ---
    // Pattern: HOFS = (old_latch) | ((value & 3) << 8); latch = value
    case 0x210D: m_bgHOFS[0] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x210E: m_bgVOFS[0] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x210F: m_bgHOFS[1] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2110: m_bgVOFS[1] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2111: m_bgHOFS[2] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2112: m_bgVOFS[2] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2113: m_bgHOFS[3] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2114: m_bgVOFS[3] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;

    // --- $2115 VMAIN: VRAM access mode ---
    case 0x2115:
        m_vmain = value;
        break;

    // --- $2116/$2117 VMADD: VRAM word address (prefetch on write) ---
    case 0x2116:
        m_vramAddr = (m_vramAddr & 0xFF00) | value;
        vramPrefetch();
        break;
    case 0x2117:
        m_vramAddr = (m_vramAddr & 0x00FF) | (static_cast<uint16_t>(value) << 8);
        vramPrefetch();
        break;

    // --- $2118 VMDATAL: write low byte, increment if VMAIN.7=0 ---
    case 0x2118: {
        const uint16_t a = m_vramAddr & 0x7FFF;
        m_vram[a] = (m_vram[a] & 0xFF00) | value;
        if ((m_vmain & 0x80) == 0) m_vramAddr += vramStep();
        break;
    }

    // --- $2119 VMDATAH: write high byte, increment if VMAIN.7=1 ---
    case 0x2119: {
        const uint16_t a = m_vramAddr & 0x7FFF;
        m_vram[a] = (m_vram[a] & 0x00FF) | (static_cast<uint16_t>(value) << 8);
        if ((m_vmain & 0x80) != 0) m_vramAddr += vramStep();
        break;
    }

    // --- $211A M7SEL ---
    case 0x211A:
        m_m7sel = value;
        break;

    // --- $211B-$2120 Mode 7 matrix + center (shared write latch) ---
    case 0x211B: m_m7a = static_cast<int16_t>((value << 8) | m_m7latch); m_m7latch = value; break;
    case 0x211C: m_m7b = static_cast<int16_t>((value << 8) | m_m7latch); m_m7latch = value; break;
    case 0x211D: m_m7c = static_cast<int16_t>((value << 8) | m_m7latch); m_m7latch = value; break;
    case 0x211E: m_m7d = static_cast<int16_t>((value << 8) | m_m7latch); m_m7latch = value; break;
    case 0x211F: m_m7x = static_cast<int16_t>((value << 8) | m_m7latch); m_m7latch = value; break;
    case 0x2120: m_m7y = static_cast<int16_t>((value << 8) | m_m7latch); m_m7latch = value; break;

    // --- $2121 CGADD: CGRAM word address (color index) ---
    case 0x2121:
        m_cgramWordAddr = value;
        m_cgramFlip     = false;
        break;

    // --- $2122 CGDATA: two-write latch, BGR555 ---
    case 0x2122:
        if (!m_cgramFlip) {
            m_cgramBuf  = value;        // buffer low byte
            m_cgramFlip = true;
        } else {
            m_cgram[m_cgramWordAddr] =
                static_cast<uint16_t>(m_cgramBuf) |
                (static_cast<uint16_t>(value & 0x7F) << 8);
            m_cgramWordAddr++;          // wraps at 256
            m_cgramFlip = false;
        }
        break;

    // --- $2123-$212B Windowing ---
    case 0x2123: m_w12sel  = value; break;
    case 0x2124: m_w34sel  = value; break;
    case 0x2125: m_wobjsel = value; break;
    case 0x2126: m_wh[0]   = value; break;
    case 0x2127: m_wh[1]   = value; break;
    case 0x2128: m_wh[2]   = value; break;
    case 0x2129: m_wh[3]   = value; break;
    case 0x212A: m_wbglog  = value; break;
    case 0x212B: m_wobjlog = value; break;

    // --- $212C-$212F Screen designation ---
    case 0x212C: m_tm  = value & 0x1F; break;
    case 0x212D: m_ts  = value & 0x1F; break;
    case 0x212E: m_tmw = value & 0x1F; break;
    case 0x212F: m_tsw = value & 0x1F; break;

    // --- $2130 CGSWSEL / $2131 CGADSUB ---
    case 0x2130: m_cgswsel = value; break;
    case 0x2131: m_cgadsub = value; break;

    // --- $2132 COLDATA: fixed color (R/G/B components written separately) ---
    case 0x2132: {
        const uint8_t col = value & 0x1F;
        if (value & 0x20) m_fixedR = col;
        if (value & 0x40) m_fixedG = col;
        if (value & 0x80) m_fixedB = col;
        break;
    }

    // --- $2133 SETINI ---
    case 0x2133:
        m_setini = value;
        break;

    default:
        break;
    }
}

// -----------------------------------------------------------------------
// readReg — all PPU register reads ($2100-$213F)
// -----------------------------------------------------------------------
uint8_t Ppu::readReg(uint16_t addr) const {
    switch (addr) {

    // --- $2138 OAMDATAREAD ---
    case 0x2138:
        return readOam();

    // --- $2139 VMDATALREAD: return prefetch low byte ---
    case 0x2139: {
        const uint8_t val = static_cast<uint8_t>(m_vramReadBuf & 0xFF);
        if ((m_vmain & 0x80) == 0) {
            m_vramAddr += vramStep();
            vramPrefetch();
        }
        return val;
    }

    // --- $213A VMDATAHREAD: return prefetch high byte ---
    case 0x213A: {
        const uint8_t val = static_cast<uint8_t>(m_vramReadBuf >> 8);
        if ((m_vmain & 0x80) != 0) {
            m_vramAddr += vramStep();
            vramPrefetch();
        }
        return val;
    }

    // --- $213B CGDATAREAD: two-read latch ---
    case 0x213B: {
        uint8_t val;
        const uint16_t color = m_cgram[m_cgramWordAddr];
        if (!m_cgramFlip) {
            val         = static_cast<uint8_t>(color & 0xFF);
            m_cgramFlip = true;
        } else {
            val             = static_cast<uint8_t>((color >> 8) & 0x7F);
            m_cgramWordAddr++;
            m_cgramFlip = false;
        }
        return val;
    }

    // --- $213E PPU1 status: version=1, OBJ overflow flags (cleared on read) ---
    case 0x213E:
        return 0x01;

    default:
        return 0xFF;
    }
}
