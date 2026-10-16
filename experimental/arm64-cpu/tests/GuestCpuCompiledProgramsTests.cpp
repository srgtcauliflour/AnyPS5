#include "GuestCpu.hpp"
#include "stage3_generated.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>
#include <vector>

using anyps5::experimental::GuestCpu;
extern "C" unsigned fixture_triangular(unsigned);
extern "C" unsigned fixture_bitmix(unsigned);
extern "C" unsigned fixture_popcount(unsigned);
extern "C" unsigned fixture_factorial(unsigned);
extern "C" unsigned fixture_fib(unsigned);

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
    const std::array<Fixture, 5> fixtures{{
        {"triangular", stage3_generated::fixture_triangular, fixture_triangular, 128},
        {"bitmix", stage3_generated::fixture_bitmix, fixture_bitmix, 511},
        {"popcount", stage3_generated::fixture_popcount, fixture_popcount, 255},
        {"factorial", stage3_generated::fixture_factorial, fixture_factorial, 12},
        {"fibonacci", stage3_generated::fixture_fib, fixture_fib, 32}
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
    std::printf("STAGE3 COMPATIBILITY %u/%u (%.2f%%) unsupported=%u mismatches=%u errors=%u\n",
        passed, total, total ? 100.0 * passed / total : 0.0, unsupported, mismatch, other);
    return passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
