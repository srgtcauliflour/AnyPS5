#include "GuestCpu.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

using anyps5::experimental::GuestCpu;

static void put32(std::vector<std::uint8_t>& code, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        code.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

static bool check(const char* name, std::uint64_t actual, std::uint64_t expected) {
    if (actual == expected) return true;
    std::fprintf(stderr, "%s: expected %llu, got %llu\n", name,
        static_cast<unsigned long long>(expected),
        static_cast<unsigned long long>(actual));
    return false;
}

int main() {
    // A complete loop: sum 1..n, with a signed conditional branch.
    // mov edi,n; xor eax,eax; mov ecx,1; add eax,ecx;
    // inc ecx; cmp ecx,edi; jle loop; ret.
    for (std::uint32_t n = 1; n <= 128; ++n) {
        std::vector<std::uint8_t> code{0xBF};
        put32(code, n);
        const std::array<std::uint8_t, 14> body{
            0x31, 0xC0,             // xor eax,eax
            0xB9, 1, 0, 0, 0,       // mov ecx,1
            0x01, 0xC8,             // add eax,ecx
            0xFF, 0xC1,             // inc ecx
            0x39, 0xF9,             // cmp ecx,edi
            0x7E                    // jle rel8 (displacement appended below)
        };
        code.insert(code.end(), body.begin(), body.end());
        code.push_back(0xF8); // jump back 8 bytes to add eax,ecx
        code.push_back(0xC3);
        if (!check("sum loop", GuestCpu(code).run(1000),
                   static_cast<std::uint64_t>(n) * (n + 1) / 2))
            return EXIT_FAILURE;
    }

    // Combined bitwise data path: shifts, OR, AND and return.
    // mov edi,x; lea eax,[rdi*4]; shr edi,3; or eax,edi;
    // and eax,255; ret.
    for (std::uint32_t x = 0; x < 512; ++x) {
        std::vector<std::uint8_t> code{0xBF};
        put32(code, x);
        const std::array<std::uint8_t, 17> body{
            0x8D, 0x04, 0xBD, 0, 0, 0, 0,
            0xC1, 0xEF, 0x03,
            0x09, 0xF8,
            0x81, 0xE0, 0xFF, 0, 0
        };
        code.insert(code.end(), body.begin(), body.end());
        code.push_back(0);
        code.push_back(0xC3);
        if (!check("bitmix", GuestCpu(code).run(),
                   ((x << 2) | (x >> 3)) & 255u))
            return EXIT_FAILURE;
    }

    // Full stack-frame call and return, including 16-bit and byte writes.
    // call function; ret; function: push rbp; mov rbp,rsp;
    // sub rsp,16; mov word [rbp-2],42; movzx eax,word [rbp-2];
    // leave; ret.
    const std::array<std::uint8_t, 25> stackFunction{
        0xE8, 1, 0, 0, 0, 0xC3,
        0x55, 0x48, 0x89, 0xE5,
        0x48, 0x83, 0xEC, 0x10,
        0x66, 0xC7, 0x45, 0xFE, 42, 0,
        0x0F, 0xB7, 0x45, 0xFE, 0xC9
    };
    std::vector<std::uint8_t> stackCode(stackFunction.begin(), stackFunction.end());
    stackCode.push_back(0xC3);
    GuestCpu stackCpu(stackCode, 128);
    if (!check("stack-frame function", stackCpu.run(), 42) ||
        !check("stack restored", stackCpu.registerValue(4), 128))
        return EXIT_FAILURE;

    // Diagnostics must include the unsupported opcode and its offset.
    const std::array<std::uint8_t, 2> unsupported{0x90, 0xF4};
    try {
        (void)GuestCpu(unsupported).run();
        std::fprintf(stderr, "unsupported opcode unexpectedly executed\n");
        return EXIT_FAILURE;
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        if (message.find("244") == std::string::npos ||
            message.find("offset 1") == std::string::npos) {
            std::fprintf(stderr, "insufficient opcode diagnostics: %s\n", error.what());
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
