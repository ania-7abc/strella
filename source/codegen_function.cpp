#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/isa.hpp"
#include "strella/location.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace strella
{
auto CodeGen::Impl::emitFunctionPrologue(FunctionDecl& function) -> void
{
  auto const frameSize = current_->frameSize;
  if (frameSize > 255)
  {
    fail(file_, function.location,
         "stack frame of '" + function.name + "' is too large");
  }

  // Save the caller's frame pointer just below the new frame, then point FP
  // at it: C = SP; [C] = FP; FP = C + 1; SP = FP + frameSize.
  emitBytes(isa::ldImm(Reg::C, 0x0F));
  emitBytes(isa::ldImm(Reg::A, 0x10));
  emitOp(isa::stReg(Reg::C, Reg::A));
  emitOp(isa::inc(Reg::C));
  emitBytes(isa::stImm(Reg::C, 0x10));

  if (frameSize > 0)
  {
    emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(frameSize)));
    emitOp(isa::add(Reg::C, Reg::A));
  }
  emitBytes(isa::stImm(Reg::C, 0x0F));
}

auto CodeGen::Impl::emitFunctionEpilogue(FunctionDecl& function) -> void
{
  if (current_->isMain)
  {
    emitOp(isa::hlt());
    return;
  }

  if (current_->stackBased)
  {
    if (function.returnType.isVoid())
    {
      emitStackReturn(false);
      return;
    }

    if (function.returnType.isStruct())
    {
      emitBytes(isa::ldi(
          Reg::A, static_cast<std::uint8_t>(function.structReturnAddress)));
      emitBytes(isa::ldi(Reg::B, 0));
    }
    else
    {
      emitBytes(isa::ldi(Reg::A, 0));
      emitBytes(isa::ldi(Reg::B, 0));
    }
    emitStackReturn(true);
    return;
  }

  if (function.returnType.isVoid())
  {
    emitUncondJump(current_->voidEpilogueLabel);
    return;
  }

  if (function.returnType.isStruct())
  {
    emitBytes(isa::ldi(
        Reg::A, static_cast<std::uint8_t>(function.structReturnAddress)));
    emitBytes(isa::ldi(Reg::B, 0));
    emitStoreReturnValue();
    emitUncondJump(current_->valueEpilogueLabel);
    return;
  }

  emitBytes(isa::ldi(Reg::A, 0));
  emitBytes(isa::ldi(Reg::B, 0));
  emitStoreReturnValue();
  emitUncondJump(current_->valueEpilogueLabel);
}

auto CodeGen::Impl::emitStartup() -> void
{
  for (auto* declaration : sema_.runtimeGlobalInitializers())
  {
    if (declaration == nullptr || declaration->symbol == nullptr
        || !declaration->initializer)
    {
      continue;
    }

    if (declaration->type.isStruct())
    {
      emitCopyToSymbol(*declaration->symbol, *declaration->initializer,
                       declaration->type.size());
      continue;
    }

    auto const wide = declaration->type.isWide();
    emitExpr(*declaration->initializer);
    emitExtendOperand(*declaration->initializer, wide);
    emitStoreSymbol(*declaration->symbol, wide);
  }
}

auto CodeGen::Impl::prepareFunctions() -> void
{
  auto* mainFunction = program_.findFunction("main");
  if (mainFunction == nullptr)
  {
    fail(file_, SourceLocation {}, "no 'main' function found");
  }

  functionOrder_.clear();
  functionOrder_.push_back(mainFunction);
  for (auto& function : program_.functions)
  {
    if (function.get() == mainFunction
        || function->annotation == FunctionAnnotation::Inline)
    {
      continue;
    }
    functionOrder_.push_back(function.get());
  }

  for (auto* function : functionOrder_)
  {
    FunctionCode code;
    code.function = function;
    code.isMain = function == mainFunction;
    code.stackBased = function->annotation == FunctionAnnotation::Stack;
    code.maxTemps = analyzeFunctionTemps(*function);

    if (code.isMain)
    {
      int startupUsage = 0;
      for (auto* declaration : sema_.runtimeGlobalInitializers())
      {
        if (declaration != nullptr && declaration->initializer)
        {
          startupUsage = std::max(
              startupUsage, analyzeExpressionTemps(*declaration->initializer));
        }
      }
      code.maxTemps = std::max(code.maxTemps, startupUsage + 4);
    }

    if (code.stackBased)
    {
      code.frameSize = function->stackFrameSize + (code.maxTemps * 2);
      if (code.frameSize > 255)
      {
        fail(file_, function->location,
             "stack frame of '" + function->name + "' is too large");
      }
    }
    else
    {
      code.tempBase
          = allocator_.allocate(code.maxTemps * 2, file_, function->location);
    }

    functions_[function] = std::move(code);
  }

  stackBase_ = allocator_.cursor();

  if (sema_.usesStack())
  {
    if (stackBase_ > kStackLimit)
    {
      fail(file_, SourceLocation {},
           "not enough common RAM for the software stack");
    }
  }
}

auto CodeGen::Impl::generateFunctionItems(FunctionDecl& function) -> void
{
  auto const found = functions_.find(&function);
  if (found == functions_.end())
  {
    return;
  }

  current_ = &found->second;
  currentFunction_ = &function;
  tempTop_ = 0;
  tempMax_ = 0;
  contexts_.clear();
  inlineContexts_.clear();

  current_->entryLabel = newLabel();

  if (current_->isMain)
  {
    emitLabel(current_->entryLabel);

    if (sema_.usesStack())
    {
      emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(stackBase_)));
      emitBytes(isa::stImm(Reg::A, 0x0F));
    }

    if (current_->stackBased)
    {
      emitFunctionPrologue(function);
    }

    emitStartup();

    current_->haltLabel = newLabel();

    if (function.body)
    {
      emitStatement(*function.body);
    }

    emitLabel(current_->haltLabel);
    emitOp(isa::hlt());
  }
  else if (current_->stackBased)
  {
    emitLabel(current_->entryLabel);
    emitFunctionPrologue(function);

    if (function.body)
    {
      emitStatement(*function.body);
    }

    emitFunctionEpilogue(function);
  }
  else
  {
    // Reserved static-call return record at 0x80/0x81.
    emitBytes({isa::nop(), isa::nop()});

    current_->valueEpilogueLabel = newLabel();
    emitLabel(current_->valueEpilogueLabel);
    emitBytes(isa::ldImm(Reg::A, kReturnLow));
    emitBytes(isa::ldImm(Reg::B, kReturnHigh));
    emitBytes(isa::ldImm(Reg::C, 0x80));
    emitBytes(isa::ldImm(Reg::D, 0x81));
    emitBytes(isa::jmpImm(0x05));

    current_->voidEpilogueLabel = newLabel();
    emitLabel(current_->voidEpilogueLabel);
    emitBytes(isa::ldImm(Reg::C, 0x80));
    emitBytes(isa::ldImm(Reg::D, 0x81));
    emitBytes(isa::jmpImm(0x05));

    current_->entryLabel = newLabel();
    emitLabel(current_->entryLabel);

    if (function.body)
    {
      emitStatement(*function.body);
    }

    emitFunctionEpilogue(function);
  }

  if (tempMax_ > current_->maxTemps)
  {
    fail(file_, function.location,
         "internal error: temporary slots exhausted in '" + function.name
             + "' (" + std::to_string(tempMax_) + " > "
             + std::to_string(current_->maxTemps) + ")");
  }

  current_ = nullptr;
  currentFunction_ = nullptr;
}

auto CodeGen::Impl::layoutFunction(FunctionCode& code) -> void
{
  code.labels.clear();
  code.boundaries.clear();
  code.continuationBanks.clear();

  int bank = 0;
  int offset = code.startOffset;

  for (auto& item : code.items)
  {
    auto const size = itemSize(item);
    auto const protectedOffset = protectedCodeOffset(code.startBank + bank);

    // Keep the protected code byte free; shifting the whole item preserves its
    // internal jump patches and labels.
    if (protectedOffset >= 0
        && (offset == protectedOffset
            || (size > 0 && offset < protectedOffset
                && offset + size > protectedOffset)))
    {
      offset = protectedOffset + 1;
    }

    if (size > 0 && offset + size > kCodeLimit)
    {
      if (size > kCodeLimit - kContinuationOffset)
      {
        fail(file_, code.function->location,
             "a single code block in '" + code.function->name
                 + "' is too large for one bank");
      }

      BoundaryJump boundary;
      boundary.bank = bank;
      boundary.address = kCodeLimit;
      boundary.nextBank = bank + 1;
      code.boundaries.push_back(boundary);

      ++bank;
      offset = kContinuationOffset;
      code.continuationBanks.insert(bank);
    }

    item.bank = bank;
    item.offset = offset;

    if (item.kind == Item::Kind::Label)
    {
      code.labels[item.label] = {bank, offset};
    }

    offset += size;
  }

  code.banksUsed = bank + 1;
  code.endOffset = offset;
}

} // namespace strella
