#include "GuestRuntime.hpp"
#include <limits>
#include <stdexcept>
namespace anyps5::experimental {
GuestRuntime::GuestRuntime(std::vector<std::uint8_t> code, std::size_t memorySize,
                           std::size_t dataSize, std::size_t stackBase)
    : cpu_(code, memorySize) {
    cpu_.setMemoryRegions(dataBase, dataBase + dataSize, stackBase);
}
GuestRuntime GuestRuntime::load(std::span<const std::uint8_t> image,
                                std::size_t stackSize) {
    if (image.size() < 20 || image[0] != 'A' || image[1] != 'G' ||
        image[2] != '6' || image[3] != '4')
        throw std::invalid_argument("Invalid AG64 image magic or header");
    const auto read32 = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(image[offset]) |
            (static_cast<std::uint32_t>(image[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(image[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(image[offset + 3]) << 24);
    };
    const auto version = read32(4);
    const auto codeSize = static_cast<std::size_t>(read32(8));
    const auto dataSize = static_cast<std::size_t>(read32(12));
    const auto entry = static_cast<std::size_t>(read32(16));
    if (version != 1 || !codeSize || entry >= codeSize ||
        codeSize > image.size() - 20 || dataSize > image.size() - 20 - codeSize ||
        image.size() != 20 + codeSize + dataSize)
        throw std::invalid_argument("Invalid AG64 image sections or entry");
    if (dataSize == 0 || dataSize > (1u << 20) || stackSize < 128 ||
        stackSize > (1u << 20))
        throw std::invalid_argument("Invalid AG64 data/stack size");
    const auto stackBase = static_cast<std::size_t>(dataBase) + dataSize + 4096;
    const auto memorySize = stackBase + stackSize;
    std::vector<std::uint8_t> code(image.begin() + 20,
                                   image.begin() + 20 + codeSize);
    GuestRuntime runtime(std::move(code), memorySize, dataSize, stackBase);
    // Initialize only mapped data; the guard page is intentionally unmapped.
    for (std::size_t i = 0; i < dataSize; ++i) {
        const auto address = dataBase + i;
        const auto value = image[20 + codeSize + i];
        // No public byte memory writer yet; write aligned words.
        // Write the final partial word with zero padding only inside data.
        if ((i % 4) == 0) {
            if (dataSize - i >= 4) {
                std::uint32_t word = 0;
                for (std::size_t j = 0; j < 4; ++j)
                    word |= static_cast<std::uint32_t>(image[20 + codeSize + i + j]) << (8 * j);
                runtime.cpu_.writeMemory32(address, word);
            } else if (value != 0) {
                throw std::invalid_argument("AG64 data size must be word aligned");
            }
        }
    }
    runtime.cpu_.setRegister(4, memorySize);
    // Entry offset is encoded by prepending a near jump in the next
    // revision; v1 currently only supports entry zero.
    if (entry != 0)
        throw std::invalid_argument("AG64 v1 requires entry offset zero");
    return runtime;
}
std::uint64_t GuestRuntime::run(std::size_t instructionLimit) {
    return cpu_.run(instructionLimit);
}
}
