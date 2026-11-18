#pragma once

#include "codegen_types.hpp"

namespace strella
{
struct CodeGen::Impl
{
  struct LoopContext
  {
    int breakLabel = -1;
    int continueLabel = -1;
    bool isLoop = false;
  };

  Impl(Program& program, Sema& sema, std::string file, bool displayProtect)
      : program_ {program}, sema_ {sema}, file_ {std::move(file)},
        displayProtect_ {displayProtect}, allocator_ {sema.dataEnd()},
        image_(kImageSize, 0)
  {
  }

private:
  Program& program_;
  Sema& sema_;
  std::string file_;
  bool displayProtect_ = true;
  int protectedImageOffset_ = -1;
  CommonAllocator allocator_;
  std::vector<std::uint8_t> image_;
  std::vector<std::pair<int, int>> dataRanges_;
  std::unordered_map<FunctionDecl*, FunctionCode> functions_;
  std::vector<FunctionDecl*> functionOrder_;
  FunctionCode* current_ = nullptr;
  FunctionDecl* currentFunction_ = nullptr;
  int nextLabel_ = 0;
  int tempTop_ = 0;
  int tempMax_ = 0;
  std::vector<LoopContext> contexts_;
  std::vector<InlineContext> inlineContexts_;
  int stackBase_ = 0;

  auto newLabel() -> int { return nextLabel_++; }

  auto allocTemp() -> int
  {
    auto const index = tempTop_++;
    tempMax_ = std::max(tempMax_, tempTop_);
    return index;
  }

  void releaseTemp(int index)
  {
    if (index == tempTop_ - 1)
    {
      --tempTop_;
    }
  }

  void emitBytes(std::vector<std::uint8_t> const& bytes)
  {
    Item item;
    item.kind = Item::Kind::Bytes;
    item.bytes = bytes;
    current_->items.push_back(std::move(item));
  }

  void emitBytes(std::initializer_list<std::uint8_t> bytes)
  {
    emitBytes(std::vector<std::uint8_t>(bytes));
  }

  void emitOp(std::uint8_t opcode) { emitBytes({opcode}); }

  void emitLabel(int label)
  {
    Item item;
    item.kind = Item::Kind::Label;
    item.label = label;
    current_->items.push_back(std::move(item));
  }

  void emitJump(int target, int condition)
  {
    Item item;
    item.kind = Item::Kind::Jump;
    item.target = target;
    item.condition = condition;
    current_->items.push_back(std::move(item));
  }

  void emitUncondJump(int target) { emitJump(target, -1); }

  void emitCondJump(int target, Cond condition, bool negate = false)
  {
    emitJump(target, static_cast<int>(condition) + (negate ? 4 : 0));
  }

  void emitStaticCall(FunctionDecl* callee, int returnLabel)
  {
    Item item;
    item.kind = Item::Kind::StaticCall;
    item.callee = callee;
    item.returnLabel = returnLabel;
    current_->items.push_back(std::move(item));
  }

  void emitStackCall(FunctionDecl* callee)
  {
    Item item;
    item.kind = Item::Kind::StackCall;
    item.callee = callee;
    current_->items.push_back(std::move(item));
  }

  void emitRawBlock(std::vector<std::uint8_t> bytes,
                    std::vector<std::pair<int, int>> shortJumps = {})
  {
    Item item;
    item.kind = Item::Kind::RawBlock;
    item.bytes = std::move(bytes);
    item.shortJumps = std::move(shortJumps);
    current_->items.push_back(std::move(item));
  }

  void emitLoadImm(std::uint32_t value, bool word)
  {
    emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(value & 0xFF)));
    if (word)
    {
      emitBytes(
          isa::ldi(Reg::B, static_cast<std::uint8_t>((value >> 8) & 0xFF)));
    }
  }

  auto tempAddress(int index) const -> int
  {
    return current_->tempBase + (index * 2);
  }

  auto stackFrameOffset(Symbol const& symbol) const -> int
  {
    if (symbol.storage == Storage::StackParameter)
    {
      return -(3 + currentFunction_->stackParamBytes) + symbol.frameOffset;
    }
    return symbol.frameOffset;
  }

  void emitFrameAddress(int offset, Reg addressReg, bool preserveAB);

  void emitLoadSymbol(Symbol const& symbol, bool word);
  void emitStoreSymbol(Symbol const& symbol, bool word);
  void emitExtendOperand(Expr const& operand, bool wide);
  void emitLoadTemp(int index, bool word, Reg low, Reg high);
  void emitStoreTemp(int index, bool word);
  void emitTempAddress(int index);
  void emitAddressOf(Expr& expression);
  void emitLoadIndirect(bool word);
  void emitStoreToLValue(Expr& expression, bool word);

  void emitSymbolAddress(Symbol const& symbol);
  auto emitDirectFieldAddress(Expr& expression, int offset, Reg addressReg,
                              bool preserveAB) -> bool;
  void emitCopyBytes(int destTemp, int srcTemp, int size);
  void emitCopyBytesFromAddress(int destTemp, int size);
  void emitCopyBytesAtOffset(int destTemp, int offset, int srcTemp, int size);
  void emitCopyBytesFromExpr(int destTemp, int offset, Expr& source, int size);
  void emitCopyToSymbol(Symbol const& symbol, Expr& source, int size);
  void emitCopyToStackPointer(Expr& source, int size);
  void emitWriteValueAtTemp(int destTemp, int offset, Expr& value);
  void emitBuildStructLiteral(Expr& literal, int destTemp);

  void emitCondition(Expr& expression);
  void emitExpr(Expr& expression);
  void emitUnary(Expr& expression);
  void emitCast(Expr& expression);
  void emitIncDec(Expr& expression);
  void emitComparison(Expr& expression);
  void emitBinary(Expr& expression);
  void emitLogical(Expr& expression);
  void emitTernary(Expr& expression);
  void emitAssign(Expr& expression);
  void emitSetBoolFromComparison(TokenKind op, bool signedComparison);
  void emitSetBoolFromZero();
  void emitSetBoolFromNonzero();
  void emitUserCall(Expr& expression);
  void emitInlineCall(Expr& expression);
  void emitBuiltinCall(Expr& expression);
  void emitStackCallSequence(FunctionDecl* callee,
                             std::vector<ExprPtr>& arguments);
  [[nodiscard]] auto inlineSlotFor(Symbol const& symbol) const -> int;

  void emitStatement(Stmt& statement);
  void emitBlock(Stmt& statement);
  void emitDeclaration(Stmt& statement);
  void emitIf(Stmt& statement);
  void emitWhile(Stmt& statement);
  void emitDoWhile(Stmt& statement);
  void emitFor(Stmt& statement);
  void emitSwitch(Stmt& statement);
  void emitReturn(Stmt& statement);

  void emitFunctionPrologue(FunctionDecl& function);
  void emitFunctionEpilogue(FunctionDecl& function);
  void emitStackReturn(bool hasValue);
  void emitStoreReturnValue();
  void prepareFunctions();
  void emitStartup();
  void generateFunctionItems(FunctionDecl& function);
  void placeFunctions();
  void layoutFunction(FunctionCode& code);
  void emitFunctionImage(FunctionCode& code);
  void buildImage();
  void findProtectedImageOffset();
  [[nodiscard]] auto protectedCodeOffset(int absoluteBank) const -> int;
  void applyDisplayProtect();

public:
  auto generate() -> CompilationResult;
};
} // namespace strella
