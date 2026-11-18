#include "strella/disasm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace strella
{
namespace
{

constexpr std::array<char const*, 4> kRegs {"a", "b", "c", "d"};
constexpr std::array<char const*, 4> kConds {"z", "s", "c", "o"};
constexpr std::array<char const*, 4> kShifts {"shl", "shr", "sar", "rnd"};

auto hexDigit(std::uint8_t value) -> char
{
  static constexpr std::array<char, 16> digits {'0', '1', '2', '3', '4', '5',
                                                '6', '7', '8', '9', 'A', 'B',
                                                'C', 'D', 'E', 'F'};
  return digits.at(static_cast<std::size_t>(value & 0x0F));
}

auto hexByte(std::uint8_t value) -> std::string
{
  return std::string {"0x"} + hexDigit(static_cast<std::uint8_t>(value >> 4))
         + hexDigit(value);
}

auto hexAddress(std::size_t value) -> std::string
{
  std::string result(4, '0');
  for (std::size_t i = 0; i < result.size(); ++i)
  {
    auto const shift = static_cast<unsigned>((3 - i) * 4);
    result[i] = hexDigit(static_cast<std::uint8_t>(value >> shift));
  }
  return result;
}

auto unaryRegister(unsigned low) -> int
{
  switch (low)
  {
  case 0:
    return 0;
  case 5:
    return 1;
  case 0xA:
    return 2;
  case 0xF:
    return 3;
  default:
    return -1;
  }
}

struct Instruction
{
  std::string text;
  std::size_t size = 1;
  bool valid = true;
};

auto invalid(std::uint8_t opcode) -> Instruction
{
  return {"db " + hexByte(opcode), 1, false};
}

auto decode(std::vector<std::uint8_t> const& bytes, std::size_t pos)
    -> Instruction
{
  auto const opcode = bytes.at(pos);
  auto const low = static_cast<unsigned>(opcode & 0x0F);

  auto operand = [&]() -> std::pair<bool, std::uint8_t>
  {
    if (pos + 1 >= bytes.size())
    {
      return {false, 0};
    }
    return {true, bytes.at(pos + 1)};
  };

  switch (opcode)
  {
  case 0x00:
    return {"nop", 1, true};
  case 0x01:
    return {"hlt", 1, true};
  case 0x03:
  {
    auto const value = operand();
    if (!value.first)
    {
      return invalid(opcode);
    }
    return {"jmp " + hexByte(value.second), 2, true};
  }
  default:
    break;
  }

  if (opcode >= 0x04 && opcode <= 0x07)
  {
    return {
        "jmp "
            + std::string {kRegs.at(static_cast<std::size_t>(opcode - 0x04))},
        1, true};
  }

  if (opcode >= 0x08 && opcode <= 0x0F)
  {
    auto const value = operand();
    if (!value.first)
    {
      return invalid(opcode);
    }
    auto const index = static_cast<std::size_t>((opcode - 0x08) % 4);
    auto const negate = opcode >= 0x0C;
    return {std::string {negate ? "jn" : "j"} + kConds.at(index) + " "
                + hexByte(value.second),
            2, true};
  }

  if (opcode >= 0x10 && opcode <= 0x2F)
  {
    auto const index = static_cast<std::size_t>(low % 4);
    auto const reg = static_cast<std::size_t>(low / 4);
    auto const negate = opcode >= 0x20;
    return {std::string {negate ? "jn" : "j"} + kConds.at(index) + " "
                + kRegs.at(reg),
            1, true};
  }

  if (opcode >= 0x30 && opcode <= 0x3F)
  {
    auto const reg = unaryRegister(low);
    if (reg >= 0)
    {
      auto const value = operand();
      if (!value.first)
      {
        return invalid(opcode);
      }
      return {"st " + std::string {kRegs.at(static_cast<std::size_t>(reg))}
                  + ", " + hexByte(value.second),
              2, true};
    }
    auto const address = static_cast<std::size_t>(low / 4);
    auto const value = static_cast<std::size_t>(low % 4);
    return {"st " + std::string {kRegs.at(value)} + ", " + kRegs.at(address), 1,
            true};
  }

  if (opcode >= 0x40 && opcode <= 0x4F)
  {
    auto const address = static_cast<std::size_t>(low / 4);
    auto const dest = static_cast<std::size_t>(low % 4);
    return {"ld " + std::string {kRegs.at(dest)} + ", " + kRegs.at(address), 1,
            true};
  }

  if (opcode >= 0x50 && opcode <= 0x57)
  {
    auto const value = operand();
    if (!value.first)
    {
      return invalid(opcode);
    }
    auto const reg = static_cast<std::size_t>(low % 4);
    auto const mnemonic
        = (opcode <= 0x53) ? std::string {"ld "} : std::string {"ldi "};
    return {mnemonic + kRegs.at(reg) + ", " + hexByte(value.second), 2, true};
  }

  if (opcode >= 0x60 && opcode <= 0xDF)
  {
    std::string unary;
    std::string binary;
    switch (static_cast<unsigned>(opcode >> 4))
    {
    case 0x6:
      unary = "inc";
      binary = "add";
      break;
    case 0x7:
      unary = "dec";
      binary = "sub";
      break;
    case 0x8:
      unary = "not";
      binary = "adc";
      break;
    case 0x9:
      unary = "neg";
      binary = "sbb";
      break;
    case 0xA:
      unary = "clr";
      binary = "mov";
      break;
    case 0xB:
      unary = "test";
      binary = "and";
      break;
    case 0xC:
      unary = "rcl";
      binary = "or";
      break;
    case 0xD:
      unary = "rcr";
      binary = "xor";
      break;
    default:
      break;
    }

    auto const reg = unaryRegister(low);
    if (reg >= 0)
    {
      return {unary + " " + kRegs.at(static_cast<std::size_t>(reg)), 1, true};
    }
    auto const dst = static_cast<std::size_t>(low % 4);
    auto const src = static_cast<std::size_t>(low / 4);
    return {binary + " " + kRegs.at(dst) + ", " + kRegs.at(src), 1, true};
  }

  if (opcode >= 0xF0)
  {
    if (unaryRegister(low) >= 0)
    {
      return invalid(opcode);
    }
    auto const dst = static_cast<std::size_t>(low % 4);
    auto const src = static_cast<std::size_t>(low / 4);
    return {"cmp " + std::string {kRegs.at(dst)} + ", " + kRegs.at(src), 1,
            true};
  }

  if (opcode >= 0xE0)
  {
    auto const shift = static_cast<std::size_t>(low / 4);
    auto const reg = static_cast<std::size_t>(low % 4);
    return {std::string {kShifts.at(shift)} + " " + kRegs.at(reg), 1, true};
  }

  return invalid(opcode);
}

} // namespace

auto disassemble(std::vector<std::uint8_t> const& bytes, bool showAddresses,
                 std::vector<std::pair<int, int>> const& dataRanges)
    -> std::string
{
  if (bytes.empty())
  {
    return {};
  }

  std::vector<bool> isData(bytes.size(), false);
  for (auto const& range : dataRanges)
  {
    for (int i = 0; i < range.second; ++i)
    {
      auto const index = range.first + i;
      if (index < 0 || index >= static_cast<int>(bytes.size()))
      {
        continue;
      }
      isData[static_cast<std::size_t>(index)] = true;
    }
  }

  auto last = bytes.size() - 1;
  while (last > 0 && bytes.at(last) == 0)
  {
    --last;
  }

  std::string result;
  std::size_t pos = 0;
  std::size_t dataIndex = 0;
  std::vector<std::uint8_t> pending;

  auto prefix = [&](std::size_t address) -> std::string
  { return showAddresses ? hexAddress(address) + ":  " : std::string {}; };

  auto dataLabel
      = [&]() -> std::string { return "_data" + std::to_string(dataIndex++); };

  auto flush = [&]() -> void
  {
    if (pending.empty())
    {
      return;
    }
    result += prefix(pos - pending.size()) + dataLabel() + " db";
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
      if (i > 0)
      {
        result += ',';
      }
      result += " " + hexByte(pending.at(i));
    }
    result += '\n';
    pending.clear();
  };

  while (pos <= last)
  {
    if (bytes.at(pos) == 0 && !isData.at(pos))
    {
      std::size_t count = 0;
      while (pos + count <= last && bytes.at(pos + count) == 0)
      {
        ++count;
      }
      if (count >= 4)
      {
        // Emit any bytes collected before the run first: they belong to lower
        // addresses, and the address prefix of a `db` line depends on `pos`.
        flush();
        while (count > 0)
        {
          auto const chunk = std::min<std::size_t>(count, 16);
          std::string line = prefix(pos) + dataLabel() + " db";
          for (std::size_t i = 0; i < chunk; ++i)
          {
            if (i > 0)
            {
              line += ',';
            }
            line += " 0x00";
          }
          result += line + '\n';
          pos += chunk;
          count -= chunk;
        }
        continue;
      }
    }

    if (isData.at(pos))
    {
      pending.push_back(bytes.at(pos));
      ++pos;
      if (pending.size() == 8)
      {
        flush();
      }
      continue;
    }

    auto const instruction = decode(bytes, pos);
    if (!instruction.valid)
    {
      pending.push_back(bytes.at(pos));
      ++pos;
      if (pending.size() == 8)
      {
        flush();
      }
      continue;
    }

    flush();
    result += prefix(pos) + instruction.text + '\n';
    pos += instruction.size;
  }

  flush();
  return result;
}

} // namespace strella
