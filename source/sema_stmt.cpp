#include "strella/sema.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>

#include "sema_internal.hpp"
#include "strella/ast.hpp"
#include "strella/diagnostics.hpp"
#include "strella/type.hpp"

namespace strella
{

void Sema::checkFunctions()
{
  for (auto const& function : program_.functions)
  {
    checkFunction(*function);
  }
}

void Sema::checkFunction(FunctionDecl& function)
{
  currentFunction_ = &function;
  currentFrameSize_ = 0;
  loopDepth_ = 0;
  switchDepth_ = 0;

  pushScope();

  int parameterOffset = 0;
  for (auto& parameter : function.parameters)
  {
    if (scopes_.back().count(parameter.name) != 0)
    {
      fail(file_, parameter.location,
           "duplicate parameter '" + parameter.name + "'");
    }

    auto* symbol = program_.newSymbol();
    symbol->name = parameter.name;
    symbol->type = parameter.type;
    symbol->location = parameter.location;

    if (function.annotation == FunctionAnnotation::Stack)
    {
      symbol->storage = Storage::StackParameter;
      symbol->frameOffset = parameterOffset;
      usesStack_ = true;
    }
    else if (function.annotation == FunctionAnnotation::Inline)
    {
      // Inlined parameters live in the caller's temporaries.
      symbol->storage = Storage::Parameter;
      symbol->address = -1;
    }
    else
    {
      symbol->storage = Storage::Parameter;
      symbol->address = allocateData(parameter.type.size(), parameter.location);
    }

    parameterOffset += parameter.type.size();
    parameter.symbol = symbol;
    declareSymbol(symbol);
  }

  function.stackParamBytes = parameterOffset;

  if (function.body)
  {
    checkBlock(*function.body, false);
  }

  if (function.annotation == FunctionAnnotation::Stack)
  {
    function.stackFrameSize = currentFrameSize_;
  }

  popScope();
  currentFunction_ = nullptr;
}

void Sema::checkStatement(Stmt& statement)
{
  switch (statement.kind)
  {
  case StmtKind::Block:
    checkBlock(statement);
    break;

  case StmtKind::Declaration:
    checkDeclaration(statement);
    break;

  case StmtKind::Expression:
    if (statement.expression)
    {
      checkExpression(*statement.expression);
    }
    break;

  case StmtKind::If:
    checkCondition(*statement.expression, "if");
    checkStatement(*statement.thenBranch);
    if (statement.elseBranch)
    {
      checkStatement(*statement.elseBranch);
    }
    break;

  case StmtKind::While:
    checkCondition(*statement.expression, "while");
    ++loopDepth_;
    checkStatement(*statement.thenBranch);
    --loopDepth_;
    break;

  case StmtKind::DoWhile:
    ++loopDepth_;
    checkStatement(*statement.thenBranch);
    --loopDepth_;
    checkCondition(*statement.expression, "do-while");
    break;

  case StmtKind::For:
  {
    pushScope();
    if (statement.declaration)
    {
      checkDeclaration(statement);
    }
    else if (statement.expression)
    {
      checkExpression(*statement.expression);
    }

    if (statement.secondExpression)
    {
      checkCondition(*statement.secondExpression, "for");
    }

    if (statement.thirdExpression)
    {
      checkExpression(*statement.thirdExpression);
    }

    ++loopDepth_;
    checkStatement(*statement.thenBranch);
    --loopDepth_;
    popScope();
    break;
  }

  case StmtKind::Switch:
  {
    auto const valueType = checkExpression(*statement.expression);
    if (!integerOrBool(valueType))
    {
      fail(file_, statement.expression->location,
           "switch value must be an integer");
    }

    std::unordered_set<std::uint32_t> seen;
    ++switchDepth_;
    for (auto& switchCase : statement.cases)
    {
      for (auto& value : switchCase.values)
      {
        auto const caseType = checkExpression(*value);
        if (!integerOrBool(caseType))
        {
          fail(file_, value->location, "case value must be an integer");
        }
        if (!value->isConstant)
        {
          fail(file_, value->location,
               "case value must be a compile-time constant");
        }
        if (!seen.insert(value->constantValue).second)
        {
          fail(file_, value->location, "duplicate case value");
        }
      }

      for (auto& child : switchCase.body)
      {
        checkStatement(*child);
      }
    }
    --switchDepth_;
    break;
  }

  case StmtKind::Break:
    if (loopDepth_ == 0 && switchDepth_ == 0)
    {
      fail(file_, statement.location, "'break' outside of a loop or switch");
    }
    break;

  case StmtKind::Continue:
    if (loopDepth_ == 0)
    {
      fail(file_, statement.location, "'continue' outside of a loop");
    }
    break;

  case StmtKind::Return:
    if (currentFunction_->returnType.isVoid())
    {
      if (statement.expression)
      {
        fail(file_, statement.location, "void function cannot return a value");
      }
    }
    else
    {
      if (!statement.expression)
      {
        fail(file_, statement.location,
             "non-void function must return a value");
      }

      if (currentFunction_->returnType.isStruct())
      {
        checkStructInitializer(statement.expression,
                               currentFunction_->returnType);
        break;
      }

      auto const valueType = checkExpression(*statement.expression);
      if (!canAssign(*statement.expression, currentFunction_->returnType))
      {
        fail(file_, statement.expression->location,
             "cannot return " + formatType(valueType)
                 + " from a function returning "
                 + formatType(currentFunction_->returnType));
      }
      coerce(statement.expression, currentFunction_->returnType);
    }
    break;

  case StmtKind::Halt:
  case StmtKind::Empty:
    break;

  case StmtKind::StaticAssert:
  {
    if (!statement.expression)
    {
      fail(file_, statement.location, "static_assert needs a condition");
    }
    checkExpression(*statement.expression);
    if (!statement.expression->isConstant)
    {
      fail(file_, statement.location,
           "static_assert condition must be a constant expression");
    }
    if (statement.expression->constantValue == 0)
    {
      fail(file_, statement.location,
           "static assertion failed: " + statement.message);
    }
    break;
  }
  }
}

void Sema::checkBlock(Stmt& statement, bool createScope)
{
  if (createScope)
  {
    pushScope();
  }

  for (auto& child : statement.statements)
  {
    checkStatement(*child);
  }

  if (createScope)
  {
    popScope();
  }
}

void Sema::checkDeclaration(Stmt& statement)
{
  auto& declaration = *statement.declaration;

  if (scopes_.back().count(declaration.name) != 0)
  {
    fail(file_, declaration.location,
         "duplicate variable '" + declaration.name + "'");
  }

  if (declaration.isConst && !declaration.initializer)
  {
    fail(file_, declaration.location,
         "constant '" + declaration.name + "' needs an initializer");
  }

  if (declaration.type.isAuto())
  {
    if (!declaration.initializer)
    {
      fail(file_, declaration.location,
           "'auto' needs an initializer to deduce the type");
    }
    auto const valueType = checkExpression(*declaration.initializer);
    if (valueType.isVoid() || valueType.isAuto())
    {
      fail(file_, declaration.initializer->location,
           "cannot deduce a type from " + formatType(valueType));
    }
    declaration.type = decayed(valueType);
  }

  Type const type = declaration.type;
  auto const stackBased
      = currentFunction_->annotation == FunctionAnnotation::Stack;

  auto* symbol = program_.newSymbol();
  symbol->name = declaration.name;
  symbol->type = type;
  symbol->location = declaration.location;
  symbol->readOnly = declaration.isConst;
  declaration.symbol = symbol;

  if (type.isArray())
  {
    auto const& elementType = type.pointee();
    if (declaration.initializer)
    {
      if (stackBased
          || currentFunction_->annotation == FunctionAnnotation::Inline)
      {
        fail(file_, declaration.location,
             "an array in an @inline or @stack function cannot have an "
             "initializer");
      }
      if (declaration.initializer->kind != ExprKind::ArrayLiteral)
      {
        fail(file_, declaration.initializer->location,
             "an array initializer must be an array literal");
      }
      checkArrayLiteral(*declaration.initializer, elementType,
                        type.arrayLength());
    }

    auto const backingSize = type.arrayLength() * elementType.size();
    symbol->arrayLength = type.arrayLength();

    if (stackBased)
    {
      symbol->storage = Storage::StackLocal;
      symbol->frameOffset = currentFrameSize_;
      currentFrameSize_ += 2;
      symbol->arrayFrameOffset = currentFrameSize_;
      currentFrameSize_ += backingSize;
    }
    else if (currentFunction_->annotation == FunctionAnnotation::Inline)
    {
      // Inlined locals live in the caller's temporaries.
      symbol->storage = Storage::Local;
      symbol->address = -1;
    }
    else
    {
      symbol->storage = Storage::Local;
      symbol->arrayAddress = allocateData(backingSize, declaration.location);
      if (declaration.initializer)
      {
        emitArrayData(*declaration.initializer, elementType,
                      symbol->arrayAddress);
      }
      symbol->address = allocateData(2, declaration.location);
      dataInitializers_.push_back(
          DataInitializer {symbol->address, 2,
                           static_cast<std::uint32_t>(symbol->arrayAddress)});
    }

    declareSymbol(symbol);
    return;
  }

  if (type.isStruct())
  {
    if (declaration.initializer)
    {
      checkStructInitializer(declaration.initializer, type);
      if (declaration.isConst && !declaration.initializer->isAggregateConstant
          && declaration.initializer->kind != ExprKind::Identifier)
      {
        fail(file_, declaration.location,
             "a constant struct needs a constant initializer");
      }
    }

    if (stackBased)
    {
      symbol->storage = Storage::StackLocal;
      symbol->frameOffset = currentFrameSize_;
      currentFrameSize_ += type.size();
    }
    else if (currentFunction_->annotation == FunctionAnnotation::Inline)
    {
      symbol->storage = Storage::Local;
      symbol->address = -1;
    }
    else
    {
      symbol->storage = Storage::Local;
      symbol->address = allocateData(type.size(), declaration.location);
    }
    declareSymbol(symbol);
    return;
  }

  bool foldedConstant = false;
  if (declaration.initializer)
  {
    auto const valueType = checkExpression(*declaration.initializer);
    if (!canAssign(*declaration.initializer, type))
    {
      fail(file_, declaration.initializer->location,
           "cannot initialize '" + declaration.name + "' of type "
               + formatType(type) + " with " + formatType(valueType));
    }
    coerce(declaration.initializer, type);

    if (declaration.isConst && declaration.initializer->isConstant)
    {
      if (!fitsType(type, declaration.initializer->constantValue))
      {
        fail(file_, declaration.initializer->location,
             "constant value "
                 + std::to_string(declaration.initializer->constantValue)
                 + " does not fit into " + formatType(type));
      }

      symbol->storage = Storage::Constant;
      symbol->constantValue = declaration.initializer->constantValue;
      symbol->address = -1;
      symbol->frameOffset = 0;
      foldedConstant = true;
    }
  }

  if (!foldedConstant)
  {
    if (stackBased)
    {
      symbol->storage = Storage::StackLocal;
      symbol->frameOffset = currentFrameSize_;
      currentFrameSize_ += type.size();
    }
    else if (currentFunction_->annotation == FunctionAnnotation::Inline)
    {
      // Inlined locals live in the caller's temporaries.
      symbol->storage = Storage::Local;
      symbol->address = -1;
    }
    else
    {
      symbol->storage = Storage::Local;
      symbol->address = allocateData(type.size(), declaration.location);
    }
  }

  declareSymbol(symbol);
}

void Sema::checkCondition(Expr& expression, char const* what)
{
  auto const type = checkExpression(expression);
  if (!conditionType(type))
  {
    fail(file_, expression.location,
         std::string {what} + " condition must be an integer, bool or pointer");
  }
}

} // namespace strella
