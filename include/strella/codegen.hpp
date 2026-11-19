#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "strella/ast.hpp"
#include "strella/sema.hpp"
#include "strella/target.hpp"

namespace strella
{

/**
 * @brief Code density of a single 128-byte code bank.
 */
struct BankUsage
{
  int bank = 0;
  int used = 0;
  int free = 0;
  int functions = 0;
};

/**
 * @brief Summary of how functions were distributed across code banks.
 */
struct LayoutStats
{
  int banksUsed = 0;
  int totalFunctions = 0;
  int codeBytes = 0;
  int freeBytes = 0;
  int packedBanks = 0;
  std::vector<BankUsage> banks;
};

/**
 * @brief Result of compiling a program.
 */
struct CompilationResult
{
  std::vector<std::uint8_t> image;
  std::vector<std::string> listing;
  std::vector<std::pair<std::string, std::pair<int, int>>> map;
  // Address ranges that hold data rather than code (preimage fields, constant
  // globals, string literals).  The disassembler prints them as `db`.
  std::vector<std::pair<int, int>> dataRanges;
  int optimizations = 0;
  LayoutStats layout;
};

/**
 * @brief Generates a 32 KB memory image from a checked program.
 */
class CodeGen
{
public:
  CodeGen(Program& program, Sema& sema, std::string file,
          Target target = Target::K1);
  ~CodeGen();

  CodeGen(CodeGen&&) noexcept;
  auto operator=(CodeGen&&) noexcept -> CodeGen&;

  CodeGen(CodeGen const&) = delete;
  auto operator=(CodeGen const&) -> CodeGen& = delete;

  auto generate() -> CompilationResult;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace strella
