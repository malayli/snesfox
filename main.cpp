#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "bus.hpp"
#include "cpu.hpp"
#include "display.hpp"
#include "header.hpp"
#include "opcodes.hpp"
#include "rom.hpp"
#include "disasm_dump.hpp"
#include "reasm.hpp"

namespace {
constexpr uint64_t CYCLES_PER_FRAME = 30000;
constexpr int LOG_SIZE = 20;
constexpr uint64_t DEFAULT_COV_FRAMES = 600;
constexpr uint64_t COV_MAX_STEPS = 20000000ull;

std::string trimCopy(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

bool parseHex24Line(const std::string& t, uint32_t& out) {
    std::string h = trimCopy(t);
    if (h.empty()) return false;
    if (h.size() >= 2 && h[0] == '$') h = h.substr(1);
    if (h.size() > 6) return false;
    uint32_t v = 0;
    for (char c : h) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(10 + c - 'A');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(10 + c - 'a');
        else return false;
    }
    out = v & 0xFFFFFFu;
    return true;
}

std::unordered_set<uint32_t> loadCoverageFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open coverage file: " + path);
    }

    std::unordered_set<uint32_t> pcs;
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trimCopy(line);
        if (t.empty() || t[0] == '#') continue;

        uint32_t pc = 0;
        if (!parseHex24Line(t, pc)) {
            throw std::runtime_error("invalid coverage line (expected hex PC): " + path);
        }
        pcs.insert(pc);
    }

    return pcs;
}

std::string hex8(uint8_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(value);
    return oss.str();
}

std::string hex16(uint16_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
        << value;
    return oss.str();
}

std::string hex24(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
        << (value & 0xFFFFFF);
    return oss.str();
}

uint16_t readResetVector(const std::vector<uint8_t>& rom, bool isLoRom) {
    const size_t addr = isLoRom ? 0x7FFC : 0xFFFC;
    if (rom.size() <= addr + 1) return 0x0000;
    return static_cast<uint16_t>(rom[addr] | (rom[addr + 1] << 8));
}

std::string formatDisasmLine(uint32_t pc24, const CPU& cpu, bool isCurrent = false) {
    std::ostringstream oss;
    oss << (isCurrent ? "> " : "  ");
    oss << "$" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << ((pc24 >> 16) & 0xFF) << ":" << std::setw(4) << (pc24 & 0xFFFF) << "  ";
    oss << std::left << std::setw(10) << std::setfill(' ') << cpu.bytes();
    oss << " " << cpu.instruction();
    return oss.str();
}

std::vector<std::string> makeDebugLines(
    const std::vector<std::string>& headerLines,
    const CPU& cpu,
    const std::deque<std::string>& instructionLog,
    bool paused
) {
    std::vector<std::string> lines = headerLines;
    lines.push_back("");
    lines.push_back("=== CPU Debug ===");
    lines.push_back(std::string("State        : ") + (paused ? "PAUSED" : "RUNNING"));
    lines.push_back("Reset Vector : " + hex16(cpu.resetVector()));
    lines.push_back("Bank         : " + hex8(cpu.bank()));
    lines.push_back("PC           : " + hex16(cpu.pc()));
    lines.push_back("PC24         : " + hex24(cpu.pc24()));
    lines.push_back("Opcode       : " + hex8(cpu.opcode()));
    lines.push_back("Instruction  : " + cpu.instruction());
    lines.push_back("P            : " + hex8(cpu.p()));
    lines.push_back(std::string("M/X          : ")
                    + (cpu.flagM() ? "M=8" : "M=16")
                    + "  "
                    + (cpu.flagX() ? "X=8" : "X=16"));
    lines.push_back("A            : " + hex16(cpu.a()));
    lines.push_back("X            : " + hex16(cpu.x()));
    lines.push_back("Y            : " + hex16(cpu.y()));
    lines.push_back("SP           : " + hex16(cpu.sp()));
    lines.push_back("Cycles       : " + std::to_string(cpu.cycles()));
    lines.push_back("");
    lines.push_back("=== Instruction Log ===");
    for (const auto& line : instructionLog) {
        lines.push_back(line);
    }
    return lines;
}

void printRomInfo(const Rom& rom, const std::vector<uint8_t>& data) {
    std::cout << "=== Rom Header ===\n";
    std::cout << "File size      : " << rom.fileSize() << " bytes\n";
    std::cout << "ROM size       : " << rom.size() << " bytes\n";
    std::cout << "Copier header  : " << (rom.hasHeader() ? "Yes (512 bytes)" : "No") << "\n";
    std::cout << "Data offset    : " << rom.offset() << "\n";
    HeaderParser::print(data);
}

int runHeader(const std::string& romPath) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }
    printRomInfo(rom, data);
    return 0;
}

int runDisasm(const std::string& romPath,
                const std::string& asmPath,
                const std::optional<std::string>& coveragePath) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    printRomInfo(rom, data);

    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector = readResetVector(data, isLoRom);

    const std::unordered_set<uint32_t>* covPtr = nullptr;
    std::unordered_set<uint32_t> covStorage;
    if (coveragePath) {
        covStorage = loadCoverageFile(*coveragePath);
        covPtr = &covStorage;
        std::cout << "Coverage PCs    : " << covStorage.size() << " (from " << *coveragePath
                  << ")\n";
    }

    dumpRomAsAsmFull(data, resetVector, asmPath, covPtr);
    std::cout << "=== Disassembler ===\n";
    std::cout << "Disassembly written to: " << asmPath << "\n";
    return 0;
}

int runReasm(const std::string& asmPath, const std::string& outRomPath) {
    std::string error;
    if (!reassembleDumpAsmToRomFile(asmPath, outRomPath, error)) {
        throw std::runtime_error("Reassembly failed: " + error);
    }

    std::cout << "=== Reassembler ===\n";
    std::cout << "ROM rebuilt successfully: " << outRomPath << "\n";
    return 0;
}

int runCov(const std::string& romPath, const std::string& covPath, uint64_t frames) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    printRomInfo(rom, data);

    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector = readResetVector(data, isLoRom);

    Bus bus(data);
    bus.reset();

    CPU cpu;
    cpu.reset(bus, resetVector);

    std::unordered_set<uint32_t> hit;
    uint64_t steps = 0;
    bool stopped = false;

    for (uint64_t f = 0; f < frames && !stopped; ++f) {
        const uint64_t frameStartCycles = cpu.cycles();

        while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
            hit.insert(cpu.pc24());

            cpu.step(bus);
            if (bus.stepPeripherals(cpu.cycles())) {
                cpu.triggerNmi(bus);
            }

            ++steps;
            if (steps >= COV_MAX_STEPS) {
                stopped = true;
                break;
            }
        }
    }

    std::vector<uint32_t> sorted(hit.begin(), hit.end());
    std::sort(sorted.begin(), sorted.end());

    std::ofstream out(covPath);
    if (!out) {
        throw std::runtime_error("cannot write coverage file: " + covPath);
    }

    out << "# snesfox-cov-v1 frames=" << frames << " steps=" << steps
        << " unique=" << sorted.size() << "\n";

    for (uint32_t pc : sorted) {
        out << std::uppercase << std::hex << std::setw(6) << std::setfill('0') << (pc & 0xFFFFFFu)
            << "\n";
    }

    std::cout << std::dec << std::nouppercase << std::setfill(' ');
    std::cout << "=== Coverage ===\n";
    std::cout << "Frames (target) : " << frames << "\n";
    std::cout << "Steps executed  : " << steps << "\n";
    std::cout << "Unique PCs      : " << sorted.size() << "\n";
    std::cout << "Written         : " << covPath << "\n";
    return 0;
}

int runEmu(const std::string& romPath) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    printRomInfo(rom, data);

    const auto headerLines = HeaderParser::toLines(data);
    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector = readResetVector(data, isLoRom);

    printMissingOpcodes(OPCODES);

    Bus bus(data);
    bus.reset();

    CPU cpu;
    cpu.reset(bus, resetVector);

    std::deque<std::string> instructionLog;
    bool paused = false;
    bool stepOnce = false;

    Display display("snesfox");

    bool running = true;
    while (running) {
        DebugAction action = DebugAction::None;
        running = display.processEvents(action);

        if (action == DebugAction::TogglePause) {
            paused = !paused;
        }
        if (action == DebugAction::StepOne && paused) {
            stepOnce = true;
        }

        if (!paused) {
            const uint64_t frameStartCycles = cpu.cycles();

            while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
                const uint32_t pcBefore = cpu.pc24();

                cpu.step(bus);
                if (bus.stepPeripherals(cpu.cycles())) {
                    cpu.triggerNmi(bus);
                }

                if (!instructionLog.empty() && instructionLog.front().rfind("> ", 0) == 0) {
                    instructionLog.front().replace(0, 2, "  ");
                }

                instructionLog.push_front(formatDisasmLine(pcBefore, cpu, true));
                if (instructionLog.size() > LOG_SIZE) {
                    instructionLog.pop_back();
                }
            }
        } else if (stepOnce) {
            stepOnce = false;

            const uint32_t pcBefore = cpu.pc24();
            cpu.step(bus);
            if (bus.stepPeripherals(cpu.cycles())) {
                cpu.triggerNmi(bus);
            }

            if (!instructionLog.empty() && instructionLog.front().rfind("> ", 0) == 0) {
                instructionLog.front().replace(0, 2, "  ");
            }

            instructionLog.push_front(formatDisasmLine(pcBefore, cpu, true));
            if (instructionLog.size() > LOG_SIZE) {
                instructionLog.pop_back();
            }
        }

        const auto lines = makeDebugLines(headerLines, cpu, instructionLog, paused);
        display.clear();
        display.present(lines);
        display.delay(16);
    }

    return 0;
}

void printUsage() {
    std::cerr << "Usage:\n";
    std::cerr << "  ./snesfox emu <rom.sfc>\n";
    std::cerr << "  ./snesfox header <rom.sfc>\n";
    std::cerr << "  ./snesfox cov <rom.sfc> <coverage.out> [frames]\n";
    std::cerr << "  ./snesfox disasm <rom.sfc> [output.asm [coverage.out]]\n";
    std::cerr << "  ./snesfox reasm <input.asm> [output.sfc]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            printUsage();
            return 1;
        }

        const std::string mode = argv[1];

        if (mode == "emu") {
            return runEmu(argv[2]);
        }

        if (mode == "header") {
            return runHeader(argv[2]);
        }

        if (mode == "cov") {
            if (argc < 4) {
                printUsage();
                return 1;
            }
            const std::string covOut = argv[3];
            uint64_t frames = DEFAULT_COV_FRAMES;
            if (argc >= 5) {
                frames = std::stoull(argv[4]);
                if (frames == 0) {
                    throw std::runtime_error("cov frames must be >= 1");
                }
            }
            return runCov(argv[2], covOut, frames);
        }

        if (mode == "disasm") {
            const std::string asmPath = (argc >= 4) ? argv[3] : "output.asm";
            std::optional<std::string> covPath;
            if (argc >= 5) {
                covPath = argv[4];
            }
            return runDisasm(argv[2], asmPath, covPath);
        }

        if (mode == "reasm") {
            const std::string outRomPath = (argc >= 4) ? argv[3] : "output.sfc";
            return runReasm(argv[2], outRomPath);
        }

        printUsage();
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
