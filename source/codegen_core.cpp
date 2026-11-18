#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/location.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace strella
{
auto CodeGen::Impl::generate() -> CompilationResult
{
  prepareFunctions();

  for (auto* function : functionOrder_)
  {
    generateFunctionItems(*function);
  }

  // The protected byte is identified from the image without protection, so
  // build that image first and then relayout with the byte reserved if needed.
  placeFunctions();
  buildImage();
  findProtectedImageOffset();

  if (displayProtect_ && protectedImageOffset_ >= 0)
  {
    placeFunctions();
    buildImage();
    applyDisplayProtect();
  }

  // Collect defragmentation statistics: how much of every used bank is
  // occupied and how many functions share it.
  std::vector<int> bankEnd(256, -1);
  std::vector<int> bankFunctions(256, 0);

  for (auto* function : functionOrder_)
  {
    auto& code = functions_[function];
    std::vector<char> touched(256, 0);

    for (auto const& item : code.items)
    {
      auto const bank = code.startBank + item.bank;
      if (bank < 0 || bank >= 256)
      {
        continue;
      }
      auto const end = item.offset + itemSize(item);
      bankEnd[static_cast<std::size_t>(bank)]
          = std::max(bankEnd[static_cast<std::size_t>(bank)], end);
      touched[static_cast<std::size_t>(bank)] = 1;
    }

    for (auto const& boundary : code.boundaries)
    {
      auto const bank = code.startBank + boundary.bank;
      if (bank < 0 || bank >= 256)
      {
        continue;
      }
      // st A, st B, st C, st D, ldi A, ldi B, jmp = 14 bytes.
      auto const end = boundary.address + 14;
      bankEnd[static_cast<std::size_t>(bank)]
          = std::max(bankEnd[static_cast<std::size_t>(bank)], end);
      touched[static_cast<std::size_t>(bank)] = 1;
    }

    for (auto const relativeBank : code.continuationBanks)
    {
      auto const bank = code.startBank + relativeBank;
      if (bank < 0 || bank >= 256)
      {
        continue;
      }
      // Register restore lives at 0x80..0x87.
      bankEnd[static_cast<std::size_t>(bank)]
          = std::max(bankEnd[static_cast<std::size_t>(bank)], 0x88);
      touched[static_cast<std::size_t>(bank)] = 1;
    }

    for (int bank = 0; bank < 256; ++bank)
    {
      if (touched[static_cast<std::size_t>(bank)] != 0)
      {
        ++bankFunctions[static_cast<std::size_t>(bank)];
      }
    }
  }

  LayoutStats stats;
  stats.totalFunctions = static_cast<int>(functionOrder_.size());
  for (int bank = 1; bank < 256; ++bank)
  {
    auto const end = bankEnd[static_cast<std::size_t>(bank)];
    if (end < 0)
    {
      continue;
    }

    BankUsage usage;
    usage.bank = bank;
    usage.used = std::clamp(end - 0x80, 0, kBankSize);
    usage.free = kBankSize - usage.used;
    usage.functions = bankFunctions[static_cast<std::size_t>(bank)];
    stats.banks.push_back(usage);
    ++stats.banksUsed;
    stats.codeBytes += usage.used;
    stats.freeBytes += usage.free;
    if (usage.functions > 1)
    {
      ++stats.packedBanks;
    }
  }

  CompilationResult result;
  result.image = image_;
  result.layout = stats;
  result.dataRanges = dataRanges_;

  for (auto* function : functionOrder_)
  {
    auto const found = functions_.find(function);
    if (found == functions_.end())
    {
      continue;
    }

    auto const& code = found->second;
    result.map.push_back({function->name, {code.startBank, code.entry}});

    result.listing.push_back("func " + function->name + " bank "
                             + std::to_string(code.startBank) + " entry 0x"
                             + hexByte(static_cast<std::uint8_t>(code.entry))
                             + (code.stackBased ? " @stack" : ""));

    for (auto const& item : code.items)
    {
      if (item.kind == Item::Kind::Label)
      {
        continue;
      }

      auto const bank = code.startBank + item.bank;
      std::string line = "  " + std::to_string(bank) + ":0x"
                         + hexByte(static_cast<std::uint8_t>(item.offset))
                         + " ";

      switch (item.kind)
      {
      case Item::Kind::Bytes:
      case Item::Kind::RawBlock:
        for (auto byte : item.bytes)
        {
          line += hexByte(byte) + " ";
        }
        break;
      case Item::Kind::Jump:
        line += "jump slot";
        break;
      case Item::Kind::StaticCall:
        line += "call "
                + ((item.callee != nullptr) ? item.callee->name
                                            : std::string {"?"});
        break;
      case Item::Kind::StackCall:
        line += "stack-call "
                + ((item.callee != nullptr) ? item.callee->name
                                            : std::string {"?"});
        break;
      case Item::Kind::Label:
        break;
      }

      result.listing.push_back(std::move(line));
    }
  }

  return result;
}

auto CodeGen::Impl::placeFunctions() -> void
{
  int openBank = 1;
  int openOffset = 0x80;
  bool openBankUsed = false;

  auto finishPlacement = [&](FunctionCode& code) -> void
  {
    auto const found = code.labels.find(code.entryLabel);
    if (found == code.labels.end())
    {
      fail(file_, code.function->location,
           "internal error: unresolved entry in '" + code.function->name + "'");
    }
    code.entry = found->second.second;

    auto const lastBank = code.startBank + code.banksUsed - 1;
    if (lastBank > 255)
    {
      fail(file_, SourceLocation {},
           "program does not fit into the 32 KB memory image");
    }

    openBank = lastBank;
    openOffset = code.endOffset;
    openBankUsed = true;

    code.function->bank = code.startBank;
    code.function->entry = code.entry;
  };

  auto placeAt = [&](FunctionCode& code, int startBank, int startOffset) -> void
  {
    if (startBank < 1 || startBank > 255)
    {
      fail(file_, SourceLocation {},
           "program does not fit into the 32 KB memory image");
    }
    code.startBank = startBank;
    code.startOffset = startOffset;
    layoutFunction(code);
    finishPlacement(code);
  };

  for (auto* function : functionOrder_)
  {
    auto& code = functions_[function];

    if (code.isMain)
    {
      if (openBankUsed)
      {
        fail(file_, function->location,
             "internal error: 'main' is not the first function");
      }
      placeAt(code, 1, 0x80);
      continue;
    }

    if (!code.stackBased)
    {
      // A static function keeps its call record at 0x80/0x81 and fixed
      // epilogues at the start of a bank, so it needs a fresh bank.
      placeAt(code, openBankUsed ? openBank + 1 : openBank, 0x80);
      continue;
    }

    // @stack functions are position independent: try to pack them into the
    // free tail of the current bank before opening a new one.
    if (openBankUsed)
    {
      code.startBank = openBank;
      code.startOffset = openOffset;
      layoutFunction(code);
      if (code.banksUsed == 1)
      {
        finishPlacement(code);
        continue;
      }
    }

    placeAt(code, openBankUsed ? openBank + 1 : openBank, 0x80);
  }
}
} // namespace strella
