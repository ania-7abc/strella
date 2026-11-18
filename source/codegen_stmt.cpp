#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/isa.hpp"
#include <cstdint>
#include <vector>

namespace strella
{
auto CodeGen::Impl::emitStatement(Stmt& statement) -> void
{
  switch (statement.kind)
  {
  case StmtKind::Block:
    emitBlock(statement);
    break;

  case StmtKind::Declaration:
    emitDeclaration(statement);
    break;

  case StmtKind::Expression:
    if (statement.expression)
    {
      emitExpr(*statement.expression);
    }
    break;

  case StmtKind::If:
    emitIf(statement);
    break;

  case StmtKind::While:
    emitWhile(statement);
    break;

  case StmtKind::DoWhile:
    emitDoWhile(statement);
    break;

  case StmtKind::For:
    emitFor(statement);
    break;

  case StmtKind::Switch:
    emitSwitch(statement);
    break;

  case StmtKind::Break:
    for (auto it = contexts_.rbegin(); it != contexts_.rend(); ++it)
    {
      if (it->breakLabel >= 0)
      {
        emitUncondJump(it->breakLabel);
        break;
      }
    }
    break;

  case StmtKind::Continue:
    for (auto it = contexts_.rbegin(); it != contexts_.rend(); ++it)
    {
      if (it->isLoop && it->continueLabel >= 0)
      {
        emitUncondJump(it->continueLabel);
        break;
      }
    }
    break;

  case StmtKind::Return:
    emitReturn(statement);
    break;

  case StmtKind::Halt:
    emitOp(isa::hlt());
    break;

  case StmtKind::StaticAssert:
  case StmtKind::Empty:
    break;
  }
}

auto CodeGen::Impl::emitBlock(Stmt& statement) -> void
{
  for (auto& child : statement.statements)
  {
    emitStatement(*child);
  }
}

auto CodeGen::Impl::emitDeclaration(Stmt& statement) -> void
{
  auto& declaration = *statement.declaration;
  if (declaration.symbol == nullptr
      || declaration.symbol->storage == Storage::Constant)
  {
    return;
  }

  if (declaration.type.isArray())
  {
    auto const& symbol = *declaration.symbol;
    if (symbol.storage == Storage::StackLocal)
    {
      // The pointer slot points at the backing storage in the same frame.
      emitFrameAddress(symbol.arrayFrameOffset, Reg::C, false);
      emitOp(isa::mov(Reg::A, Reg::C));
      emitBytes(isa::ldi(Reg::B, 0));
      emitStoreSymbol(symbol, true);
    }
    else
    {
      auto const inlineTemp = inlineSlotFor(symbol);
      if (inlineTemp >= 0)
      {
        // The pointer slot occupies one temporary; the backing storage
        // follows it.
        emitTempAddress(inlineTemp + 1);
        emitStoreSymbol(symbol, true);
      }
      // Statically allocated arrays are initialized by a data initializer.
    }
    return;
  }

  if (declaration.type.isStruct())
  {
    if (declaration.initializer)
    {
      emitCopyToSymbol(*declaration.symbol, *declaration.initializer,
                       declaration.type.size());
    }
    return;
  }

  if (!declaration.initializer)
  {
    return;
  }

  auto const wide = declaration.type.isWide();
  emitExpr(*declaration.initializer);
  emitExtendOperand(*declaration.initializer, wide);
  emitStoreSymbol(*declaration.symbol, wide);
}

auto CodeGen::Impl::emitIf(Stmt& statement) -> void
{
  auto const elseLabel = newLabel();
  auto const endLabel = newLabel();

  emitCondition(*statement.expression);
  emitCondJump(elseLabel, Cond::Z);
  emitStatement(*statement.thenBranch);

  if (statement.elseBranch)
  {
    emitUncondJump(endLabel);
    emitLabel(elseLabel);
    emitStatement(*statement.elseBranch);
    emitLabel(endLabel);
  }
  else
  {
    emitLabel(elseLabel);
  }
}

auto CodeGen::Impl::emitWhile(Stmt& statement) -> void
{
  auto const loopLabel = newLabel();
  auto const endLabel = newLabel();

  contexts_.push_back(LoopContext {endLabel, loopLabel, true});
  emitLabel(loopLabel);
  emitCondition(*statement.expression);
  emitCondJump(endLabel, Cond::Z);
  emitStatement(*statement.thenBranch);
  emitUncondJump(loopLabel);
  emitLabel(endLabel);
  contexts_.pop_back();
}

auto CodeGen::Impl::emitDoWhile(Stmt& statement) -> void
{
  auto const bodyLabel = newLabel();
  auto const conditionLabel = newLabel();
  auto const endLabel = newLabel();

  contexts_.push_back(LoopContext {endLabel, conditionLabel, true});
  emitLabel(bodyLabel);
  emitStatement(*statement.thenBranch);
  emitLabel(conditionLabel);
  emitCondition(*statement.expression);
  emitCondJump(bodyLabel, Cond::Z, true); // jnz body
  emitLabel(endLabel);
  contexts_.pop_back();
}

auto CodeGen::Impl::emitFor(Stmt& statement) -> void
{
  if (statement.declaration)
  {
    emitDeclaration(statement);
  }
  else if (statement.expression)
  {
    emitExpr(*statement.expression);
  }

  auto const conditionLabel = newLabel();
  auto const continueLabel = newLabel();
  auto const endLabel = newLabel();

  contexts_.push_back(LoopContext {endLabel, continueLabel, true});
  emitLabel(conditionLabel);
  if (statement.secondExpression)
  {
    emitCondition(*statement.secondExpression);
    emitCondJump(endLabel, Cond::Z);
  }

  emitStatement(*statement.thenBranch);

  emitLabel(continueLabel);
  if (statement.thirdExpression)
  {
    emitExpr(*statement.thirdExpression);
  }

  emitUncondJump(conditionLabel);
  emitLabel(endLabel);
  contexts_.pop_back();
}

auto CodeGen::Impl::emitSwitch(Stmt& statement) -> void
{
  auto const wide = statement.expression->type.isWide();
  auto const switchTemp = allocTemp();
  emitExpr(*statement.expression);
  emitStoreTemp(switchTemp, wide);

  auto const endLabel = newLabel();
  contexts_.push_back(LoopContext {endLabel, -1, false});

  struct CaseInfo
  {
    SwitchCase* value = nullptr;
    int label = -1;
  };

  std::vector<CaseInfo> cases;
  int defaultLabel = -1;

  for (auto& switchCase : statement.cases)
  {
    auto const label = newLabel();
    cases.push_back(CaseInfo {&switchCase, label});
    if (switchCase.isDefault)
    {
      defaultLabel = label;
    }
  }

  for (auto& info : cases)
  {
    if (info.value->isDefault)
    {
      continue;
    }

    for (auto& value : info.value->values)
    {
      emitLoadTemp(switchTemp, wide, Reg::A, Reg::B);
      emitBytes(isa::ldi(
          Reg::C, static_cast<std::uint8_t>(value->constantValue & 0xFF)));
      if (!wide)
      {
        emitOp(isa::cmp(Reg::A, Reg::C));
        emitCondJump(info.label, Cond::Z);
        continue;
      }

      emitBytes(isa::ldi(Reg::D, static_cast<std::uint8_t>(
                                     (value->constantValue >> 8) & 0xFF)));
      // Word equality: Z = (low ^ caseLow) | (high ^ caseHigh) == 0.
      emitOp(isa::bitXor(Reg::A, Reg::C));
      emitOp(isa::bitXor(Reg::B, Reg::D));
      emitOp(isa::bitOr(Reg::A, Reg::B));
      emitOp(isa::test(Reg::A));
      emitCondJump(info.label, Cond::Z);
    }
  }

  emitUncondJump(defaultLabel >= 0 ? defaultLabel : endLabel);

  for (auto& info : cases)
  {
    emitLabel(info.label);
    for (auto& child : info.value->body)
    {
      emitStatement(*child);
    }
  }

  emitLabel(endLabel);
  contexts_.pop_back();
  releaseTemp(switchTemp);
}

auto CodeGen::Impl::emitReturn(Stmt& statement) -> void
{
  if (currentFunction_ == nullptr)
  {
    return;
  }

  if (!inlineContexts_.empty())
  {
    // Copy the context fields before evaluating the expression: nested inline
    // calls may reallocate the context vector.
    auto const returnsValue = inlineContexts_.back().returnsValue;
    auto const resultTemp = inlineContexts_.back().resultTemp;
    auto const endLabel = inlineContexts_.back().endLabel;
    auto* const inlineFunction = inlineContexts_.back().function;
    auto const wide = inlineFunction->returnType.isWide();

    if (returnsValue && inlineFunction->returnType.isStruct()
        && statement.expression)
    {
      auto const address = inlineFunction->structReturnAddress;
      auto const destTemp = allocTemp();
      emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(address)));
      emitBytes(isa::ldi(Reg::B, 0));
      emitStoreTemp(destTemp, true);
      emitCopyBytesFromExpr(destTemp, 0, *statement.expression,
                            inlineFunction->returnType.size());
      releaseTemp(destTemp);
      emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(address)));
      emitBytes(isa::ldi(Reg::B, 0));
      emitStoreTemp(resultTemp, true);
      emitUncondJump(endLabel);
      return;
    }

    if (returnsValue && statement.expression)
    {
      emitExpr(*statement.expression);
      emitExtendOperand(*statement.expression, wide);
      emitStoreTemp(resultTemp, wide);
    }
    emitUncondJump(endLabel);
    return;
  }

  if (currentFunction_->returnType.isStruct())
  {
    auto const address = currentFunction_->structReturnAddress;
    auto const destTemp = allocTemp();
    emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(address)));
    emitBytes(isa::ldi(Reg::B, 0));
    emitStoreTemp(destTemp, true);
    emitCopyBytesFromExpr(destTemp, 0, *statement.expression,
                          currentFunction_->returnType.size());
    releaseTemp(destTemp);

    emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(address)));
    emitBytes(isa::ldi(Reg::B, 0));
    if (current_->stackBased)
    {
      emitStackReturn(true);
    }
    else
    {
      emitStoreReturnValue();
      emitUncondJump(current_->valueEpilogueLabel);
    }
    return;
  }

  if (current_->isMain)
  {
    if (statement.expression)
    {
      emitExpr(*statement.expression);
    }
    emitUncondJump(current_->haltLabel);
    return;
  }

  if (current_->stackBased)
  {
    if (!currentFunction_->returnType.isVoid())
    {
      emitExpr(*statement.expression);
      emitExtendOperand(*statement.expression,
                        currentFunction_->returnType.isWide());
      emitStackReturn(true);
    }
    else
    {
      emitStackReturn(false);
    }
    return;
  }

  if (currentFunction_->returnType.isVoid())
  {
    emitUncondJump(current_->voidEpilogueLabel);
    return;
  }

  emitExpr(*statement.expression);
  emitExtendOperand(*statement.expression,
                    currentFunction_->returnType.isWide());
  emitStoreReturnValue();
  emitUncondJump(current_->valueEpilogueLabel);
}
} // namespace strella
