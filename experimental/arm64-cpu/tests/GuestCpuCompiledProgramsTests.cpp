#include "GuestCpu.hpp"
#include "stage3_generated.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <vector>

using anyps5::experimental::GuestCpu;
extern "C" unsigned fixture_triangular(unsigned);
extern "C" unsigned fixture_bitmix(unsigned);
extern "C" unsigned fixture_popcount(unsigned);
extern "C" unsigned fixture_factorial(unsigned);
extern "C" unsigned fixture_fib(unsigned);
extern "C" unsigned fixture_gcd(unsigned);
extern "C" unsigned fixture_digit_sum(unsigned);
extern "C" unsigned fixture_reverse_bits(unsigned);
extern "C" unsigned fixture_array_sum(const unsigned*, unsigned);
extern "C" unsigned fixture_array_sort(unsigned*, unsigned);

struct Fixture {
    const char* name;
    std::span<const std::uint8_t> bytes;
    unsigned (*reference)(unsigned);
    unsigned lastInput;
};
static std::vector<std::uint8_t> wrap(std::span<const std::uint8_t> body, unsigned argument) {
    // SysV AMD64 integer arg 0 in EDI, result in EAX. RET terminates
    // the interpreter at callDepth==0. No guest OS or loader is assumed.
    std::vector<std::uint8_t> code{0xBF};
    for (unsigned i = 0; i < 4; ++i)
        code.push_back(static_cast<std::uint8_t>(argument >> (8 * i)));
    code.insert(code.end(), body.begin(), body.end());
    return code;
}
int main() {
    const std::array<Fixture, 8> fixtures{{
        {"triangular", stage3_generated::fixture_triangular, fixture_triangular, 128},
        {"bitmix", stage3_generated::fixture_bitmix, fixture_bitmix, 511},
        {"popcount", stage3_generated::fixture_popcount, fixture_popcount, 255},
        {"factorial", stage3_generated::fixture_factorial, fixture_factorial, 12},
        {"fibonacci", stage3_generated::fixture_fib, fixture_fib, 32},
        {"gcd", stage3_generated::fixture_gcd, fixture_gcd, 255},
        {"digit_sum", stage3_generated::fixture_digit_sum, fixture_digit_sum, 255},
        {"reverse_bits", stage3_generated::fixture_reverse_bits, fixture_reverse_bits, 255}
    }};
    unsigned total = 0, passed = 0, unsupported = 0, mismatch = 0, other = 0;
    for (const auto& fixture : fixtures) {
        unsigned fixturePassed = 0;
        for (unsigned input = 0; input <= fixture.lastInput; ++input) {
            ++total;
            const auto expected = fixture.reference(input);
            try {
                const auto code = wrap(fixture.bytes, input);
                const auto actual = GuestCpu(code, 65536).run(100000);
                if (actual == expected) {
                    ++passed;
                    ++fixturePassed;
                } else {
                    ++mismatch;
                    std::fprintf(stderr, "MISMATCH %s(%u) expected=%u actual=%llu\n",
                        fixture.name, input, expected,
                        static_cast<unsigned long long>(actual));
                }
            } catch (const std::exception& e) {
                const std::string message(e.what());
                if (message.find("Unsupported guest opcode") != std::string::npos ||
                    message.find("Unsupported") != std::string::npos) ++unsupported;
                else ++other;
                std::fprintf(stderr, "ERROR %s(%u) category=%s detail=%s\n",
                    fixture.name, input, message.find("Unsupported") != std::string::npos ?
                    "unsupported-instruction" : "execution-error", e.what());
            }
        }
        std::printf("CORPUS %s %u/%u passed\n", fixture.name, fixturePassed,
                    fixture.lastInput + 1);
    }
    // Compiler-generated pointer/array fixtures use a bounded guest-memory
    // region at address 4096, distinct from the top-of-stack at 65536.
    // RDI=guest pointer, ESI=count; native reference uses a separate copy.
    constexpr std::uint32_t guestArrayAddress = 4096;
    for (const bool sorting : {false, true}) {
        const auto body = sorting ? std::span<const std::uint8_t>(stage3_generated::fixture_array_sort)
                                  : std::span<const std::uint8_t>(stage3_generated::fixture_array_sum);
        for (unsigned length = 0; length <= 32; ++length) {
            std::array<unsigned, 32> reference{};
            for (unsigned i = 0; i < length; ++i)
                reference[i] = (i * 71u + length * 29u) % 101u;
            const auto original = reference;
            const auto expected = sorting ? fixture_array_sort(reference.data(), length)
                                          : fixture_array_sum(reference.data(), length);
            std::vector<std::uint8_t> code{0xBF};
            for (unsigned i = 0; i < 4; ++i)
                code.push_back(static_cast<std::uint8_t>(guestArrayAddress >> (i * 8)));
            code.push_back(0xBE);
            for (unsigned i = 0; i < 4; ++i)
                code.push_back(static_cast<std::uint8_t>(length >> (i * 8)));
            code.insert(code.end(), body.begin(), body.end());
            ++total;
            try {
                GuestCpu cpu(code, 65536);
                for (unsigned i = 0; i < length; ++i)
                    cpu.writeMemory32(guestArrayAddress + 4u * i, original[i]);
                const auto actual = cpu.run(100000);
                bool memoryMatches = true;
                for (unsigned i = 0; i < length; ++i)
                    memoryMatches &= cpu.readMemory32(guestArrayAddress + 4u * i) == reference[i];
                if (actual == expected && memoryMatches) ++passed;
                else {
                    ++mismatch;
                    std::fprintf(stderr, "MISMATCH %s(%u) return=%llu expected=%u memory=%d\\n",
                        sorting ? "array_sort" : "array_sum", length,
                        static_cast<unsigned long long>(actual), expected, memoryMatches);
                }
            } catch (const std::exception& e) {
                const std::string message(e.what());
                if (message.find("Unsupported") != std::string::npos) ++unsupported;
                else ++other;
                std::fprintf(stderr, "ERROR %s(%u): %s\\n",
                    sorting ? "array_sort" : "array_sum", length, e.what());
            }
        }
    }
    std::printf("STAGE3 COMPATIBILITY %u/%u (%.2f%%) unsupported=%u mismatches=%u errors=%u\n",
        passed, total, total ? 100.0 * passed / total : 0.0, unsupported, mismatch, other);
    return passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
