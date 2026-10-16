#include "GuestCpu.hpp"
#include <array>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>

using anyps5::experimental::GuestCpu;

int main() {
    const std::array<std::uint8_t, 12> addition{
        0xB8, 0x28, 0, 0, 0,
        0x05, 0x02, 0, 0, 0,
        0xC3, 0x90
    };
    if (GuestCpu(addition).run() != 42) return EXIT_FAILURE;
    const std::array<std::uint8_t, 1> invalid{0x90};
    bool rejected = false;
    try {
        GuestCpu(invalid).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 5> truncated{0xB8, 1, 2, 3, 4};
    rejected = false;
    try {
        GuestCpu(truncated).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    rejected = false;
    try {
        GuestCpu(addition).run(1);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 18> branch{
        0xB8, 0x2A, 0, 0, 0,
        0x3D, 0x2A, 0, 0, 0,
        0x75, 0x05,
        0xC3,
        0xB8, 0, 0, 0, 0
    };
    if (GuestCpu(branch).run() != 42) return EXIT_FAILURE;
    const std::array<std::uint8_t, 7> extended{
        0x41, 0xB8, 0x07, 0, 0, 0, 0xC3
    };
    GuestCpu extendedCpu(extended);
    if (extendedCpu.run() != 0 || extendedCpu.registerValue(8) != 7) return EXIT_FAILURE;
    const std::array<std::uint8_t, 2> loop{0xEB, 0xFE};
    rejected = false;
    try {
        GuestCpu(loop).run(8);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 2> invalidJump{0xEB, 0x7F};
    rejected = false;
    try {
        GuestCpu(invalidJump).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 17> nestedCall{
        0xE8, 0x06, 0, 0, 0,
        0xC3,
        0x90, 0x90, 0x90, 0x90, 0x90,
        0xB8, 0x2A, 0, 0, 0, 0xC3
    };
    GuestCpu callCpu(nestedCall);
    if (callCpu.run() != 42 || callCpu.registerValue(4) != 65536) return EXIT_FAILURE;
    const std::array<std::uint8_t, 8> stackCode{
        0xB8, 0x2A, 0, 0, 0, 0x50, 0x58, 0xC3
    };
    GuestCpu stackCpu(stackCode, 64);
    if (stackCpu.run() != 42 || stackCpu.registerValue(4) != 64) return EXIT_FAILURE;
    GuestCpu memoryCpu(stackCode, 64);
    memoryCpu.writeMemory64(8, 0x123456789ABCDEF0ULL);
    if (memoryCpu.readMemory64(8) != 0x123456789ABCDEF0ULL) return EXIT_FAILURE;
    rejected = false;
    try { memoryCpu.writeMemory64(60, 1); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return EXIT_FAILURE;
    rejected = false;
    try { memoryCpu.readMemory64(65); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 10> overflow{
        0xB8, 1, 0, 0, 0, 0x50, 0x50, 0x50, 0x50, 0xC3
    };
    rejected = false;
    try { GuestCpu(overflow, 16).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 1> underflow{0x58};
    rejected = false;
    try { GuestCpu(underflow, 16).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return EXIT_FAILURE;

    const std::array<std::uint8_t, 15> registerRoundtrip{
        0xB8, 0x2A, 0, 0, 0,
        0x89, 0xC1,
        0xB8, 0, 0, 0, 0,
        0x8B, 0xC1, 0xC3
    };
    if (GuestCpu(registerRoundtrip).run() != 42) return EXIT_FAILURE;
    const std::array<std::uint8_t, 19> displacementMov{
        0xB8, 0x2A, 0, 0, 0,
        0x89, 0x44, 0x24, 0xF8,
        0xB8, 0, 0, 0, 0,
        0x8B, 0x44, 0x24, 0xF8,
        0xC3
    };
    if (GuestCpu(displacementMov, 64).run() != 42) return EXIT_FAILURE;
    const std::array<std::uint8_t, 11> extendedMov{
        0xB8, 0x2A, 0, 0, 0,
        0x41, 0x89, 0xC0,
        0x41, 0x8B, 0xC0
    };
    GuestCpu rexCpu(extendedMov);
    rejected = false;
    try { rexCpu.run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected || rexCpu.registerValue(8) != 42) return EXIT_FAILURE;
    const std::array<std::uint8_t, 4> invalidAddress{0x8B, 0x04, 0x24, 0xC3};
    rejected = false;
    try { GuestCpu(invalidAddress, 64).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 2> truncatedModrm{0x8B, 0x04};
    rejected = false;
    try { GuestCpu(truncatedModrm).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return EXIT_FAILURE;

    // Use RSP-8 so the full operand fits inside the bounded guest memory.
    const std::array<std::uint8_t, 19> memoryRoundtrip{
        0xB8, 0x2A, 0, 0, 0,
        0x89, 0x44, 0x24, 0xF8,
        0xB8, 0, 0, 0, 0,
        0x8B, 0x44, 0x24, 0xF8,
        0xC3
    };
    GuestCpu memoryRoundtripCpu(memoryRoundtrip, 64);
    if (memoryRoundtripCpu.run() != 42 || memoryRoundtripCpu.readMemory32(56) != 42)
        return EXIT_FAILURE;

    // REX.W: 64-bit register -> memory -> register roundtrip.
    const std::array<std::uint8_t, 11> wideMemory{
        0x48, 0x89, 0x64, 0x24, 0xF8,
        0x48, 0x8B, 0x44, 0x24, 0xF8,
        0xC3
    };
    GuestCpu wideCpu(wideMemory, 64);
    if (wideCpu.run() != 64) return EXIT_FAILURE;
    if (wideCpu.readMemory64(56) != 64) return EXIT_FAILURE;

    // REX.R selects R8 as the destination register.
    const std::array<std::uint8_t, 10> rexRMov{
        0xB8, 0x2A, 0, 0, 0,
        0x44, 0x8B, 0xC0,
        0xC3, 0x90
    };
    GuestCpu rexRCpu(rexRMov);
    if (rexRCpu.run() != 42 || rexRCpu.registerValue(8) != 42)
        return EXIT_FAILURE;

    // SIB addressing: [RSP + RAX*2 - 16] resolves to address 56.
    const std::array<std::uint8_t, 15> sibIndexed{
        0xB8, 0x04, 0, 0, 0,
        0x8B, 0x44, 0x44, 0xF0,
        0xC3, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    GuestCpu sibCpu(sibIndexed, 64);
    sibCpu.writeMemory32(56, 42);
    if (sibCpu.run() != 42) return EXIT_FAILURE;

    // RIP-relative address uses the instruction pointer after disp32.
    const std::array<std::uint8_t, 7> ripRelative{
        0x8B, 0x05, 0x08, 0, 0, 0, 0xC3
    };
    GuestCpu ripCpu(ripRelative, 64);
    ripCpu.writeMemory32(14, 42);
    if (ripCpu.run() != 42) return EXIT_FAILURE;

    // Reject malformed and out-of-range operands.
    const std::array<std::uint8_t, 2> truncatedDisp{0x8B, 0x45};
    rejected = false;
    try { GuestCpu(truncatedDisp).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
