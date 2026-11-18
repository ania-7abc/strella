#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace strella
{

/**
 * @brief Decodes a raw memory image into assembly-like text.
 *
 * The disassembler performs a linear sweep.  Bytes that are not valid
 * opcodes (or reserved opcodes) are printed as `db` directives; instruction
 * operands are consumed with their opcode.  When `showAddresses` is false,
 * address prefixes are omitted, producing a plain assembly file.
 *
 * `dataRanges` lists address ranges that are known to hold data (preimage
 * fields, constants, strings).  Those bytes are always printed as `db`, even
 * when they happen to decode as valid instructions.
 */
auto disassemble(std::vector<std::uint8_t> const& bytes,
                 bool showAddresses = true,
                 std::vector<std::pair<int, int>> const& dataRanges
                 = {}) -> std::string;

} // namespace strella
