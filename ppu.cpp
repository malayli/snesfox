#include "ppu.hpp"
#include <algorithm>
#include <cstdio>

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
    m_bgTileSize  = 0;
    m_mosaic      = 0;
    m_framebuffer.fill(0xFF000000u);

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

    m_setini  = 0;
    m_diagDone = false;
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
        m_bgTileSize  = value >> 4;   // bit0=BG1, bit1=BG2, bit2=BG3, bit3=BG4
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

    // --- $213E PPU1 status: version=1 in bits 3:0; bit 6 = OBJ range overflow ---
    case 0x213E: {
        const uint8_t val = 0x01 | (m_objRangeOver ? 0x40 : 0x00);
        m_objRangeOver = false;   // cleared on read
        return val;
    }

    default:
        return 0xFF;
    }
}

// =======================================================================
// Rendering
// =======================================================================

// -----------------------------------------------------------------------
// cgramToArgb — BGR555 → 0xAARRGGBB (alpha always 0xFF)
// -----------------------------------------------------------------------
uint32_t Ppu::cgramToArgb(uint16_t bgr) const {
    const uint8_t r = static_cast<uint8_t>((bgr & 0x001F) << 3);
    const uint8_t g = static_cast<uint8_t>(((bgr >> 5)  & 0x1F) << 3);
    const uint8_t b = static_cast<uint8_t>(((bgr >> 10) & 0x1F) << 3);
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16)
                       | (static_cast<uint32_t>(g) << 8)
                       | b;
}

// -----------------------------------------------------------------------
// chrBase — VRAM word address of CHR data for bg (0-3)
// -----------------------------------------------------------------------
uint16_t Ppu::chrBase(int bg) const {
    const uint8_t nba   = m_bgNBA[bg >> 1];           // $210B for BG1/2, $210C for BG3/4
    const int     shift = (bg & 1) ? 4 : 0;
    return static_cast<uint16_t>(((nba >> shift) & 0x0F) * 0x1000);
}

// -----------------------------------------------------------------------
// tilemapEntry — fetch the 16-bit tilemap word for tile (col,row)
//   Handles 32×32, 64×32, 32×64, 64×64 tilemaps via BGxSC bits 1:0
// -----------------------------------------------------------------------
uint16_t Ppu::tilemapEntry(int bg, int tileCol, int tileRow) const {
    const uint8_t  sc   = m_bgSC[bg];
    const uint16_t base = static_cast<uint16_t>((sc >> 2) * 0x400);
    const uint8_t  size = sc & 0x03;

    uint16_t off = 0;
    int c = tileCol & 63;
    int r = tileRow & 63;

    switch (size) {
    case 0: // 32×32
        c &= 31; r &= 31;
        break;
    case 1: // 64×32 — second screen to the right
        if (c >= 32) { off = 0x400; c -= 32; }
        r &= 31;
        break;
    case 2: // 32×64 — second screen below
        c &= 31;
        if (r >= 32) { off = 0x400; r -= 32; }
        break;
    case 3: // 64×64 — four screens
        if (c >= 32) off += 0x400;
        if (r >= 32) off += 0x800;
        c &= 31; r &= 31;
        break;
    }

    return m_vram[(base + off + static_cast<uint16_t>(r * 32 + c)) & 0x7FFF];
}

// -----------------------------------------------------------------------
// getPixel — decode one pixel from VRAM tile data
//   bpp : 2, 4, or 8
//   base: VRAM word address of CHR block
//   row : pixel row within tile (0-7, already flip-adjusted)
//   col : pixel column (0-7, already flip-adjusted)
//   Returns raw color index (0 = transparent)
// -----------------------------------------------------------------------
uint8_t Ppu::getPixel(int bpp, uint16_t base, uint16_t tileNum, int row, int col) const {
    const int bit = 7 - col;   // bit position (MSB = pixel 0)

    auto plane = [&](uint16_t wordOff) -> uint16_t {
        return m_vram[(base + tileNum * static_cast<uint16_t>(bpp == 2 ? 8 : bpp == 4 ? 16 : 32) + wordOff) & 0x7FFF];
    };

    switch (bpp) {
    case 2: {
        const uint16_t w = plane(static_cast<uint16_t>(row));
        return static_cast<uint8_t>(((w >> bit) & 1) | (((w >> (8 + bit)) & 1) << 1));
    }
    case 4: {
        const uint16_t w0 = plane(static_cast<uint16_t>(row));
        const uint16_t w1 = plane(static_cast<uint16_t>(8 + row));
        const uint8_t p01 = static_cast<uint8_t>(((w0 >> bit) & 1) | (((w0 >> (8 + bit)) & 1) << 1));
        const uint8_t p23 = static_cast<uint8_t>(((w1 >> bit) & 1) | (((w1 >> (8 + bit)) & 1) << 1));
        return static_cast<uint8_t>(p01 | (p23 << 2));
    }
    case 8: {
        const uint16_t w0 = plane(static_cast<uint16_t>(row));
        const uint16_t w1 = plane(static_cast<uint16_t>(8 + row));
        const uint16_t w2 = plane(static_cast<uint16_t>(16 + row));
        const uint16_t w3 = plane(static_cast<uint16_t>(24 + row));
        const uint8_t p01 = static_cast<uint8_t>(((w0 >> bit) & 1) | (((w0 >> (8 + bit)) & 1) << 1));
        const uint8_t p23 = static_cast<uint8_t>(((w1 >> bit) & 1) | (((w1 >> (8 + bit)) & 1) << 1));
        const uint8_t p45 = static_cast<uint8_t>(((w2 >> bit) & 1) | (((w2 >> (8 + bit)) & 1) << 1));
        const uint8_t p67 = static_cast<uint8_t>(((w3 >> bit) & 1) | (((w3 >> (8 + bit)) & 1) << 1));
        return static_cast<uint8_t>(p01 | (p23 << 2) | (p45 << 4) | (p67 << 6));
    }
    default:
        return 0;
    }
}

// -----------------------------------------------------------------------
// renderBg — rasterize one BG layer for a single scanline
//   Handles 8×8 and 16×16 tiles, H/V flip, all tilemap mirror sizes.
//   Writes into 'out[0..255]'.  out[x].cgramIdx == 0 → transparent.
// -----------------------------------------------------------------------
void Ppu::renderBg(int bg, int bpp, int line, LayerPixel* out) const {
    const bool     largeTiles = (m_bgTileSize >> bg) & 1;
    const int      tileSz     = largeTiles ? 16 : 8;
    const uint16_t hofs       = m_bgHOFS[bg] & 0x3FF;
    const uint16_t vofs       = m_bgVOFS[bg] & 0x3FF;
    const uint16_t chr        = chrBase(bg);

    // CGRAM palette stride per bpp
    const int palStride = (bpp == 2) ? 4 : (bpp == 4) ? 16 : 256;

    const int effY    = (line + static_cast<int>(vofs)) & 0x3FF;
    const int tileRow = effY / tileSz;                  // coarse tile row in tilemap
    const int fineYt  = effY % tileSz;                  // pixel row within the tile cell

    for (int x = 0; x < 256; ++x) {
        const int effX    = (x + static_cast<int>(hofs)) & 0x3FF;
        const int tileCol = effX / tileSz;
        const int fineXt  = effX % tileSz;

        // Fetch tilemap word
        const uint16_t entry = tilemapEntry(bg, tileCol, tileRow);
        const bool     pri   = (entry >> 15) & 1;
        const bool     vflip = (entry >> 14) & 1;
        const bool     hflip = (entry >> 13) & 1;
        const uint8_t  pal   = (entry >> 10) & 7;
        const uint16_t baseN = entry & 0x3FF;

        // Apply flip within the tile cell
        int fx = hflip ? (tileSz - 1 - fineXt) : fineXt;
        int fy = vflip ? (tileSz - 1 - fineYt) : fineYt;

        // For 16×16 tiles: select sub-tile and reduce to 8×8 coordinates
        uint16_t tileNum = baseN;
        if (largeTiles) {
            const int subX = fx >> 3;
            const int subY = fy >> 3;
            tileNum = (baseN + static_cast<uint16_t>(subX) + static_cast<uint16_t>(subY * 16)) & 0x3FF;
            fx &= 7;
            fy &= 7;
        }

        const uint8_t color = getPixel(bpp, chr, tileNum, fy, fx);

        if (color == 0) {
            out[x] = { 0, 0 };   // transparent
        } else {
            const uint8_t idx = (bpp == 8)
                ? color
                : static_cast<uint8_t>(pal * palStride + color);
            out[x] = { idx, pri ? uint8_t(1) : uint8_t(0) };
        }
    }
}

// -----------------------------------------------------------------------
// renderSprites — rasterize all OBJ onto 'out[0..255]' for one scanline
//   OAM index 0 has highest priority (overwrites higher indices).
//   Sprite CGRAM: entries 128-255 (palette 0-7, 16 colors each).
//   Sets m_objRangeOver if >32 sprites appear on this line.
// -----------------------------------------------------------------------
void Ppu::renderSprites(int line, SpritePixel* out) const {
    for (int i = 0; i < 256; ++i) out[i] = {};

    // Size table: [OBSEL bits 7:5][small=0 / large=1] = {width, height}
    static const int kW[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
    static const int kH[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{32,64},{32,32}};

    const int sizeSet = (m_obsel >> 5) & 0x07;

    // Sprite CHR addressing: first name table base + gap to second table
    // base (words) = (obsel & 7) << 12
    // gap  (words) = (((obsel >> 3) & 3) + 1) << 12
    const uint16_t nameBase = static_cast<uint16_t>((m_obsel & 0x07) << 12);
    const uint16_t nameGap  = static_cast<uint16_t>((static_cast<uint16_t>((m_obsel >> 3) & 0x03) + 1u) << 12);

    // ---- Pass 1: collect up to 32 visible sprites ----
    int visIdx[32];
    int visCount = 0;
    bool overflow = false;

    for (int i = 0; i < 128; ++i) {
        const uint8_t* s = m_oam.data() + i * 4;

        // Extra OAM: 2 bits per sprite packed into bytes at offset 512
        const uint8_t extra = m_oam[512 + (i >> 2)];
        const int     shift = (i & 3) << 1;
        const uint8_t eBits = (extra >> shift) & 0x03;

        const bool largeSize = (eBits >> 1) & 1;
        const int  sprH      = kH[sizeSet][largeSize ? 1 : 0];

        // 8-bit Y comparison (handles wrap correctly for off-screen sprites)
        if (static_cast<uint8_t>(static_cast<uint8_t>(line) - s[1])
                >= static_cast<uint8_t>(sprH)) continue;

        // X bounds check: 9-bit signed X
        const int sprX = (static_cast<int>(s[0]) | (static_cast<int>(eBits & 1) << 8));
        const int sX   = (sprX >= 256) ? (sprX - 512) : sprX;
        const int sprW = kW[sizeSet][largeSize ? 1 : 0];
        if (sX + sprW <= 0 || sX >= 256) continue;

        if (visCount < 32) {
            visIdx[visCount++] = i;
        } else {
            overflow = true;
            break;
        }
    }
    if (overflow) m_objRangeOver = true;

    // ---- Pass 2: render back-to-front (high OAM index first so low index wins) ----
    for (int v = visCount - 1; v >= 0; --v) {
        const int     i     = visIdx[v];
        const uint8_t* s    = m_oam.data() + i * 4;

        const uint8_t extra = m_oam[512 + (i >> 2)];
        const int     shift = (i & 3) << 1;
        const uint8_t eBits = (extra >> shift) & 0x03;

        const bool largeSize = (eBits >> 1) & 1;
        const int  sprW      = kW[sizeSet][largeSize ? 1 : 0];
        const int  sprH      = kH[sizeSet][largeSize ? 1 : 0];

        const int rawX = static_cast<int>(s[0]) | (static_cast<int>(eBits & 1) << 8);
        const int sprX = (rawX >= 256) ? (rawX - 512) : rawX;

        const uint8_t attr    = s[3];
        const bool    vflip   = (attr >> 7) & 1;
        const bool    hflip   = (attr >> 6) & 1;
        const uint8_t pri     = (attr >> 4) & 0x03;
        const uint8_t pal     = (attr >> 1) & 0x07;
        const bool    nameBit = attr & 0x01;
        const uint8_t baseN   = s[2];

        // Local Y within sprite (already validated to be in [0, sprH))
        int ly = static_cast<int>(static_cast<uint8_t>(static_cast<uint8_t>(line) - s[1]));
        if (vflip) ly = sprH - 1 - ly;
        const int tileRow = ly >> 3;
        const int fineY   = ly & 7;

        // CHR base for this sprite
        const uint16_t tileBase = nameBase + (nameBit ? nameGap : uint16_t(0));

        for (int lxi = 0; lxi < sprW; ++lxi) {
            const int screenX = sprX + lxi;
            if (screenX < 0 || screenX >= 256) continue;

            const int lx      = hflip ? (sprW - 1 - lxi) : lxi;
            const int tileCol = lx >> 3;
            const int fineX   = lx & 7;

            // Tile index wraps within the 256-tile name table row of 16
            const uint8_t tileNum = static_cast<uint8_t>(
                baseN + static_cast<uint8_t>(tileCol) + static_cast<uint8_t>(tileRow * 16));

            const uint8_t color = getPixel(4, tileBase, tileNum, fineY, fineX);
            if (color == 0) continue;  // transparent

            out[screenX] = {
                static_cast<uint8_t>(128u + pal * 16u + color),
                pri
            };
        }
    }
}

// -----------------------------------------------------------------------
// compositePixel — pick the highest-priority opaque BG pixel for column x
//   Mode 1 priority order (no sprites yet):
//     bg3Priority set   : BG3p1 > BG1p1 > BG2p1 > BG1p0 > BG2p0 > BG3p0
//     bg3Priority clear : BG1p1 > BG2p1 > BG1p0 > BG2p0 > BG3p1 > BG3p0
//   Mode 0 (all 2bpp, 4 layers):
//     BG1p1 > BG2p1 > BG3p1 > BG4p1 > BG1p0 > BG2p0 > BG3p0 > BG4p0
// -----------------------------------------------------------------------
uint32_t Ppu::compositePixel(int x,
                              const LayerPixel*  bg0,
                              const LayerPixel*  bg1,
                              const LayerPixel*  bg2,
                              const LayerPixel*  bg3,
                              const SpritePixel* spr) const
{
    const LayerPixel* layers[4] = { bg0, bg1, bg2, bg3 };

    // Winner state — cgramIdx=0 means backdrop (CGADSUB bit 0)
    uint8_t winIdx     = 0;
    uint8_t winCmBit   = 0x01;  // CGADSUB bit: 0=backdrop,1=OBJ,2=BG1,3=BG2,4=BG3,5=BG4
    bool    found      = false;

    auto tryBg = [&](int bg, uint8_t pri) {
        if (found) return;
        if (layers[bg][x].cgramIdx == 0 || layers[bg][x].priority != pri) return;
        winIdx   = layers[bg][x].cgramIdx;
        winCmBit = static_cast<uint8_t>(1u << (bg + 2));   // BG1=bit2…BG4=bit5
        found    = true;
    };
    auto trySpr = [&](uint8_t pri) {
        if (found) return;
        if (spr[x].cgramIdx == 0 || spr[x].priority != pri) return;
        winIdx   = spr[x].cgramIdx;
        winCmBit = 0x02;    // OBJ = bit 1
        found    = true;
    };

    switch (m_bgMode) {
    case 0:
        trySpr(3);
        tryBg(0,1); tryBg(1,1);
        trySpr(2);
        tryBg(2,1); tryBg(3,1);
        trySpr(1);
        tryBg(0,0); tryBg(1,0);
        trySpr(0);
        tryBg(2,0); tryBg(3,0);
        break;

    case 1:
        if (m_bg3Priority) {
            tryBg(2,1);
            trySpr(3);
            tryBg(0,1); tryBg(1,1);
            trySpr(2);
            tryBg(0,0); tryBg(1,0);
            trySpr(1);
            tryBg(2,0);
            trySpr(0);
        } else {
            trySpr(3);
            tryBg(0,1); tryBg(1,1);
            trySpr(2);
            tryBg(0,0); tryBg(1,0);
            trySpr(1);
            tryBg(2,1);
            trySpr(0);
            tryBg(2,0);
        }
        break;

    case 3:
        trySpr(3);
        tryBg(0,1); tryBg(1,1);
        trySpr(2);
        tryBg(0,0); tryBg(1,0);
        trySpr(1);
        trySpr(0);
        break;

    default:
        break;
    }

    // Resolve CGRAM color of winner
    uint32_t mainColor = cgramToArgb(m_cgram[winIdx]);

    // Basic fixed-color math ($2130 bits 7:6 == 00 → always apply)
    // $2131 bit 7: subtract, bit 6: half, bits 5:0: layer enable mask
    if ((m_cgadsub & winCmBit) && ((m_cgswsel & 0xC0) == 0x00)) {
        const bool doSub  = (m_cgadsub >> 7) & 1;
        const bool doHalf = (m_cgadsub >> 6) & 1;

        const uint32_t subR = static_cast<uint32_t>(m_fixedR) << 3;
        const uint32_t subG = static_cast<uint32_t>(m_fixedG) << 3;
        const uint32_t subB = static_cast<uint32_t>(m_fixedB) << 3;

        uint32_t mR = (mainColor >> 16) & 0xFF;
        uint32_t mG = (mainColor >>  8) & 0xFF;
        uint32_t mB =  mainColor        & 0xFF;

        if (doSub) {
            mR = (mR > subR) ? mR - subR : 0u;
            mG = (mG > subG) ? mG - subG : 0u;
            mB = (mB > subB) ? mB - subB : 0u;
        } else {
            mR = std::min(255u, mR + subR);
            mG = std::min(255u, mG + subG);
            mB = std::min(255u, mB + subB);
        }
        if (doHalf) { mR >>= 1; mG >>= 1; mB >>= 1; }

        mainColor = 0xFF000000u | (mR << 16) | (mG << 8) | mB;
    }

    return mainColor;
}

// -----------------------------------------------------------------------
// renderScanline — main entry point called once per active scanline
// -----------------------------------------------------------------------
void Ppu::renderScanline(int line) {
    if (line < 0 || line >= 224) return;

    uint32_t* row = m_framebuffer.data() + line * 256;

    // Forced blank → black scanline
    if (m_forcedBlank) {
        for (int x = 0; x < 256; ++x) row[x] = 0xFF000000u;
        return;
    }

    // One-shot diagnostic: print PPU state on the very first active scanline
    if (!m_diagDone && line == 0) {
        m_diagDone = true;
        std::fprintf(stderr,
            "[PPU diag] First active frame:\n"
            "  bgMode=%u  tm=$%02X  bg3pri=%u\n"
            "  bgSC=%02X %02X %02X %02X  bgNBA=%02X %02X\n"
            "  CGRAM[0]=%04X [1]=%04X [2]=%04X [3]=%04X\n"
            "  VRAM[0]=%04X [1]=%04X [2]=%04X [3]=%04X\n",
            m_bgMode, m_tm, (unsigned)m_bg3Priority,
            m_bgSC[0], m_bgSC[1], m_bgSC[2], m_bgSC[3],
            m_bgNBA[0], m_bgNBA[1],
            m_cgram[0], m_cgram[1], m_cgram[2], m_cgram[3],
            m_vram[0],  m_vram[1],  m_vram[2],  m_vram[3]);
    }

    // Per-layer pixel buffers (default: transparent)
    LayerPixel  bg0[256]{}, bg1[256]{}, bg2[256]{}, bg3[256]{};
    SpritePixel spr[256]{};

    switch (m_bgMode) {
    case 0:
        if (m_tm & 0x01) renderBg(0, 2, line, bg0);
        if (m_tm & 0x02) renderBg(1, 2, line, bg1);
        if (m_tm & 0x04) renderBg(2, 2, line, bg2);
        if (m_tm & 0x08) renderBg(3, 2, line, bg3);
        break;
    case 1:
        if (m_tm & 0x01) renderBg(0, 4, line, bg0);
        if (m_tm & 0x02) renderBg(1, 4, line, bg1);
        if (m_tm & 0x04) renderBg(2, 2, line, bg2);
        break;
    case 3:
        if (m_tm & 0x01) renderBg(0, 8, line, bg0);
        if (m_tm & 0x02) renderBg(1, 4, line, bg1);
        break;
    default:
        for (int x = 0; x < 256; ++x) row[x] = cgramToArgb(m_cgram[0]);
        return;
    }

    if (m_tm & 0x10) renderSprites(line, spr);  // bit 4 = OBJ on main screen

    for (int x = 0; x < 256; ++x) {
        row[x] = compositePixel(x, bg0, bg1, bg2, bg3, spr);
    }
}
