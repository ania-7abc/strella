#include "strella/sema.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sema_internal.hpp"
#include "strella/ast.hpp"
#include "strella/diagnostics.hpp"
#include "strella/location.hpp"
#include "strella/target.hpp"
#include "strella/type.hpp"

namespace strella
{
namespace
{

constexpr int kDataStart = 0x17;
constexpr int kDataLimit = 0x39;

[[nodiscard]] auto isConstantByteForSByte(Expr const& expression,
                                          Type const& destination) -> bool
{
  return destination.kind() == TypeKind::SByte
         && expression.type.kind() == TypeKind::Byte && expression.isConstant
         && expression.constantValue <= 0x7FU;
}

} // namespace

Sema::Sema(Program& program, std::string file, Target target)
    : program_ {program}, file_ {std::move(file)}, target_ {target},
      dataCursor_ {kDataStart}
{
}

void Sema::analyze()
{
  collectFunctions();

  scopes_.clear();
  pushScope();
  declareBuiltinConstants();
  pushScope();
  // The preimage reserves the display memory, so it must be checked before any
  // common data is allocated.
  checkPreimage();
  for (auto& function : program_.functions)
  {
    if (function->returnType.isStruct())
    {
      function->structReturnAddress
          = allocateData(function->returnType.size(), function->location);
    }
  }
  declareGlobals();
  for (auto& assertion : program_.staticAsserts)
  {
    checkStatement(*assertion);
  }
  checkFunctions();
  checkCallGraph();
  finalizeLayout();
}

void Sema::pushScope() { scopes_.emplace_back(); }

void Sema::popScope() { scopes_.pop_back(); }

auto Sema::lookup(std::string const& name) const -> Symbol*
{
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
  {
    auto const found = it->find(name);
    if (found != it->end())
    {
      return found->second;
    }
  }
  return nullptr;
}

void Sema::declareSymbol(Symbol* symbol)
{
  auto& scope = scopes_.back();
  if (scope.count(symbol->name) != 0)
  {
    fail(file_, symbol->location,
         "duplicate declaration of '" + symbol->name + "'");
  }
  scope[symbol->name] = symbol;
}

auto Sema::allocateData(int size, SourceLocation const& location) -> int
{
  if (size <= 0)
  {
    return dataCursor_;
  }

  if (dataCursor_ + size > kDataLimit + 1)
  {
    fail(file_, location, "common data exceeds the available RAM");
  }

  auto const address = dataCursor_;
  dataCursor_ += size;
  return address;
}

auto Sema::allocateString(std::string const& text,
                          SourceLocation const& location) -> int
{
  auto const found = stringAddresses_.find(text);
  if (found != stringAddresses_.end())
  {
    return found->second;
  }

  auto const address
      = allocateData(static_cast<int>(text.size()) + 1, location);
  stringAddresses_[text] = address;
  stringData_.emplace_back(address, text);
  return address;
}

auto Sema::makeStaticSymbol(std::string const& name, Type type, Storage storage,
                            SourceLocation const& location) -> Symbol*
{
  auto* symbol = program_.newSymbol();
  symbol->name = name;
  symbol->type = std::move(type);
  symbol->storage = storage;
  symbol->location = location;
  return symbol;
}

auto Sema::fitsType(Type const& type, std::uint32_t value) -> bool
{
  switch (type.kind())
  {
  case TypeKind::Byte:
  case TypeKind::Bool:
    return value <= 0xFF;
  case TypeKind::SByte:
    // Accept 0..127 and two's-complement values for -128..-1.
    return value <= 127 || value >= 0xFFFFFF80;
  case TypeKind::Word:
  case TypeKind::Ptr:
    return value <= 0xFFFF || value >= 0xFFFF8000;
  case TypeKind::Sword:
    return value <= 0x7FFF || value >= 0xFFFF8000;
  case TypeKind::Void:
  case TypeKind::Struct:
  case TypeKind::Auto:
    return false;
  }
  return false;
}

void Sema::recordConstant(Expr& expression, std::uint32_t value)
{
  expression.isConstant = true;
  expression.constantValue = value;
}

auto Sema::checkArrayLiteral(Expr& literal, Type elementType,
                             int expectedLength) -> Type
{
  if (literal.arguments.empty())
  {
    fail(file_, literal.location, "array literal cannot be empty");
  }

  auto const count = static_cast<int>(literal.arguments.size());
  if (expectedLength >= 0 && count != expectedLength)
  {
    fail(file_, literal.location,
         "array literal has " + std::to_string(count) + " element(s), expected "
             + std::to_string(expectedLength));
  }

  if (elementType.isStruct())
  {
    for (auto& argument : literal.arguments)
    {
      if (argument->kind == ExprKind::StructLiteral)
      {
        checkStructLiteral(*argument, elementType);
      }
      else
      {
        auto const type = checkExpression(*argument);
        if (!sameType(elementType, type))
        {
          fail(file_, argument->location,
               "array element expects " + formatType(elementType) + ", got "
                   + formatType(type));
        }
      }
    }
    return Type::arrayOf(elementType, count);
  }

  Type inferred = Type::make(TypeKind::Auto);
  for (auto& argument : literal.arguments)
  {
    auto const type = checkExpression(*argument);
    if (!integerOrBool(type))
    {
      fail(file_, argument->location,
           "array literal elements must be integers");
    }
    if (!argument->isConstant)
    {
      fail(file_, argument->location,
           "array literal elements must be constant");
    }
    if (elementType.isAuto())
    {
      inferred = inferred.isAuto() ? type : commonType(inferred, type);
    }
  }

  if (elementType.isAuto())
  {
    elementType = inferred;
  }

  for (auto& argument : literal.arguments)
  {
    if (!fitsType(elementType, argument->constantValue))
    {
      fail(file_, argument->location,
           "array literal element does not fit into "
               + formatType(elementType));
    }
  }

  return Type::arrayOf(elementType, count);
}

void Sema::emitArrayData(Expr const& literal, Type const& elementType,
                         int address)
{
  auto const size = elementType.size();
  for (std::size_t i = 0; i < literal.arguments.size(); ++i)
  {
    auto const offset = address + (static_cast<int>(i) * size);
    if (elementType.isStruct())
    {
      emitStructData(*literal.arguments[i], elementType, offset);
      continue;
    }
    auto value = literal.arguments[i]->constantValue;
    if (elementType.isBool())
    {
      value = value == 0 ? 0 : 1;
    }
    else if (size == 1)
    {
      value &= 0xFFU;
    }
    else
    {
      value &= 0xFFFFU;
    }
    dataInitializers_.push_back(DataInitializer {offset, size, value});
  }
}

auto Sema::checkStructLiteral(Expr& literal, Type const& structType) -> Type
{
  if (!structType.isStruct() || structType.structure() == nullptr
      || !structType.structure()->complete)
  {
    fail(file_, literal.location,
         "struct initializer needs a complete struct type");
  }
  if (literal.kind != ExprKind::StructLiteral)
  {
    fail(file_, literal.location,
         "struct initializer must be a braced literal");
  }

  auto const& fields = structType.structure()->fields;
  if (literal.arguments.size() != fields.size())
  {
    fail(file_, literal.location,
         "struct initializer has " + std::to_string(literal.arguments.size())
             + " field(s), expected " + std::to_string(fields.size()));
  }

  bool allConstant = true;
  for (std::size_t i = 0; i < fields.size(); ++i)
  {
    auto& argument = *literal.arguments[i];
    auto const& field = fields[i];
    if (field.type.isStruct())
    {
      if (argument.kind == ExprKind::StructLiteral)
      {
        checkStructLiteral(argument, field.type);
        allConstant = allConstant && argument.isAggregateConstant;
      }
      else
      {
        auto const type = checkExpression(argument);
        if (!sameType(field.type, type))
        {
          fail(file_, argument.location,
               "field '" + field.name + "' expects " + formatType(field.type)
                   + ", got " + formatType(type));
        }
        allConstant = false;
      }
      continue;
    }

    if (field.type.isArray())
    {
      if (argument.kind != ExprKind::ArrayLiteral)
      {
        fail(file_, argument.location,
             "array field '" + field.name + "' needs an array literal");
      }
      checkArrayLiteral(argument, field.type.pointee(),
                        field.type.arrayLength());
      continue;
    }

    auto const type = checkExpression(argument);
    if (!canAssign(argument, field.type))
    {
      fail(file_, argument.location,
           "field '" + field.name + "' expects " + formatType(field.type)
               + ", got " + formatType(type));
    }
    coerce(literal.arguments[i], field.type);
    allConstant = allConstant && argument.isConstant;
  }

  literal.type = structType;
  if (allConstant)
  {
    literal.dataAddress = allocateData(structType.size(), literal.location);
    emitStructData(literal, structType, literal.dataAddress);
    literal.isAggregateConstant = true;
  }
  return literal.type;
}

void Sema::emitStructData(Expr const& literal, Type const& structType,
                          int address)
{
  auto const& fields = structType.structure()->fields;
  for (std::size_t i = 0; i < fields.size(); ++i)
  {
    auto const& field = fields[i];
    if (field.type.isStruct())
    {
      emitStructData(*literal.arguments[i], field.type, address + field.offset);
      continue;
    }
    if (field.type.isArray())
    {
      emitArrayData(*literal.arguments[i], field.type.pointee(),
                    address + field.offset);
      continue;
    }

    auto value = literal.arguments[i]->constantValue;
    if (field.type.isBool())
    {
      value = value == 0 ? 0 : 1;
    }
    auto const size = field.type.size();
    for (int byte = 0; byte < size; ++byte)
    {
      dataInitializers_.push_back(DataInitializer {
          address + field.offset + byte, 1, (value >> (8 * byte)) & 0xFFU});
    }
  }
}

void Sema::checkStructInitializer(ExprPtr& initializer, Type const& destination)
{
  if (!destination.isStruct())
  {
    return;
  }

  if (initializer->kind == ExprKind::StructLiteral)
  {
    checkStructLiteral(*initializer, destination);
    return;
  }

  auto const type = checkExpression(*initializer);
  if (!sameType(destination, type))
  {
    fail(file_, initializer->location,
         "cannot initialize a struct of type " + formatType(destination)
             + " with " + formatType(type));
  }
}

auto Sema::canAssign(Expr const& expression, Type const& destination) -> bool
{
  return assignable(destination, expression.type)
         || isConstantByteForSByte(expression, destination);
}

void Sema::coerce(ExprPtr& expression, Type const& destination)
{
  bool const toBool = destination.isBool() && !expression->type.isBool();
  bool const byteToSByte = isConstantByteForSByte(*expression, destination);
  if (!toBool && !byteToSByte)
  {
    return;
  }

  // An implicit conversion to bool is a real conversion: store 0 or 1 rather
  // than the original nonzero value.  A fitting constant byte only needs its
  // expression type changed to sbyte.
  auto const target = toBool ? Type::make(TypeKind::Bool) : destination;
  auto cast = std::make_unique<Expr>();
  cast->kind = ExprKind::Cast;
  cast->location = expression->location;
  cast->targetType = target;
  cast->type = target;
  cast->lhs = std::move(expression);
  if (cast->lhs->isConstant)
  {
    auto value = cast->lhs->constantValue;
    if (toBool)
    {
      value = value == 0 ? 0U : 1U;
    }
    recordConstant(*cast, value);
  }
  expression = std::move(cast);
}

void Sema::addCallEdge(FunctionDecl* callee)
{
  if (currentFunction_ != nullptr)
  {
    callEdges_[currentFunction_].push_back(callee);
  }
}

void Sema::checkCallGraph()
{
  std::unordered_map<FunctionDecl*, int> colors;
  std::vector<FunctionDecl*> stack;
  for (auto const& function : program_.functions)
  {
    if (colors[function.get()] == 0)
    {
      dfsRecursion(function.get(), colors, stack);
    }
  }
}

void Sema::dfsRecursion(FunctionDecl* function,
                        std::unordered_map<FunctionDecl*, int>& colors,
                        std::vector<FunctionDecl*>& stack)
{
  colors[function] = 1;
  stack.push_back(function);

  auto const found = callEdges_.find(function);
  if (found != callEdges_.end())
  {
    for (auto* callee : found->second)
    {
      if (colors[callee] == 0)
      {
        dfsRecursion(callee, colors, stack);
      }
      else if (colors[callee] == 1)
      {
        auto const it = std::find(stack.begin(), stack.end(), callee);
        bool requiresStack = false;
        for (auto cycle = it; cycle != stack.end(); ++cycle)
        {
          if ((*cycle)->annotation != FunctionAnnotation::Stack)
          {
            requiresStack = true;
            break;
          }
        }

        if (requiresStack)
        {
          fail(file_, function->location,
               "recursive call involving '" + function->name
                   + "' requires every function in the cycle to be @stack");
        }
      }
    }
  }

  stack.pop_back();
  colors[function] = 2;
}

void Sema::finalizeLayout()
{
  if (dataCursor_ - 1 > kDataLimit)
  {
    fail(file_, SourceLocation {},
         "common data needs " + std::to_string(dataCursor_ - kDataStart)
             + " bytes but only " + std::to_string(kDataLimit - kDataStart + 1)
             + " bytes are available");
  }
}

auto Sema::stringAddress(std::string const& text) const -> int
{
  auto const found = stringAddresses_.find(text);
  return found == stringAddresses_.end() ? -1 : found->second;
}

} // namespace strella
