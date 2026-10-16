#include "GuestCpu.hpp"
#include <limits>
#include <string>

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

GuestCpu::Operand GuestCpu::decodeOperand(std::uint8_t modrm, bool rexB, bool rexX, std::size_t trailingImmediateBytes) {
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
    // RIP-relative addresses use the end of the *entire* instruction,
    // including any immediate following ModR/M displacement bytes.
    if (ripRelative) base = static_cast<std::uint64_t>(ip_) + trailingImmediateBytes;
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
        bool operand16 = false;
        if (opcode == 0x66) {
            operand16 = true;
            opcode = read8();
        }
        if (opcode >= 0x40 && opcode <= 0x4F) {
            hasRex = true;
            wide = (opcode & 8) != 0;
            extended = (opcode & 1) != 0;
            rexR = (opcode & 4) != 0;
            rexX = (opcode & 2) != 0;
            opcode = read8();
        }
        // Multi-byte alignment NOPs (0F 1F /0) are decoded below.
        // One-byte NOP and CDQE/CWDE are common compiler output.
        if (opcode == 0x90 && !operand16) continue;
        if (opcode == 0x98 && !operand16) {
            if (wide) {
                const auto value = static_cast<std::uint32_t>(registers_[0]);
                registers_[0] = (value & 0x80000000u)
                    ? (static_cast<std::uint64_t>(value) | UINT64_C(0xFFFFFFFF00000000))
                    : value;
            } else {
                const auto value = static_cast<std::uint16_t>(registers_[0]);
                registers_[0] = (value & 0x8000u)
                    ? (static_cast<std::uint32_t>(value) | 0xFFFF0000u)
                    : value;
            }
            continue;
        }
        // Byte MOV (88/8A) and immediate byte MOV (C6 /0).
        // Without REX, byte register codes 4..7 select AH/CH/DH/BH.
        if (!operand16 && (opcode == 0x88 || opcode == 0x8A || opcode == 0xC6)) {
            const auto modrm = read8();
            const auto group = static_cast<unsigned>((modrm >> 3) & 7);
            if (opcode == 0xC6 && group != 0)
                throw std::runtime_error("Unsupported C6 group operation");
            const auto operand = decodeOperand(modrm, extended, rexX, opcode == 0xC6 ? 1 : 0);
            const auto readByteReg = [&](std::size_t index) -> std::uint8_t {
                if (!hasRex && index >= 4 && index <= 7)
                    return static_cast<std::uint8_t>(registers_[index - 4] >> 8);
                return static_cast<std::uint8_t>(registers_[index]);
            };
            const auto writeByteReg = [&](std::size_t index, std::uint8_t value) {
                if (!hasRex && index >= 4 && index <= 7) {
                    const auto parent = index - 4;
                    registers_[parent] = (registers_[parent] & ~UINT64_C(0xFF00)) |
                        (static_cast<std::uint64_t>(value) << 8);
                } else {
                    registers_[index] = (registers_[index] & ~UINT64_C(0xFF)) | value;
                }
            };
            const auto readByte = [&]() -> std::uint8_t {
                if (operand.isRegister) return readByteReg(operand.index);
                if (operand.address >= memory_.size())
                    throw std::runtime_error("Guest byte read out of bounds");
                return memory_[static_cast<std::size_t>(operand.address)];
            };
            const auto writeByte = [&](std::uint8_t value) {
                if (operand.isRegister) writeByteReg(operand.index, value);
                else {
                    if (operand.address >= memory_.size())
                        throw std::runtime_error("Guest byte write out of bounds");
                    memory_[static_cast<std::size_t>(operand.address)] = value;
                }
            };
            const auto reg = static_cast<std::size_t>(group + (rexR ? 8 : 0));
            if (opcode == 0x88) writeByte(readByteReg(reg));
            else if (opcode == 0x8A) writeByteReg(reg, readByte());
            else writeByte(read8());
            continue;
        }
        // MOVSXD r64,r/m32 (REX.W + 63 /r).
        // Without REX.W, opcode 63 is deliberately unsupported here:
        // the legacy-width form has different destination semantics.
        if (opcode == 0x63) {
            if (!wide)
                throw std::runtime_error("MOVSXD requires REX.W");
            const auto modrm = read8();
            const auto destination = static_cast<std::size_t>(
                ((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto operand = decodeOperand(modrm, extended, rexX);
            const auto value = static_cast<std::uint32_t>(readOperand(operand, false));
            const auto extendedValue = value & 0x80000000u
                ? static_cast<std::uint64_t>(value) | UINT64_C(0xFFFFFFFF00000000)
                : static_cast<std::uint64_t>(value);
            registers_[destination] = extendedValue;
            continue;
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
        // 66 89 /r and 66 8B /r: 16-bit MOV. Register destinations
        // preserve bits 16..63; memory accesses are exactly two bytes.
        if (operand16 && (opcode == 0x89 || opcode == 0x8B)) {
            const auto modrm = read8();
            const auto reg = static_cast<std::size_t>(((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto operand = decodeOperand(modrm, extended, rexX);
            const auto readWord = [&]() -> std::uint16_t {
                if (operand.isRegister)
                    return static_cast<std::uint16_t>(registers_[operand.index]);
                if (operand.address > memory_.size() ||
                    memory_.size() - static_cast<std::size_t>(operand.address) < 2)
                    throw std::runtime_error("Guest word read out of bounds");
                const auto address = static_cast<std::size_t>(operand.address);
                return static_cast<std::uint16_t>(
                    memory_[address] | (static_cast<unsigned>(memory_[address + 1]) << 8));
            };
            const auto writeWord = [&](std::uint16_t value) {
                if (operand.isRegister) {
                    registers_[operand.index] =
                        (registers_[operand.index] & ~UINT64_C(0xFFFF)) | value;
                } else {
                    if (operand.address > memory_.size() ||
                        memory_.size() - static_cast<std::size_t>(operand.address) < 2)
                        throw std::runtime_error("Guest word write out of bounds");
                    const auto address = static_cast<std::size_t>(operand.address);
                    memory_[address] = static_cast<std::uint8_t>(value);
                    memory_[address + 1] = static_cast<std::uint8_t>(value >> 8);
                }
            };
            if (opcode == 0x89) writeWord(static_cast<std::uint16_t>(registers_[reg]));
            else registers_[reg] = (registers_[reg] & ~UINT64_C(0xFFFF)) | readWord();
            continue;
        }
        // 66 C7 /0: MOV r/m16,imm16. The immediate is two bytes,
        // including when the destination uses RIP-relative addressing.
        if (operand16 && opcode == 0xC7) {
            const auto modrm = read8();
            if (((modrm >> 3) & 7) != 0)
                throw std::runtime_error("Unsupported 16-bit C7 group operation");
            const auto operand = decodeOperand(modrm, extended, rexX, 2);
            const auto low = static_cast<std::uint16_t>(read8());
            const auto high = static_cast<std::uint16_t>(read8());
            const auto value = static_cast<std::uint16_t>(low | (high << 8));
            if (operand.isRegister) {
                registers_[operand.index] =
                    (registers_[operand.index] & ~UINT64_C(0xFFFF)) | value;
            } else {
                if (operand.address > memory_.size() ||
                    memory_.size() - static_cast<std::size_t>(operand.address) < 2)
                    throw std::runtime_error("Guest word write out of bounds");
                const auto address = static_cast<std::size_t>(operand.address);
                memory_[address] = static_cast<std::uint8_t>(value);
                memory_[address + 1] = static_cast<std::uint8_t>(value >> 8);
            }
            continue;
        }
        if (operand16)
            throw std::runtime_error("Unsupported 16-bit operand-size instruction");
        if (opcode == 0x89 || opcode == 0x8B) {
            const auto modrm = read8();
            const auto reg = static_cast<std::size_t>(((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto operand = decodeOperand(modrm, extended, rexX);
            if (opcode == 0x89) writeOperand(operand, registers_[reg], wide);
            else registers_[reg] = readOperand(operand, wide);
            continue;
        }
        // C7 /0: MOV r/m32,imm32 or MOV r/m64,sign-extended imm32.
        // ModR/M.reg is an opcode extension, not a destination register.
        if (opcode == 0xC7) {
            const auto modrm = read8();
            if (((modrm >> 3) & 7) != 0)
                throw std::runtime_error("Unsupported C7 group operation");
            const auto operand = decodeOperand(modrm, extended, rexX, 4);
            const auto immediate = readSigned32();
            const auto value = wide
                ? static_cast<std::uint64_t>(static_cast<std::int64_t>(immediate))
                : static_cast<std::uint64_t>(static_cast<std::uint32_t>(immediate));
            writeOperand(operand, value, wide);
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
            const auto operand = decodeOperand(modrm, extended, rexX, 4);
            const auto immediate = readSigned32();
            if (operation != 0 && operation != 1 && operation != 4 &&
                operation != 5 && operation != 6 && operation != 7)
                throw std::runtime_error("Unsupported group 1 imm32 operation");
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto lhs = readOperand(operand, wide) & mask;
            const auto rhs = static_cast<std::uint64_t>(static_cast<std::int64_t>(immediate)) & mask;
            std::uint64_t result = 0;
            if (operation == 1 || operation == 4 || operation == 6) {
                result = operation == 1 ? (lhs | rhs) :
                         operation == 4 ? (lhs & rhs) : (lhs ^ rhs);
                zeroFlag_ = result == 0;
                signFlag_ = (result & (wide ? (UINT64_C(1) << 63) : (UINT64_C(1) << 31))) != 0;
                carryFlag_ = false;
                overflowFlag_ = false;
            } else {
                const bool subtract = operation != 0;
                result = (subtract ? lhs - rhs : lhs + rhs) & mask;
                updateArithmeticFlags(lhs, rhs, result, wide, subtract);
            }
            if (operation != 7) writeOperand(operand, result, wide);
            continue;
        }
        // F7 /0 TEST r/m,imm32 (sign-extended with REX.W).
        // TEST updates logic flags but never changes the destination.
        if (opcode == 0xF7) {
            // Remaining F7 suboperations are handled in the next block.
            // Decode /0 here without consuming bytes for other operations.
            if (ip_ >= program_.size())
                throw std::runtime_error("Truncated F7 ModR/M");
            const auto group = static_cast<unsigned>((program_[ip_] >> 3) & 7);
            if (group == 0) {
                const auto modrm = read8();
                const auto operand = decodeOperand(modrm, extended, rexX, 4);
                const auto immediate = readSigned32();
                const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
                const auto value = readOperand(operand, wide) & mask;
                const auto result = value &
                    (static_cast<std::uint64_t>(static_cast<std::int64_t>(immediate)) & mask);
                zeroFlag_ = result == 0;
                signFlag_ = (result & (wide ? (UINT64_C(1) << 63) : (UINT64_C(1) << 31))) != 0;
                carryFlag_ = false;
                overflowFlag_ = false;
                continue;
            }
        }
        // F7 /2 NOT and /3 NEG on 32-bit or REX.W 64-bit operands.
        // NOT leaves flags unchanged; NEG sets CF for every nonzero input.
        if (opcode == 0xF7) {
            const auto modrm = read8();
            const auto operation = static_cast<unsigned>((modrm >> 3) & 7);
            if (operation != 2 && operation != 3)
                throw std::runtime_error("Unsupported F7 group operation");
            const auto operand = decodeOperand(modrm, extended, rexX);
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto value = readOperand(operand, wide) & mask;
            if (operation == 2) {
                writeOperand(operand, (~value) & mask, wide);
            } else {
                const auto result = (std::uint64_t{0} - value) & mask;
                updateArithmeticFlags(0, value, result, wide, true);
                writeOperand(operand, result, wide);
            }
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
            const auto operand = decodeOperand(modrm, extended, rexX, opcode == 0xC1 ? 1 : 0);
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
            const auto operand = decodeOperand(modrm, extended, rexX, 1);
            const auto immediate = static_cast<std::int8_t>(read8());
            if (operation != 0 && operation != 1 && operation != 4 &&
                operation != 5 && operation != 6 && operation != 7)
                throw std::runtime_error("Unsupported group 1 immediate operation");
            const auto mask = wide ? UINT64_MAX : static_cast<std::uint64_t>(UINT32_MAX);
            const auto lhs = readOperand(operand, wide) & mask;
            const auto rhs = static_cast<std::uint64_t>(static_cast<std::int64_t>(immediate)) & mask;
            std::uint64_t result = 0;
            if (operation == 1 || operation == 4 || operation == 6) {
                result = operation == 1 ? (lhs | rhs) :
                         operation == 4 ? (lhs & rhs) : (lhs ^ rhs);
                zeroFlag_ = result == 0;
                signFlag_ = (result & (wide ? (UINT64_C(1) << 63) : (UINT64_C(1) << 31))) != 0;
                carryFlag_ = false;
                overflowFlag_ = false;
            } else {
                const bool subtract = operation != 0;
                result = (subtract ? lhs - rhs : lhs + rhs) & mask;
                updateArithmeticFlags(lhs, rhs, result, wide, subtract);
            }
            if (operation != 7) writeOperand(operand, result, wide);
            continue;
        }
        // Three-operand IMUL: 6B uses sign-extended imm8, 69 imm32.
        // Only CF and OF are defined for IMUL; preserve other flags.
        if (opcode == 0x6B || opcode == 0x69) {
            const auto modrm = read8();
            const auto destination = static_cast<std::size_t>(
                ((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto source = decodeOperand(modrm, extended, rexX, opcode == 0x6B ? 1 : 4);
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
            // 0F 1F /0: multi-byte NOP with ModR/M/SIB/displacement.
            // Decode addressing bytes but never dereference the operand.
            if (second == 0x1F && !operand16) {
                const auto modrm = read8();
                if (((modrm >> 3) & 7) != 0)
                    throw std::runtime_error("Unsupported multi-byte NOP group");
                (void)decodeOperand(modrm, extended, rexX);
                continue;
            }
            // CMOVcc r32/r64,r/m32/r/m64 (0F 40..4F).
            // x86 reads the source even when the condition is false;
            // memory faults therefore cannot be skipped by a false CMOV.
            if (second >= 0x40 && second <= 0x4F) {
                const auto modrm = read8();
                const auto destination = static_cast<std::size_t>(
                    ((modrm >> 3) & 7) + (rexR ? 8 : 0));
                const auto operand = decodeOperand(modrm, extended, rexX);
                const auto source = readOperand(operand, wide);
                if (evaluateCondition(static_cast<std::uint8_t>(second & 0x0F)))
                    registers_[destination] = source;
                continue;
            }
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
            throw std::runtime_error("Unsupported two-byte guest opcode 0F " +
                std::to_string(static_cast<unsigned>(second)) +
                " at offset " + std::to_string(ip_ - 2));
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
        throw std::runtime_error("Unsupported guest opcode " +
            std::to_string(static_cast<unsigned>(opcode)) +
            " at offset " + std::to_string(ip_ - 1));
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
