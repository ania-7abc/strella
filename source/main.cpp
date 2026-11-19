#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "strella/codegen.hpp"
#include "strella/compiler.hpp"
#include "strella/diagnostics.hpp"
#include "strella/disasm.hpp"
#include "strella/target.hpp"

using strella::Target;

namespace
{

auto usage() -> void
{
  std::cout
      << "usage: strella [options] <input>\n"
      << "\n"
      << "options:\n"
      << "  -o <file>          write output to <file>\n"
      << "  --hex              print a hexadecimal dump instead of writing a "
         "file\n"
      << "  --map              print the function map\n"
      << "  --asm              compile the input and write assembly to a file\n"
      << "  --disasm           disassemble a binary memory image\n"
      << "  --stats            print code-bank fragmentation statistics\n"
      << "  --no-opt           disable AST optimizations\n"
      << "  --target 1k|32k    select target RAM (default: 1k)\n"
      << "  -1k, -32k, --32k   select target RAM (short forms)\n"
      << "  32k rejects preimages and enables display protection\n"
      << "  -h, --help         show this message\n";
}

auto parseTarget(std::string const& value, Target& target) -> bool
{
  if (value == "1k")
  {
    target = Target::K1;
    return true;
  }
  if (value == "32k")
  {
    target = Target::K32;
    return true;
  }
  std::cerr << "error: unknown target '" << value << "' (expected 1k or 32k)\n";
  return false;
}

auto withExtension(std::string const& input,
                   std::string const& extension) -> std::string
{
  auto const slash = input.find_last_of('/');
  auto const dot = input.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
  {
    return input + extension;
  }
  return input.substr(0, dot) + extension;
}

auto defaultOutputPath(std::string const& input) -> std::string
{
  return withExtension(input, ".bin");
}

auto defaultAsmOutputPath(std::string const& input) -> std::string
{
  auto const path = withExtension(input, ".asm");
  return path == input ? input + ".asm" : path;
}

auto printHex(std::vector<std::uint8_t> const& image) -> void
{
  static constexpr std::array<char, 16> digits {'0', '1', '2', '3', '4', '5',
                                                '6', '7', '8', '9', 'A', 'B',
                                                'C', 'D', 'E', 'F'};

  for (std::size_t offset = 0; offset < image.size(); offset += 16)
  {
    std::string line;
    for (std::size_t i = 0; i < 16; ++i)
    {
      auto const index = offset + i;
      if (index < image.size())
      {
        auto const value = image[index];
        line += digits.at(static_cast<std::size_t>(value >> 4));
        line += digits.at(static_cast<std::size_t>(value & 0x0F));
      }
      else
      {
        line += "  ";
      }
      line += ' ';
    }

    std::string address;
    auto value = offset;
    for (int i = 0; i < 4; ++i)
    {
      address.insert(address.begin(),
                     digits.at(static_cast<std::size_t>(value & 0x0F)));
      value >>= 4;
    }

    std::cout << address << ": " << line << '\n';
  }
}

auto writeBinary(std::string const& path,
                 std::vector<std::uint8_t> const& image) -> bool
{
  std::ofstream output {path, std::ios::binary};
  if (!output)
  {
    return false;
  }

  std::copy(image.begin(), image.end(), std::ostreambuf_iterator<char>(output));
  return static_cast<bool>(output);
}

auto readBinary(std::string const& path) -> std::vector<std::uint8_t>
{
  std::ifstream input {path, std::ios::binary};
  if (!input)
  {
    throw strella::CompileError {"error: cannot open '" + path + "'"};
  }
  return {std::istreambuf_iterator<char> {input},
          std::istreambuf_iterator<char> {}};
}

auto writeText(std::string const& path, std::string const& text) -> bool
{
  std::ofstream output {path};
  if (!output)
  {
    return false;
  }
  output << text;
  return static_cast<bool>(output);
}

auto printStats(strella::LayoutStats const& stats) -> void
{
  auto const total = stats.banksUsed * 128;
  auto const percent = total > 0 ? (stats.codeBytes * 100) / total : 0;

  std::cout << "layout statistics:\n";
  std::cout << "  functions:     " << stats.totalFunctions << '\n';
  std::cout << "  banks used:    " << stats.banksUsed << '\n';
  std::cout << "  code bytes:    " << stats.codeBytes << " / " << total << " ("
            << percent << "%)\n";
  std::cout << "  free bytes:    " << stats.freeBytes << '\n';
  std::cout << "  packed banks:  " << stats.packedBanks
            << " (two or more functions)\n";
  std::cout << "  banks:\n";
  for (auto const& bank : stats.banks)
  {
    std::cout << "    bank " << bank.bank << ": " << bank.used << "/128 used, "
              << bank.free << " free, " << bank.functions << " function"
              << (bank.functions == 1 ? "" : "s") << '\n';
  }
}

} // namespace

auto main(int argc, char** argv) -> int
{
  std::string input;
  std::string output;
  bool hex = false;
  bool map = false;
  bool asmOutput = false;
  bool disasm = false;
  bool stats = false;
  bool optimize = true;
  Target target = Target::K1;

  for (int i = 1; i < argc; ++i)
  {
    std::string const argument = argv[i];

    if (argument == "-h" || argument == "--help")
    {
      usage();
      return 0;
    }

    if (argument == "-o")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "error: -o requires a file name\n";
        return 2;
      }
      output = argv[++i];
      continue;
    }

    if (argument == "--hex")
    {
      hex = true;
      continue;
    }

    if (argument == "--map")
    {
      map = true;
      continue;
    }

    if (argument == "--asm")
    {
      asmOutput = true;
      continue;
    }

    if (argument == "--disasm")
    {
      disasm = true;
      continue;
    }

    if (argument == "--stats")
    {
      stats = true;
      continue;
    }

    if (argument == "-O0" || argument == "--no-opt")
    {
      optimize = false;
      continue;
    }

    if (argument == "--target")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "error: --target requires 1k or 32k\n";
        return 2;
      }
      if (!parseTarget(argv[++i], target))
      {
        return 2;
      }
      continue;
    }

    if (argument.rfind("--target=", 0) == 0)
    {
      if (!parseTarget(argument.substr(9), target))
      {
        return 2;
      }
      continue;
    }

    if (argument == "-1k")
    {
      target = Target::K1;
      continue;
    }

    if (argument == "-32k" || argument == "--32k")
    {
      target = Target::K32;
      continue;
    }

    if (!argument.empty() && argument[0] == '-')
    {
      std::cerr << "error: unknown option '" << argument << "'\n";
      return 2;
    }

    if (!input.empty())
    {
      std::cerr << "error: only one input file is supported\n";
      return 2;
    }

    input = argument;
  }

  if (input.empty())
  {
    usage();
    return 2;
  }

  try
  {
    auto emitText = [&](std::string const& path, std::string const& text) -> int
    {
      if (path.empty())
      {
        std::cout << text;
        return 0;
      }
      if (!writeText(path, text))
      {
        std::cerr << "error: cannot write '" << path << "'\n";
        return 1;
      }
      std::cerr << "wrote " << text.size() << " bytes to " << path << '\n';
      return 0;
    };

    if (disasm)
    {
      return emitText(output, strella::disassemble(readBinary(input)));
    }

    auto const result = strella::compileFile(input, optimize, target);

    if (asmOutput)
    {
      auto const path = output.empty() ? defaultAsmOutputPath(input) : output;
      auto const status = emitText(
          path, strella::disassemble(result.image, false, result.dataRanges));
      if (status != 0)
      {
        return status;
      }
    }
    else if (hex)
    {
      printHex(result.image);
    }
    else
    {
      auto const path = output.empty() ? defaultOutputPath(input) : output;
      if (!writeBinary(path, result.image))
      {
        std::cerr << "error: cannot write '" << path << "'\n";
        return 1;
      }
      std::cerr << "wrote " << result.image.size() << " bytes to " << path
                << '\n';
    }

    if (map)
    {
      std::cout << "function map:\n";
      for (auto const& entry : result.map)
      {
        std::cout << "  " << entry.first << " bank " << entry.second.first
                  << " entry 0x" << std::hex << entry.second.second << std::dec
                  << '\n';
      }
    }

    if (stats)
    {
      printStats(result.layout);
    }
  }
  catch (strella::CompileError const& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
