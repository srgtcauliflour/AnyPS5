#include "GuestCpu.hpp"
#include <limits>

namespace anyps5::experimental {
GuestCpu::GuestCpu(std::span<const std::uint8_t> program, std::size_t stackBytes)
    : program_(program.begin(), program.end()), memory_(stackBytes, 0) {
    if (stackBytes < 8 || stackBytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Invalid guest stack size");
    }
    registers_[4] = stackBytes;
}

std::uint8_t GuestCpu::read8() {
    if (ip_ >= program_.size()) throw std::runtime_error("Guest instruction exceeds code bounds");
    return program_[ip_++];
}

std::uint32_t GuestCpu::read32() {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(read8()) << shift;
    return value;
}

std::int32_t GuestCpu::readSigned32() {
    const auto bits = read32();
    const std::int64_t signedValue = bits <= INT32_MAX ? static_cast<std::int64_t>(bits) : static_cast<std::int64_t>(bits) - (std::int64_t{1} << 32);
    return static_cast<std::int32_t>(signedValue);
}

void GuestCpu::jumpRelative(std::int32_t displacement) {
    const auto destination = static_cast<std::int64_t>(ip_) + displacement;
    if (destination < 0 || static_cast<std::uint64_t>(destination) >= program_.size()) {
        throw std::runtime_error("Guest branch outside code bounds");
    }
    ip_ = static_cast<std::size_t>(destination);
}

std::uint64_t GuestCpu::readMemory64(std::uint64_t address) const {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 8) {
        throw std::runtime_error("Guest memory read out of bounds");
    }
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{memory_[static_cast<std::size_t>(address) + i]} << (i * 8);
    return value;
}

void GuestCpu::writeMemory64(std::uint64_t address, std::uint64_t value) {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 8) {
        throw std::runtime_error("Guest memory write out of bounds");
    }
    for (unsigned i = 0; i < 8; ++i) memory_[static_cast<std::size_t>(address) + i] = static_cast<std::uint8_t>(value >> (i * 8));
}


std::uint32_t GuestCpu::readMemory32(std::uint64_t address) const {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 4)
        throw std::runtime_error("Guest memory read out of bounds");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t{memory_[static_cast<std::size_t>(address) + i]} << (i * 8);
    return value;
}

void GuestCpu::writeMemory32(std::uint64_t address, std::uint32_t value) {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 4)
        throw std::runtime_error("Guest memory write out of bounds");
    for (unsigned i = 0; i < 4; ++i)
        memory_[static_cast<std::size_t>(address) + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

GuestCpu::Operand GuestCpu::decodeOperand(std::uint8_t modrm, bool rexB, bool rexX) {
    const auto mod = static_cast<unsigned>(modrm >> 6);
    const auto rm = static_cast<unsigned>(modrm & 7);
    if (mod == 3) return {true, rm + (rexB ? 8u : 0u), 0};
    std::uint64_t base = 0;
    std::uint64_t index = 0;
    unsigned scale = 1;
    bool hasBase = true;
    bool ripRelative = false;
    if (rm == 4) {
        const auto sib = read8();
        scale = 1u << (sib >> 6);
        const auto indexBits = static_cast<unsigned>((sib >> 3) & 7);
        const auto baseBits = static_cast<unsigned>(sib & 7);
        if (indexBits != 4 || rexX)
            index = registers_[indexBits + (rexX ? 8u : 0u)];
        if (mod == 0 && baseBits == 5) hasBase = false;
        else base = registers_[baseBits + (rexB ? 8u : 0u)];
    } else if (mod == 0 && rm == 5) {
        hasBase = false;
        ripRelative = true;
    } else {
        base = registers_[rm + (rexB ? 8u : 0u)];
    }
    std::int64_t displacement = 0;
    if (mod == 1) displacement = static_cast<std::int8_t>(read8());
    if (mod == 2 || (mod == 0 && !hasBase)) displacement = readSigned32();
    if (ripRelative) base = ip_;
    return {false, 0, base + index * scale + static_cast<std::uint64_t>(displacement)};
}

std::uint64_t GuestCpu::readOperand(const Operand& operand, bool wide) const {
    if (operand.isRegister) {
        const auto value = registers_[operand.index];
        return wide ? value : static_cast<std::uint32_t>(value);
    }
    return wide ? readMemory64(operand.address) : readMemory32(operand.address);
}

void GuestCpu::writeOperand(const Operand& operand, std::uint64_t value, bool wide) {
    if (operand.isRegister) {
        registers_[operand.index] = wide ? value : static_cast<std::uint32_t>(value);
    } else if (wide) {
        writeMemory64(operand.address, value);
    } else {
        writeMemory32(operand.address, static_cast<std::uint32_t>(value));
    }
}

void GuestCpu::push64(std::uint64_t value) {
    if (registers_[4] < 8 || registers_[4] > memory_.size()) throw std::runtime_error("Guest stack overflow");
    const auto next = registers_[4] - 8;
    writeMemory64(next, value);
    registers_[4] = next;
}

std::uint64_t GuestCpu::pop64() {
    const auto address = registers_[4];
    const auto value = readMemory64(address);
    registers_[4] += 8;
    return value;
}

void GuestCpu::updateArithmeticFlags(std::uint64_t lhs, std::uint64_t rhs,
                                     std::uint64_t result, bool wide, bool subtraction) {
    const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
    const auto sign = wide ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 31);
    lhs &= mask;
    rhs &= mask;
    result &= mask;
    zeroFlag_ = result == 0;
    signFlag_ = (result & sign) != 0;
    if (subtraction) {
        carryFlag_ = lhs < rhs;
        overflowFlag_ = ((lhs ^ rhs) & (lhs ^ result) & sign) != 0;
    } else {
        carryFlag_ = result < lhs;
        overflowFlag_ = ((~(lhs ^ rhs)) & (lhs ^ result) & sign) != 0;
    }
}

bool GuestCpu::evaluateCondition(std::uint8_t condition) const {
    switch (condition & 0x0F) {
    case 0x0: return overflowFlag_;
    case 0x1: return !overflowFlag_;
    case 0x2: return carryFlag_;
    case 0x3: return !carryFlag_;
    case 0x4: return zeroFlag_;
    case 0x5: return !zeroFlag_;
    case 0x6: return carryFlag_ || zeroFlag_;
    case 0x7: return !carryFlag_ && !zeroFlag_;
    case 0x8: return signFlag_;
    case 0x9: return !signFlag_;
    case 0xC: return signFlag_ != overflowFlag_;
    case 0xD: return signFlag_ == overflowFlag_;
    case 0xE: return zeroFlag_ || (signFlag_ != overflowFlag_);
    case 0xF: return !zeroFlag_ && (signFlag_ == overflowFlag_);
    default: throw std::runtime_error("Unsupported condition code");
    }
}

std::uint64_t GuestCpu::run(std::size_t maxInstructions) {
    for (std::size_t count = 0; count < maxInstructions; ++count) {
        bool wide = false;
        bool extended = false;
        bool rexR = false;
        bool rexX = false;
        bool hasRex = false;
        auto opcode = read8();
        if (opcode >= 0x40 && opcode <= 0x4F) {
            hasRex = true;
            wide = (opcode & 8) != 0;
            extended = (opcode & 1) != 0;
            rexR = (opcode & 4) != 0;
            rexX = (opcode & 2) != 0;
            opcode = read8();
        }
        // LEA computes the effective address without reading guest memory.
        if (opcode == 0x8D) {
            const auto modrm = read8();
            if ((modrm >> 6) == 3)
                throw std::runtime_error("LEA requires a memory addressing operand");
            const auto reg = static_cast<std::size_t>(((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto operand = decodeOperand(modrm, extended, rexX);
            registers_[reg] = wide ? operand.address : static_cast<std::uint32_t>(operand.address);
            continue;
        }
        if (opcode == 0x89 || opcode == 0x8B) {
            const auto modrm = read8();
            const auto reg = static_cast<std::size_t>(((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto operand = decodeOperand(modrm, extended, rexX);
            if (opcode == 0x89) writeOperand(operand, registers_[reg], wide);
            else registers_[reg] = readOperand(operand, wide);
            continue;
        }
        // ModR/M arithmetic and logic: ADD, OR, AND, SUB, XOR, CMP, TEST.
        // Operands are 32-bit by default and 64-bit with REX.W.
        if (opcode == 0x01 || opcode == 0x03 ||
            opcode == 0x09 || opcode == 0x0B ||
            opcode == 0x21 || opcode == 0x23 ||
            opcode == 0x29 || opcode == 0x2B ||
            opcode == 0x31 || opcode == 0x33 ||
            opcode == 0x39 || opcode == 0x3B || opcode == 0x85) {
            const auto modrm = read8();
            const auto reg = static_cast<std::size_t>(((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto operand = decodeOperand(modrm, extended, rexX);
            const bool reversed = opcode == 0x03 || opcode == 0x0B || opcode == 0x23 ||
                                  opcode == 0x2B || opcode == 0x33 || opcode == 0x3B;
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto lhs = (reversed ? registers_[reg] : readOperand(operand, wide)) & mask;
            const auto rhs = (reversed ? readOperand(operand, wide) : registers_[reg]) & mask;
            std::uint64_t result = 0;
            if (opcode == 0x01 || opcode == 0x03) {
                result = (lhs + rhs) & mask;
                updateArithmeticFlags(lhs, rhs, result, wide, false);
            } else if (opcode == 0x09 || opcode == 0x0B ||
                       opcode == 0x21 || opcode == 0x23 ||
                       opcode == 0x31 || opcode == 0x33 || opcode == 0x85) {
                result = (opcode == 0x09 || opcode == 0x0B) ? (lhs | rhs) :
                         (opcode == 0x21 || opcode == 0x23 || opcode == 0x85) ? (lhs & rhs) :
                         (lhs ^ rhs);
                carryFlag_ = false;
                overflowFlag_ = false;
                signFlag_ = (result & (wide ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 31))) != 0;
            } else {
                result = (lhs - rhs) & mask;
                updateArithmeticFlags(lhs, rhs, result, wide, true);
            }
            zeroFlag_ = result == 0;
            if (opcode != 0x39 && opcode != 0x3B && opcode != 0x85) {
                if (reversed) registers_[reg] = result;
                else writeOperand(operand, result, wide);
            }
            continue;
        }
        // Standard compiler-generated function epilogue: mov rsp, rbp; pop rbp.
        // Group 1 imm32 arithmetic (81 /0 ADD, /5 SUB, /7 CMP).
        // In 64-bit mode, the imm32 is sign-extended to 64 bits.
        if (opcode == 0x81) {
            const auto modrm = read8();
            const auto operation = static_cast<unsigned>((modrm >> 3) & 7);
            const auto operand = decodeOperand(modrm, extended, rexX);
            const auto immediate = readSigned32();
            if (operation != 0 && operation != 5 && operation != 7)
                throw std::runtime_error("Unsupported group 1 imm32 operation");
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto lhs = readOperand(operand, wide) & mask;
            const auto rhs = static_cast<std::uint64_t>(static_cast<std::int64_t>(immediate)) & mask;
            const bool subtract = operation != 0;
            const auto result = (subtract ? lhs - rhs : lhs + rhs) & mask;
            updateArithmeticFlags(lhs, rhs, result, wide, subtract);
            if (operation != 7) writeOperand(operand, result, wide);
            continue;
        }
        // INC/DEC r/m32 or r/m64 (FF /0 and /1), preserving carry.
        if (opcode == 0xFF) {
            const auto modrm = read8();
            const auto operation = static_cast<unsigned>((modrm >> 3) & 7);
            const auto operand = decodeOperand(modrm, extended, rexX);
            if (operation != 0 && operation != 1)
                throw std::runtime_error("Unsupported FF group operation");
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto lhs = readOperand(operand, wide) & mask;
            const auto rhs = std::uint64_t{1};
            const auto result = (operation == 0 ? lhs + rhs : lhs - rhs) & mask;
            const bool savedCarry = carryFlag_;
            updateArithmeticFlags(lhs, rhs, result, wide, operation == 1);
            carryFlag_ = savedCarry;
            writeOperand(operand, result, wide);
            continue;
        }
        // Group 2 shifts: C1 r/m,imm8; D1 r/m,1; D3 r/m,CL.
        // Shift counts are masked to 5 bits for 32-bit and 6 for 64-bit.
        if (opcode == 0xC1 || opcode == 0xD1 || opcode == 0xD3) {
            const auto modrm = read8();
            const auto operation = static_cast<unsigned>((modrm >> 3) & 7);
            const auto operand = decodeOperand(modrm, extended, rexX);
            const auto rawCount = opcode == 0xC1 ? static_cast<unsigned>(read8()) :
                                  opcode == 0xD1 ? 1u :
                                  static_cast<unsigned>(registers_[1] & 0xFF);
            if (operation != 4 && operation != 5 && operation != 7)
                throw std::runtime_error("Unsupported shift operation");
            const auto bits = wide ? 64u : 32u;
            const auto count = rawCount & (wide ? 63u : 31u);
            if (count == 0) continue; // x86 leaves flags and operand unchanged.
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto value = readOperand(operand, wide) & mask;
            std::uint64_t result = 0;
            bool newCarry = false;
            if (operation == 4) { // SHL/SAL
                result = (value << count) & mask;
                newCarry = ((value >> (bits - count)) & 1) != 0;
            } else if (operation == 5) { // SHR
                result = value >> count;
                newCarry = ((value >> (count - 1)) & 1) != 0;
            } else { // SAR: explicit sign fill, without implementation-defined signed shifts.
                result = value >> count;
                const bool negative = ((value >> (bits - 1)) & 1) != 0;
                if (negative) result |= (mask << (bits - count)) & mask;
                newCarry = ((value >> (count - 1)) & 1) != 0;
            }
            result &= mask;
            carryFlag_ = newCarry;
            zeroFlag_ = result == 0;
            signFlag_ = ((result >> (bits - 1)) & 1) != 0;
            // OF is architecturally defined only for a count of one.
            // For other counts we retain its prior value.
            if (count == 1) {
                if (operation == 4) overflowFlag_ = signFlag_ != carryFlag_;
                else if (operation == 5) overflowFlag_ = ((value >> (bits - 1)) & 1) != 0;
                else overflowFlag_ = false;
            }
            writeOperand(operand, result, wide);
            continue;
        }
        // Group 1 immediate arithmetic: 83 /0 ADD, /5 SUB, /7 CMP.
        // The imm8 is sign-extended to the operand width.
        if (opcode == 0x83) {
            const auto modrm = read8();
            const auto operation = static_cast<unsigned>((modrm >> 3) & 7);
            const auto operand = decodeOperand(modrm, extended, rexX);
            const auto immediate = static_cast<std::int8_t>(read8());
            if (operation != 0 && operation != 5 && operation != 7)
                throw std::runtime_error("Unsupported group 1 immediate operation");
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto lhs = readOperand(operand, wide) & mask;
            const auto rhs = static_cast<std::uint64_t>(static_cast<std::int64_t>(immediate)) & mask;
            const bool subtract = operation != 0;
            const auto result = (subtract ? lhs - rhs : lhs + rhs) & mask;
            updateArithmeticFlags(lhs, rhs, result, wide, subtract);
            if (operation != 7) writeOperand(operand, result, wide);
            continue;
        }
        // Three-operand IMUL: 6B uses sign-extended imm8, 69 imm32.
        // Only CF and OF are defined for IMUL; preserve other flags.
        if (opcode == 0x6B || opcode == 0x69) {
            const auto modrm = read8();
            const auto destination = static_cast<std::size_t>(
                ((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto source = decodeOperand(modrm, extended, rexX);
            const auto immediate = opcode == 0x6B
                ? static_cast<std::int64_t>(static_cast<std::int8_t>(read8()))
                : static_cast<std::int64_t>(readSigned32());
            const auto sourceBits = readOperand(source, wide);
            const auto multiplier = static_cast<std::uint64_t>(immediate);
            const auto result = sourceBits * multiplier; // defined unsigned wrap
            registers_[destination] = wide ? result : static_cast<std::uint32_t>(result);
            if (wide) {
                const bool sourceNegative = (sourceBits >> 63) != 0;
                const bool immediateNegative = immediate < 0;
                const auto sourceMagnitude = sourceNegative
                    ? std::uint64_t{0} - sourceBits : sourceBits;
                const auto immediateMagnitude = immediateNegative
                    ? std::uint64_t{0} - multiplier : multiplier;
                const auto limit = (sourceNegative != immediateNegative)
                    ? (std::uint64_t{1} << 63)
                    : static_cast<std::uint64_t>(INT64_MAX);
                carryFlag_ = overflowFlag_ = immediateMagnitude != 0 &&
                    sourceMagnitude > limit / immediateMagnitude;
            } else {
                const auto signedSource = static_cast<std::int64_t>(
                    static_cast<std::int32_t>(sourceBits));
                const auto product = signedSource * immediate;
                carryFlag_ = overflowFlag_ = product < INT32_MIN || product > INT32_MAX;
            }
            continue;
        }
        // Two-operand signed IMUL r32/r64, r/m32/r/m64 (0F AF).
        if (opcode == 0x0F) {
            const auto second = read8();
            // MOVZX/MOVSX r32/r64, r/m8 (0F B6 / 0F BE).
            // Byte register encodings 4..7 refer to AH/CH/DH/BH only
            // when there is no REX prefix. With REX they mean SPL..DIL.
            if (second == 0xB6 || second == 0xBE) {
                const auto modrm = read8();
                const auto destination = static_cast<std::size_t>(
                    ((modrm >> 3) & 7) + (rexR ? 8 : 0));
                const auto operand = decodeOperand(modrm, extended, rexX);
                std::uint8_t byteValue = 0;
                if (operand.isRegister) {
                    if (!hasRex && operand.index >= 4 && operand.index <= 7)
                        byteValue = static_cast<std::uint8_t>(
                            registers_[operand.index - 4] >> 8);
                    else
                        byteValue = static_cast<std::uint8_t>(registers_[operand.index]);
                } else {
                    if (operand.address >= memory_.size())
                        throw std::runtime_error("Guest byte read out of bounds");
                    byteValue = memory_[static_cast<std::size_t>(operand.address)];
                }
                if (second == 0xB6) {
                    registers_[destination] = byteValue;
                } else {
                    const auto signedValue = byteValue < 0x80
                        ? static_cast<std::int64_t>(byteValue)
                        : static_cast<std::int64_t>(byteValue) - 256;
                    registers_[destination] = wide
                        ? static_cast<std::uint64_t>(signedValue)
                        : static_cast<std::uint32_t>(signedValue);
                }
                continue;
            }
            // MOVZX/MOVSX r32/r64, r/m16 (0F B7 / 0F BF).
            // The source is always 16-bit, while REX.W selects the
            // destination width; 32-bit register writes zero the high half.
            if (second == 0xB7 || second == 0xBF) {
                const auto modrm = read8();
                const auto destination = static_cast<std::size_t>(
                    ((modrm >> 3) & 7) + (rexR ? 8 : 0));
                const auto operand = decodeOperand(modrm, extended, rexX);
                std::uint16_t wordValue = 0;
                if (operand.isRegister) {
                    wordValue = static_cast<std::uint16_t>(registers_[operand.index]);
                } else {
                    if (operand.address > memory_.size() ||
                        memory_.size() - static_cast<std::size_t>(operand.address) < 2)
                        throw std::runtime_error("Guest word read out of bounds");
                    const auto offset = static_cast<std::size_t>(operand.address);
                    wordValue = static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(memory_[offset]) |
                        (static_cast<std::uint16_t>(memory_[offset + 1]) << 8));
                }
                if (second == 0xB7) {
                    registers_[destination] = wordValue;
                } else {
                    const auto signedValue = wordValue < 0x8000
                        ? static_cast<std::int64_t>(wordValue)
                        : static_cast<std::int64_t>(wordValue) - 65536;
                    registers_[destination] = wide
                        ? static_cast<std::uint64_t>(signedValue)
                        : static_cast<std::uint32_t>(signedValue);
                }
                continue;
            }
            // SETcc r/m8 (0F 90..9F). Unlike the existing operands,
            // SETcc writes only one byte, preserving all other register bits.
            if (second >= 0x90 && second <= 0x9F) {
                const auto modrm = read8();
                const auto operand = decodeOperand(modrm, extended, rexX);
                const auto value = static_cast<std::uint8_t>(
                    evaluateCondition(static_cast<std::uint8_t>(second & 0x0F)) ? 1 : 0);
                if (operand.isRegister) {
                    const auto reg = operand.index;
                    if (!hasRex && reg >= 4 && reg <= 7) {
                        // AH, CH, DH, BH without a REX prefix.
                        const auto parent = reg - 4;
                        registers_[parent] = (registers_[parent] & ~std::uint64_t{0xFF00}) |
                                             (std::uint64_t{value} << 8);
                    } else {
                        registers_[reg] = (registers_[reg] & ~std::uint64_t{0xFF}) | value;
                    }
                } else {
                    if (operand.address >= memory_.size())
                        throw std::runtime_error("Guest byte write out of bounds");
                    memory_[static_cast<std::size_t>(operand.address)] = value;
                }
                continue;
            }
            if (second == 0xAF) {
                const auto modrm = read8();
                const auto reg = static_cast<std::size_t>(((modrm >> 3) & 7) + (rexR ? 8 : 0));
                const auto operand = decodeOperand(modrm, extended, rexX);
                const auto rhs = readOperand(operand, wide);
                const auto lhs = wide ? registers_[reg] : static_cast<std::uint32_t>(registers_[reg]);
                const auto result = lhs * rhs; // Unsigned modular product avoids signed-overflow UB.
                registers_[reg] = wide ? result : static_cast<std::uint32_t>(result);
                // x86 IMUL sets CF and OF iff the full signed product cannot
                // be represented at the selected operand width.
                if (wide) {
                    const bool leftNegative = (lhs >> 63) != 0;
                    const bool rightNegative = (rhs >> 63) != 0;
                    const auto leftMagnitude = leftNegative ? std::uint64_t{0} - lhs : lhs;
                    const auto rightMagnitude = rightNegative ? std::uint64_t{0} - rhs : rhs;
                    const auto limit = (leftNegative != rightNegative)
                        ? (std::uint64_t{1} << 63)
                        : static_cast<std::uint64_t>(INT64_MAX);
                    carryFlag_ = overflowFlag_ = rightMagnitude != 0 &&
                        leftMagnitude > limit / rightMagnitude;
                } else {
                    const auto product = static_cast<std::int64_t>(static_cast<std::int32_t>(lhs)) *
                                         static_cast<std::int64_t>(static_cast<std::int32_t>(rhs));
                    carryFlag_ = overflowFlag_ = product < INT32_MIN || product > INT32_MAX;
                }
                continue;
            }
            if (second >= 0x80 && second <= 0x8F) {
                const auto displacement = readSigned32();
                if (evaluateCondition(second & 0x0F)) jumpRelative(displacement);
                continue;
            }
            throw std::runtime_error("Unsupported two-byte guest instruction");
        }
        if (opcode == 0xC9) {
            if (wide || extended || rexR || rexX)
                throw std::runtime_error("Unsupported LEAVE prefix");
            registers_[4] = registers_[5];
            registers_[5] = pop64();
            continue;
        }
        if (opcode == 0xC3) {
            if (wide || extended) throw std::runtime_error("Unsupported RET prefix");
            if (callDepth_ == 0) return registers_[0];
            const auto destination = pop64();
            if (destination >= program_.size()) throw std::runtime_error("Guest return outside code bounds");
            ip_ = static_cast<std::size_t>(destination);
            --callDepth_;
            continue;
        }
        if (opcode >= 0x50 && opcode <= 0x57) {
            const auto index = static_cast<std::size_t>(opcode - 0x50 + (extended ? 8 : 0));
            push64(registers_[index]);
            continue;
        }
        if (opcode >= 0x58 && opcode <= 0x5F) {
            const auto index = static_cast<std::size_t>(opcode - 0x58 + (extended ? 8 : 0));
            registers_[index] = pop64();
            continue;
        }
        if (opcode >= 0xB8 && opcode <= 0xBF) {
            const auto index = static_cast<std::size_t>((opcode - 0xB8) + (extended ? 8 : 0));
            if (wide) {
                const auto low = static_cast<std::uint64_t>(read32());
                const auto high = static_cast<std::uint64_t>(read32());
                registers_[index] = low | (high << 32);
            } else {
                registers_[index] = read32();
            }
            continue;
        }
        if (opcode == 0x05 && !wide && !extended) {
            const auto left = static_cast<std::uint32_t>(registers_[0]);
            const auto right = read32();
            const auto result = static_cast<std::uint32_t>(left + right);
            registers_[0] = result;
            zeroFlag_ = result == 0;
            updateArithmeticFlags(left, right, result, false, false);
            continue;
        }
        if (opcode == 0x3D && !wide && !extended) {
            const auto left = static_cast<std::uint32_t>(registers_[0]);
            const auto right = read32();
            updateArithmeticFlags(left, right, static_cast<std::uint32_t>(left - right), false, true);
            continue;
        }
        if (opcode == 0xE8 && !wide && !extended) {
            const auto displacement = readSigned32();
            const auto returnAddress = ip_;
            jumpRelative(displacement);
            push64(returnAddress);
            ++callDepth_;
            continue;
        }
        if (opcode == 0xE9 && !wide && !extended) {
            jumpRelative(readSigned32());
            continue;
        }
        if (opcode == 0xEB && !wide && !extended) {
            const auto offset = static_cast<std::int8_t>(read8());
            jumpRelative(offset);
            continue;
        }
        if (opcode >= 0x70 && opcode <= 0x7F) {
            const auto offset = static_cast<std::int8_t>(read8());
            if (evaluateCondition(opcode & 0x0F)) jumpRelative(offset);
            continue;
        }
        throw std::runtime_error("Unsupported guest instruction");
    }
    throw std::runtime_error("Guest instruction limit exceeded");
}

std::uint64_t GuestCpu::rax() const noexcept { return registers_[0]; }
std::uint64_t GuestCpu::registerValue(std::size_t index) const {
    if (index >= registers_.size()) throw std::out_of_range("Invalid guest register index");
    return registers_[index];
}
std::size_t GuestCpu::instructionPointer() const noexcept { return ip_; }
}
