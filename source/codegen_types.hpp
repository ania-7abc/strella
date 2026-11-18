#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/isa.hpp"
#include "strella/location.hpp"
#include "strella/sema.hpp"
#include "strella/token.hpp"

namespace strella
{
namespace codegen_detail
{
constexpr int kImageSize = 32768;
constexpr int kBankSize = 128;
constexpr int kBankRegisterOffset = 0x3F;
constexpr int kProtectedNonzeroIndex = 63;
constexpr std::uint8_t kProtectedBits = 0x30;
constexpr int kDevicesPort = 0x3E;
constexpr std::uint8_t kPreimageDevices = 48;
constexpr int kDisplayStart = 0x40;
constexpr int kDisplayEnd = 0x7F;
constexpr int kDataLimit = 0x39;
constexpr int kStackLimit = 0x39;
constexpr int kCodeLimit = 0xF2;
constexpr int kContinuationOffset = 0x88;
constexpr int kEntryOffset = 0x92;
constexpr int kValueEpilogueOffset = 0x82;
constexpr int kVoidEpilogueOffset = 0x8C;
constexpr int kScratchA = 0x11;
constexpr int kScratchB = 0x12;
constexpr int kScratchC = 0x13;
constexpr int kScratchD = 0x14;
constexpr int kReturnLow = 0x15;
constexpr int kReturnHigh = 0x16;

using isa::Cond;
using isa::Reg;

struct Item
{
  enum class Kind : std::uint8_t
  {
    Bytes,
    Label,
    Jump,
    StaticCall,
    StackCall,
    RawBlock
  };

  Kind kind = Kind::Bytes;
  std::vector<std::uint8_t> bytes;
  int label = -1;
  int target = -1;
  int condition = -1; // -1 = unconditional, otherwise a Cond value
  FunctionDecl* callee = nullptr;
  int returnLabel = -1;
  // Offsets inside `bytes` of short-jump operands and their in-block targets.
  std::vector<std::pair<int, int>> shortJumps;
  // Filled by layoutFunction().  `bank` is relative to the function's first
  // bank; `offset` is the address inside that bank (0x80..0xFF).
  int bank = -1;
  int offset = -1;
};

struct BoundaryJump
{
  int bank = 0;
  int address = 0;
  int nextBank = 0;
};

struct InlineSlot
{
  int temp = -1;
  int count = 1;
};

struct InlineContext
{
  FunctionDecl* function = nullptr;
  int resultTemp = -1;
  int endLabel = -1;
  bool returnsValue = false;
  std::unordered_map<Symbol const*, InlineSlot> slots;
};

struct FunctionCode
{
  FunctionDecl* function = nullptr;
  std::vector<Item> items;
  int startBank = -1;
  int startOffset = -1;
  int entry = -1;
  int banksUsed = 0;
  int endOffset = -1;
  int entryLabel = -1;
  int valueEpilogueLabel = -1;
  int voidEpilogueLabel = -1;
  int haltLabel = -1;
  int tempBase = -1;
  int maxTemps = 0;
  int frameSize = 0;
  bool stackBased = false;
  bool isMain = false;
  std::unordered_map<int, std::pair<int, int>> labels;
  std::vector<BoundaryJump> boundaries;
  std::unordered_set<int> continuationBanks;
};

class CommonAllocator
{
public:
  explicit CommonAllocator(int start) : cursor_ {start} {}

  auto allocate(int size, std::string const& file,
                SourceLocation const& location) -> int
  {
    if (size <= 0)
    {
      return cursor_;
    }

    if (cursor_ + size - 1 > kDataLimit)
    {
      fail(file, location, "common data exceeds the available RAM");
    }

    auto const address = cursor_;
    cursor_ += size;
    return address;
  }

  [[nodiscard]] auto cursor() const -> int { return cursor_; }

private:
  int cursor_;
};

void collectLocalSymbols(Stmt& statement, std::vector<Symbol const*>& symbols);
auto inlineSlotCount(Symbol const& symbol) -> int;
auto analyzeExpressionTemps(Expr& expression) -> int;
auto analyzeStatementTemps(Stmt& statement) -> int;
auto analyzeFunctionTemps(FunctionDecl& function) -> int;
auto itemSize(Item const& item) -> int;
auto hexByte(std::uint8_t value) -> std::string;
} // namespace codegen_detail
using namespace codegen_detail;
} // namespace strella
