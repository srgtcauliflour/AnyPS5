#include "GuestCpu.hpp"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <stdexcept>

using anyps5::experimental::GuestCpu;

int main() {
    const std::array<std::uint8_t, 12> addition{
        0xB8, 0x28, 0, 0, 0,
        0x05, 0x02, 0, 0, 0,
        0xC3, 0x90
    };
    if (GuestCpu(addition).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 1> invalid{0x90};
    bool rejected = false;
    try {
        GuestCpu(invalid).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 5> truncated{0xB8, 1, 2, 3, 4};
    rejected = false;
    try {
        GuestCpu(truncated).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    rejected = false;
    try {
        GuestCpu(addition).run(1);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 18> branch{
        0xB8, 0x2A, 0, 0, 0,
        0x3D, 0x2A, 0, 0, 0,
        0x75, 0x05,
        0xC3,
        0xB8, 0, 0, 0, 0
    };
    if (GuestCpu(branch).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 7> extended{
        0x41, 0xB8, 0x07, 0, 0, 0, 0xC3
    };
    GuestCpu extendedCpu(extended);
    if (extendedCpu.run() != 0 || extendedCpu.registerValue(8) != 7) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 2> loop{0xEB, 0xFE};
    rejected = false;
    try {
        GuestCpu(loop).run(8);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 2> invalidJump{0xEB, 0x7F};
    rejected = false;
    try {
        GuestCpu(invalidJump).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 17> nestedCall{
        0xE8, 0x06, 0, 0, 0,
        0xC3,
        0x90, 0x90, 0x90, 0x90, 0x90,
        0xB8, 0x2A, 0, 0, 0, 0xC3
    };
    GuestCpu callCpu(nestedCall);
    if (callCpu.run() != 42 || callCpu.registerValue(4) != 65536) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 8> stackCode{
        0xB8, 0x2A, 0, 0, 0, 0x50, 0x58, 0xC3
    };
    GuestCpu stackCpu(stackCode, 64);
    if (stackCpu.run() != 42 || stackCpu.registerValue(4) != 64) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    GuestCpu memoryCpu(stackCode, 64);
    memoryCpu.writeMemory64(8, 0x123456789ABCDEF0ULL);
    if (memoryCpu.readMemory64(8) != 0x123456789ABCDEF0ULL) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    rejected = false;
    try { memoryCpu.writeMemory64(60, 1); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    rejected = false;
    try { memoryCpu.readMemory64(65); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 10> overflow{
        0xB8, 1, 0, 0, 0, 0x50, 0x50, 0x50, 0x50, 0xC3
    };
    rejected = false;
    try { GuestCpu(overflow, 16).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 1> underflow{0x58};
    rejected = false;
    try { GuestCpu(underflow, 16).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    const std::array<std::uint8_t, 15> registerRoundtrip{
        0xB8, 0x2A, 0, 0, 0,
        0x89, 0xC1,
        0xB8, 0, 0, 0, 0,
        0x8B, 0xC1, 0xC3
    };
    if (GuestCpu(registerRoundtrip).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 19> displacementMov{
        0xB8, 0x2A, 0, 0, 0,
        0x89, 0x44, 0x24, 0xF8,
        0xB8, 0, 0, 0, 0,
        0x8B, 0x44, 0x24, 0xF8,
        0xC3
    };
    if (GuestCpu(displacementMov, 64).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 11> extendedMov{
        0xB8, 0x2A, 0, 0, 0,
        0x41, 0x89, 0xC0,
        0x41, 0x8B, 0xC0
    };
    GuestCpu rexCpu(extendedMov);
    rejected = false;
    try { rexCpu.run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected || rexCpu.registerValue(8) != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 4> invalidAddress{0x8B, 0x04, 0x24, 0xC3};
    rejected = false;
    try { GuestCpu(invalidAddress, 64).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    const std::array<std::uint8_t, 2> truncatedModrm{0x8B, 0x04};
    rejected = false;
    try { GuestCpu(truncatedModrm).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

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
        { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // REX.W: 64-bit register -> memory -> register roundtrip.
    const std::array<std::uint8_t, 11> wideMemory{
        0x48, 0x89, 0x64, 0x24, 0xF8,
        0x48, 0x8B, 0x44, 0x24, 0xF8,
        0xC3
    };
    GuestCpu wideCpu(wideMemory, 64);
    if (wideCpu.run() != 64) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    if (wideCpu.readMemory64(56) != 64) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // REX.R selects R8 as the destination register.
    const std::array<std::uint8_t, 10> rexRMov{
        0xB8, 0x2A, 0, 0, 0,
        0x44, 0x8B, 0xC0,
        0xC3, 0x90
    };
    GuestCpu rexRCpu(rexRMov);
    if (rexRCpu.run() != 42 || rexRCpu.registerValue(8) != 42)
        { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // SIB addressing: [RSP + RAX*2 - 16] resolves to address 56.
    const std::array<std::uint8_t, 15> sibIndexed{
        0xB8, 0x04, 0, 0, 0,
        0x8B, 0x44, 0x44, 0xF0,
        0xC3, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    GuestCpu sibCpu(sibIndexed, 64);
    sibCpu.writeMemory32(56, 42);
    if (sibCpu.run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // RIP-relative address uses the instruction pointer after disp32.
    const std::array<std::uint8_t, 7> ripRelative{
        0x8B, 0x05, 0x08, 0, 0, 0, 0xC3
    };
    GuestCpu ripCpu(ripRelative, 64);
    ripCpu.writeMemory32(14, 42);
    if (ripCpu.run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // Reject malformed and out-of-range operands.
    const std::array<std::uint8_t, 2> truncatedDisp{0x8B, 0x45};
    rejected = false;
    try { GuestCpu(truncatedDisp).run(); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // Register arithmetic (ADD r32,r/m32 and SUB r32,r/m32).
    // The final ADD immediate is executed before the terminal RET.
    const std::array<std::uint8_t, 20> arithmeticReturn{
        0xB8, 40, 0, 0, 0,
        0xB9, 4, 0, 0, 0,
        0x03, 0xC1,
        0x2B, 0xC1,
        0x05, 2, 0, 0, 0,
        0xC3
    };
    if (GuestCpu(arithmeticReturn).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // Store 40 at [RSP-8], add 2 directly in guest memory, reload 42.
    const std::array<std::uint8_t, 23> memoryArithmetic{
        0xB8, 40, 0, 0, 0,
        0x89, 0x44, 0x24, 0xF8,
        0xB8, 2, 0, 0, 0,
        0x01, 0x44, 0x24, 0xF8,
        0x8B, 0x44, 0x24, 0xF8,
        0xC3
    };
    GuestCpu memArithmeticCpu(memoryArithmetic, 64);
    if (memArithmeticCpu.run() != 42 || memArithmeticCpu.readMemory32(56) != 42)
        { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // CMP sets ZF without changing the source registers.
    const std::array<std::uint8_t, 16> compareEqual{
        0xB8, 42, 0, 0, 0,
        0xB9, 42, 0, 0, 0,
        0x39, 0xC8,
        0x75, 0x01,
        0xC3, 0x90
    };
    if (GuestCpu(compareEqual).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // TEST updates ZF without writing its operand.
    const std::array<std::uint8_t, 11> testNonzero{
        0xB8, 42, 0, 0, 0,
        0x85, 0xC0,
        0x74, 0x01,
        0xC3, 0x90
    };
    if (GuestCpu(testNonzero).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // REX.W ADD also accepts the 64-bit operand width.
    const std::array<std::uint8_t, 4> wideAdd{
        0x48, 0x03, 0xC1, 0xC3
    };
    GuestCpu wideAddCpu(wideAdd);
    if (wideAddCpu.run() != 0) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // Signed comparison: -1 is less than +1 (JL uses SF != OF).
    const std::array<std::uint8_t, 21> signedLess{
        0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
        0xB9, 1, 0, 0, 0,
        0x39, 0xC8,
        0x7C, 0x01,
        0x90,
        0xB8, 42, 0, 0, 0, 0xC3
    };
    // Branch skips an intentionally unsupported NOP when the condition is true.
    if (GuestCpu(signedLess).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // Unsigned 0xFFFFFFFF is above 1 (JA uses !CF && !ZF).
    const std::array<std::uint8_t, 21> unsignedAbove{
        0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
        0xB9, 1, 0, 0, 0,
        0x39, 0xC8,
        0x77, 0x01,
        0x90,
        0xB8, 42, 0, 0, 0, 0xC3
    };
    if (GuestCpu(unsignedAbove).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // Near conditional branch (0F 8C rel32) skips an invalid opcode.
    const std::array<std::uint8_t, 25> nearSignedLess{
        0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
        0xB9, 1, 0, 0, 0,
        0x39, 0xC8,
        0x0F, 0x8C, 1, 0, 0, 0,
        0x90,
        0xB8, 42, 0, 0, 0, 0xC3
    };
    if (GuestCpu(nearSignedLess).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // Overflow: 0x7FFFFFFF + 1 becomes negative, but signed JL is false (SF == OF).
    const std::array<std::uint8_t, 23> overflowBranch{
        0xB8, 0xFF, 0xFF, 0xFF, 0x7F,
        0xB9, 1, 0, 0, 0,
        0x03, 0xC1,
        0x7C, 0x01,
        0xEB, 0x01,
        0x90,
        0xB8, 42, 0, 0, 0, 0xC3
    };
    if (GuestCpu(overflowBranch).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    // REX.W + B8 encodes a full 64-bit immediate (movabs rax, imm64).
    const std::array<std::uint8_t, 11> movabs{
        0x48, 0xB8, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12, 0xC3
    };
    if (GuestCpu(movabs).run() != 0x123456789ABCDEF0ULL) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }

    // 32-bit register destinations must clear the high 32 bits.
    const std::array<std::uint8_t, 18> zeroExtend{
        0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xB9, 42, 0, 0, 0,
        0x8B, 0xC1, 0xC3
    };
    if (GuestCpu(zeroExtend).run() != 42) { std::fprintf(stderr, "GuestCpuTests failed at line %d\n", __LINE__); return EXIT_FAILURE; }
    // LEA calculates RSP-8 without dereferencing the address.
    const std::array<std::uint8_t, 6> leaStack{
        0x48, 0x8D, 0x44, 0x24, 0xF8, 0xC3
    };
    if (GuestCpu(leaStack, 64).run() != 56) {
        std::fprintf(stderr, "LEA stack calculation failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // A conventional frame setup/teardown restores RSP and RBP.
    const std::array<std::uint8_t, 11> framedFunction{
        0x55, 0x48, 0x89, 0xE5,
        0xB8, 42, 0, 0, 0,
        0xC9, 0xC3
    };
    GuestCpu frameCpu(framedFunction, 64);
    if (frameCpu.run() != 42 || frameCpu.registerValue(4) != 64 ||
        frameCpu.registerValue(5) != 0) {
        std::fprintf(stderr, "LEAVE frame restoration failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // Realistic function-shaped machine code: CALL, PUSH RBP, MOV RBP,RSP,
    // LEA EAX,[RDI+2], POP RBP, RET. RDI starts at zero.
    const std::array<std::uint8_t, 15> compiledStyleWithRet{
        0xE8, 0x01, 0, 0, 0, 0xC3,
        0x55, 0x48, 0x89, 0xE5,
        0x8D, 0x47, 0x02, 0x5D, 0xC3
    };
    GuestCpu compiledCpu(compiledStyleWithRet, 64);
    if (compiledCpu.run() != 2 || compiledCpu.registerValue(4) != 64) {
        std::fprintf(stderr, "Compiler-style function failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // LEA with ModR/M register-direct addressing is invalid.
    const std::array<std::uint8_t, 3> invalidLea{0x8D, 0xC0, 0xC3};
    rejected = false;
    try { GuestCpu(invalidLea).run(); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) {
        std::fprintf(stderr, "Invalid LEA was accepted at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // Compiled from tests/compiler_fixture.c with Clang -O1 targeting x86-64.
    // 8D 47 02 = lea eax,[rdi+2]; C3 = ret.
    // The caller sets EDI=40 and CALLs the compiler-produced function.
    const std::array<std::uint8_t, 15> actualCompilerFixture{
        0xBF, 40, 0, 0, 0,        // mov edi,40
        0xE8, 0x01, 0, 0, 0,      // call guest_add_two
        0xC3,                    // caller ret
        0x8D, 0x47, 0x02, 0xC3  // compiled .text from fixture
    };
    GuestCpu actualCompilerCpu(actualCompilerFixture, 64);
    if (actualCompilerCpu.run() != 42 || actualCompilerCpu.registerValue(4) != 64) {
        std::fprintf(stderr, "Real compiler fixture failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // Immediate ADD/SUB/CMP, including negative sign-extended imm8.
    const std::array<std::uint8_t, 18> immediateArithmetic{
        0xB8, 40, 0, 0, 0,
        0x83, 0xC0, 0x05,       // add eax,5
        0x83, 0xE8, 0x03,       // sub eax,3
        0x83, 0xF8, 42,         // cmp eax,42
        0x75, 0x01,             // jne invalid opcode
        0xC3, 0x90
    };
    if (GuestCpu(immediateArithmetic).run() != 42) {
        std::fprintf(stderr, "Immediate arithmetic failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }
    // Signed IMUL: -7 * -6 = 42, 32-bit result zero-extended.
    const std::array<std::uint8_t, 15> signedMultiply{
        0xB8, 0xF9, 0xFF, 0xFF, 0xFF,
        0xB9, 0xFA, 0xFF, 0xFF, 0xFF,
        0x0F, 0xAF, 0xC1,
        0xC3, 0x90
    };
    if (GuestCpu(signedMultiply).run() != 42) {
        std::fprintf(stderr, "Signed multiplication failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }
    // 64-bit signed IMUL: -7 * -6 = 42.
    const std::array<std::uint8_t, 25> wideSignedMultiply{
        0x48, 0xB8, 0xF9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x48, 0xB9, 0xFA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x48, 0x0F, 0xAF, 0xC1, 0xC3
    };
    if (GuestCpu(wideSignedMultiply).run() != 42) {
        std::fprintf(stderr, "64-bit signed multiplication failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // 2^62 * 4 overflows signed 64-bit. JO must skip the unsupported NOP.
    const std::array<std::uint8_t, 28> wideMultiplyOverflow{
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0x00, 0x40,
        0xB9, 4, 0, 0, 0,
        0x48, 0x0F, 0xAF, 0xC1,
        0x70, 0x01,
        0x90,
        0xB8, 42, 0, 0, 0, 0xC3
    };
    if (GuestCpu(wideMultiplyOverflow).run() != 42) {
        std::fprintf(stderr, "64-bit IMUL overflow flag failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // -2^63 * -1 overflows even though the truncated product is -2^63.
    const std::array<std::uint8_t, 33> signedBoundaryOverflow{
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0x80,
        0x48, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x48, 0x0F, 0xAF, 0xC1,
        0x70, 0x01,
        0x90,
        0xB8, 42, 0, 0, 0, 0xC3
    };
    if (GuestCpu(signedBoundaryOverflow).run() != 42) {
        std::fprintf(stderr, "INT64_MIN multiplication overflow failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }
    // M9: loop sum(1..6) using DEC ECX and JNE back-edge.
    const auto sumLoop = std::to_array<std::uint8_t>({
        0xB8, 0, 0, 0, 0,       // mov eax,0
        0xB9, 6, 0, 0, 0,       // mov ecx,6
        0x03, 0xC1,             // add eax,ecx
        0xFF, 0xC9,             // dec ecx
        0x75, 0xFA,             // jne -6 (back to add)
        0xC3
    });
    if (GuestCpu(sumLoop).run() != 21) {
        std::fprintf(stderr, "M9 decrement loop failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // INC a memory operand in a loop, then load the accumulated value.
    const auto memoryLoop = std::to_array<std::uint8_t>({
        0xB8, 0, 0, 0, 0,       // mov eax,0
        0x89, 0x44, 0x24, 0xF8, // mov [rsp-8],eax
        0xB9, 3, 0, 0, 0,       // mov ecx,3
        0xFF, 0x44, 0x24, 0xF8, // inc dword [rsp-8]
        0xFF, 0xC9,             // dec ecx
        0x75, 0xF8,             // jne -8 (back to inc)
        0x8B, 0x44, 0x24, 0xF8, // mov eax,[rsp-8]
        0xC3
    });
    GuestCpu memoryLoopCpu(memoryLoop, 64);
    if (memoryLoopCpu.run() != 3 || memoryLoopCpu.readMemory32(56) != 3) {
        std::fprintf(stderr, "M9 memory loop failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // Group 81 imm32: ADD 0x10000 then CMP, including the wide sign extension.
    const auto imm32Arithmetic = std::to_array<std::uint8_t>({
        0xB8, 0, 0, 0, 0,
        0x81, 0xC0, 0, 0, 1, 0,  // add eax,65536
        0x81, 0xF8, 0, 0, 1, 0,  // cmp eax,65536
        0x75, 0x01, 0xC3, 0x90
    });
    if (GuestCpu(imm32Arithmetic).run() != 65536) {
        std::fprintf(stderr, "M9 imm32 arithmetic failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    // Carry flag must survive INC and DEC (unlike ADD/SUB).
    const auto carryPreserved = std::to_array<std::uint8_t>({
        0xB8, 0, 0, 0, 0,       // mov eax,0
        0x83, 0xE8, 0x01,       // sub eax,1 (CF=1)
        0xFF, 0xC0,             // inc eax (CF must remain 1)
        0x72, 0x01,             // jc +1
        0x90,                   // invalid if CF lost
        0xFF, 0xC8,             // dec eax (CF must remain 1)
        0x72, 0x01,             // jc +1
        0x90,
        0xB8, 42, 0, 0, 0, 0xC3
    });
    if (GuestCpu(carryPreserved).run() != 42) {
        std::fprintf(stderr, "M9 INC/DEC carry preservation failed at line %d\\n", __LINE__);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
