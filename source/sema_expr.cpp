#include "strella/sema.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "sema_internal.hpp"
#include "strella/ast.hpp"
#include "strella/diagnostics.hpp"
#include "strella/token.hpp"
#include "strella/type.hpp"

namespace strella
{
namespace
{
auto elementType(Type const& type) -> Type
{
  if (type.hasPointee())
  {
    return type.pointee();
  }
  // A bare `ptr` is an untyped pointer; its elements are bytes.
  if (type.isGenericPointer())
  {
    return Type::make(TypeKind::Byte);
  }
  return Type::voidType();
}

auto isLValue(Expr const& expression) -> bool
{
  switch (expression.kind)
  {
  case ExprKind::Identifier:
    return expression.symbol != nullptr
           && expression.symbol->storage != Storage::Constant;

  case ExprKind::Index:
  case ExprKind::FieldAccess:
    return true;

  case ExprKind::Unary:
    return expression.op == TokenKind::Star && expression.lhs != nullptr;

  default:
    return false;
  }
}

auto lvalueReadOnly(Expr const& expression) -> bool
{
  switch (expression.kind)
  {
  case ExprKind::Identifier:
    return expression.symbol != nullptr && expression.symbol->readOnly;

  case ExprKind::Index:
  case ExprKind::FieldAccess:
    return expression.lhs != nullptr && lvalueReadOnly(*expression.lhs);

  default:
    return false;
  }
}

} // namespace

auto Sema::checkExpression(Expr& expression) -> Type
{
  switch (expression.kind)
  {
  case ExprKind::IntLiteral:
    if (expression.type.isVoid())
    {
      if (expression.intValue <= 0xFF)
      {
        expression.type = Type::make(TypeKind::Byte);
      }
      else
      {
        expression.type = Type::make(TypeKind::Word);
      }
    }
    recordConstant(expression, expression.intValue);
    break;

  case ExprKind::StringLiteral:
  {
    auto const address
        = allocateString(expression.stringValue, expression.location);
    expression.type = Type::pointerTo(Type::make(TypeKind::Byte));
    recordConstant(expression, static_cast<std::uint32_t>(address));
    break;
  }

  case ExprKind::ArrayLiteral:
  {
    auto const arrayType
        = checkArrayLiteral(expression, Type::make(TypeKind::Auto), -1);
    auto const address
        = allocateData(arrayType.arrayLength() * arrayType.pointee().size(),
                       expression.location);
    emitArrayData(expression, arrayType.pointee(), address);
    expression.type = arrayType;
    recordConstant(expression, static_cast<std::uint32_t>(address));
    break;
  }

  case ExprKind::StructLiteral:
    fail(file_, expression.location,
         "a struct literal needs a target type in this context");
    break;

  case ExprKind::FieldAccess:
  {
    auto baseType = checkExpression(*expression.lhs);
    if (expression.op == TokenKind::Arrow)
    {
      if (!baseType.isPointer())
      {
        fail(file_, expression.location, "'->' expects a struct pointer");
      }
      baseType = elementType(baseType);
    }
    else if (baseType.isPointer())
    {
      // C++-style automatic dereference: `p.field` means `p->field` for
      // pointers to structs.
      baseType = elementType(baseType);
    }
    if (!baseType.isStruct() || baseType.structure() == nullptr
        || !baseType.structure()->complete)
    {
      fail(file_, expression.location,
           "field access expects a struct, got " + formatType(baseType));
    }

    auto const index = baseType.structure()->findField(expression.name);
    if (index < 0)
    {
      fail(file_, expression.location,
           "struct '" + baseType.toString() + "' has no field '"
               + expression.name + "'");
    }
    expression.fieldIndex = index;
    auto const fieldIndex = static_cast<std::size_t>(index);
    expression.type = baseType.structure()->fields[fieldIndex].type;
    break;
  }

  case ExprKind::Identifier:
  {
    auto* symbol = lookup(expression.name);
    if (symbol == nullptr)
    {
      fail(file_, expression.location,
           "unknown identifier '" + expression.name + "'");
    }
    expression.symbol = symbol;
    expression.type = symbol->type;
    if (symbol->storage == Storage::Constant)
    {
      recordConstant(expression, symbol->constantValue);
    }
    break;
  }

  case ExprKind::Unary:
  {
    auto const operandType = checkExpression(*expression.lhs);
    switch (expression.op)
    {
    case TokenKind::Bang:
      if (!conditionType(operandType))
      {
        fail(file_, expression.location,
             "'!' expects an integer, bool or pointer");
      }
      expression.type = Type::make(TypeKind::Bool);
      if (expression.lhs->isConstant && integerOrBool(operandType))
      {
        recordConstant(expression, expression.lhs->constantValue == 0 ? 1 : 0);
      }
      break;

    case TokenKind::Amp:
      if (!isLValue(*expression.lhs))
      {
        fail(file_, expression.location,
             "address-of expects a variable or memory location");
      }
      if (operandType.isVoid())
      {
        fail(file_, expression.location,
             "cannot take the address of a void value");
      }
      // An array already behaves like a pointer to its first element.
      expression.type = operandType.isArray() ? decayed(operandType)
                                              : Type::pointerTo(operandType);
      break;

    case TokenKind::Star:
      if (!operandType.isPointer())
      {
        fail(file_, expression.location, "'*' expects a pointer");
      }
      expression.type = elementType(operandType);
      if (expression.type.isVoid())
      {
        fail(file_, expression.location,
             "cannot dereference a pointer to void");
      }
      break;

    default:
      if (!integerOrBool(operandType))
      {
        fail(file_, expression.location, "unary operator expects an integer");
      }

      if (expression.op == TokenKind::Tilde)
      {
        expression.type
            = operandType.isBool() ? Type::make(TypeKind::Byte) : operandType;
        if (expression.lhs->isConstant)
        {
          recordConstant(expression, ~expression.lhs->constantValue);
        }
      }
      else if (expression.op == TokenKind::Plus)
      {
        expression.type = operandType;
        if (expression.lhs->isConstant)
        {
          recordConstant(expression, expression.lhs->constantValue);
        }
      }
      else // Minus
      {
        if (operandType.kind() == TypeKind::Byte)
        {
          expression.type = Type::make(TypeKind::SByte);
        }
        else if (operandType.kind() == TypeKind::Word)
        {
          expression.type = Type::make(TypeKind::Sword);
        }
        else
        {
          expression.type = operandType;
        }
        if (expression.lhs->isConstant)
        {
          recordConstant(expression, 0U - expression.lhs->constantValue);
        }
      }
      break;
    }
    break;
  }

  case ExprKind::Binary:
  {
    auto const leftType = checkExpression(*expression.lhs);
    auto const rightType = checkExpression(*expression.rhs);

    switch (expression.op)
    {
    case TokenKind::LogicalAnd:
    case TokenKind::LogicalOr:
      if (!conditionType(leftType) || !conditionType(rightType))
      {
        fail(file_, expression.location,
             "logical operators expect integers, bools or pointers");
      }
      expression.type = Type::make(TypeKind::Bool);
      if (expression.lhs->isConstant && expression.rhs->isConstant
          && integerOrBool(leftType) && integerOrBool(rightType))
      {
        auto const left = expression.lhs->constantValue != 0;
        auto const right = expression.rhs->constantValue != 0;
        auto const value = expression.op == TokenKind::LogicalAnd
                               ? (left && right)
                               : (left || right);
        recordConstant(expression, value ? 1 : 0);
      }
      break;

    case TokenKind::Equal:
    case TokenKind::NotEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
    {
      auto const leftPointer = leftType.isPointer();
      auto const rightPointer = rightType.isPointer();
      if (!(leftPointer && rightPointer)
          && !(integerOrBool(leftType) && integerOrBool(rightType)))
      {
        fail(file_, expression.location,
             "comparison expects two integers or two pointers, got "
                 + formatType(leftType) + " and " + formatType(rightType));
      }

      expression.type = Type::make(TypeKind::Bool);
      if (expression.lhs->isConstant && expression.rhs->isConstant)
      {
        auto const left = expression.lhs->constantValue;
        auto const right = expression.rhs->constantValue;
        auto const signedComparison
            = leftType.isSigned() || rightType.isSigned();
        auto const leftSigned = static_cast<std::int32_t>(left);
        auto const rightSigned = static_cast<std::int32_t>(right);
        bool value = false;
        switch (expression.op)
        {
        case TokenKind::Equal:
          value = left == right;
          break;
        case TokenKind::NotEqual:
          value = left != right;
          break;
        case TokenKind::Less:
          value = signedComparison ? leftSigned < rightSigned : left < right;
          break;
        case TokenKind::LessEqual:
          value = signedComparison ? leftSigned <= rightSigned : left <= right;
          break;
        case TokenKind::Greater:
          value = signedComparison ? leftSigned > rightSigned : left > right;
          break;
        case TokenKind::GreaterEqual:
          value = signedComparison ? leftSigned >= rightSigned : left >= right;
          break;
        default:
          break;
        }
        recordConstant(expression, value ? 1 : 0);
      }
      break;
    }

    case TokenKind::ShiftLeft:
    case TokenKind::ShiftRight:
      if (!integerOrBool(leftType) || !integerOrBool(rightType))
      {
        fail(file_, expression.location, "shift expects integer operands");
      }
      expression.type
          = leftType.isBool() ? Type::make(TypeKind::Byte) : leftType;
      if (expression.lhs->isConstant && expression.rhs->isConstant)
      {
        auto const shift = expression.rhs->constantValue & 31U;
        std::uint32_t value = 0;
        if (expression.op == TokenKind::ShiftLeft)
        {
          value = expression.lhs->constantValue << shift;
        }
        else if (leftType.isSigned())
        {
          value = static_cast<std::uint32_t>(
              static_cast<std::int32_t>(expression.lhs->constantValue)
              >> shift);
        }
        else
        {
          value = expression.lhs->constantValue >> shift;
        }
        recordConstant(expression, value);
      }
      break;

    default:
      if (!integerOrBool(leftType) || !integerOrBool(rightType))
      {
        fail(file_, expression.location,
             "arithmetic operator expects integer operands, got "
                 + formatType(leftType) + " and " + formatType(rightType));
      }

      expression.type = commonType(leftType, rightType);
      if (expression.lhs->isConstant && expression.rhs->isConstant)
      {
        auto const left = expression.lhs->constantValue;
        auto const right = expression.rhs->constantValue;
        std::uint32_t value = 0;
        switch (expression.op)
        {
        case TokenKind::Plus:
          value = left + right;
          break;
        case TokenKind::Minus:
          value = left - right;
          break;
        case TokenKind::Amp:
          value = left & right;
          break;
        case TokenKind::Pipe:
          value = left | right;
          break;
        case TokenKind::Caret:
          value = left ^ right;
          break;
        default:
          break;
        }
        recordConstant(expression, value);
      }
      break;
    }
    break;
  }

  case ExprKind::Ternary:
  {
    checkCondition(*expression.lhs, "ternary condition");
    auto const thenType = checkExpression(*expression.rhs);
    auto const elseType = checkExpression(*expression.third);

    if (thenType.isPointer() && elseType.isPointer())
    {
      expression.type = decayed(thenType);
    }
    else if (integerOrBool(thenType) && integerOrBool(elseType))
    {
      expression.type = commonType(thenType, elseType);
    }
    else
    {
      fail(file_, expression.location,
           "ternary branches must both be integers or both be pointers, got "
               + formatType(thenType) + " and " + formatType(elseType));
    }

    if (expression.lhs->isConstant && expression.rhs->isConstant
        && expression.third->isConstant)
    {
      auto const value = expression.lhs->constantValue != 0
                             ? expression.rhs->constantValue
                             : expression.third->constantValue;
      recordConstant(expression, value);
    }
    break;
  }

  case ExprKind::Index:
  {
    auto const baseType = checkExpression(*expression.lhs);
    if (!baseType.isPointer())
    {
      fail(file_, expression.location,
           "indexing expects an array or pointer, got " + formatType(baseType));
    }

    auto const indexType = checkExpression(*expression.rhs);
    if (!integerOrBool(indexType))
    {
      fail(file_, expression.rhs->location,
           "index must be an integer, got " + formatType(indexType));
    }

    expression.type = elementType(baseType);
    break;
  }

  case ExprKind::Cast:
  {
    auto const sourceType = checkExpression(*expression.lhs);
    if (expression.targetType.isAuto() || expression.targetType.isArray()
        || expression.targetType.isVoid())
    {
      fail(file_, expression.location,
           "invalid cast target type " + formatType(expression.targetType));
    }
    if (!explicitlyConvertible(expression.targetType, sourceType))
    {
      fail(file_, expression.location,
           "cannot convert " + formatType(sourceType) + " to "
               + formatType(expression.targetType) + " with 'as'");
    }

    expression.type = expression.targetType;
    if (expression.lhs->isConstant)
    {
      auto value = expression.lhs->constantValue;
      if (expression.targetType.isBool())
      {
        value = value == 0 ? 0 : 1;
      }
      else if (expression.targetType.size() == 1)
      {
        value &= 0xFF;
      }
      else if (expression.targetType.size() == 2)
      {
        value &= 0xFFFF;
      }
      recordConstant(expression, value);
    }
    break;
  }

  case ExprKind::IncDec:
  {
    auto const operandType = checkExpression(*expression.lhs);
    if (!isLValue(*expression.lhs))
    {
      fail(file_, expression.location,
           "'" + std::string {expression.op == TokenKind::Plus ? "++" : "--"}
               + "' expects a variable or memory location");
    }
    if (lvalueReadOnly(*expression.lhs))
    {
      fail(file_, expression.location, "cannot modify a constant");
    }
    if (!operandType.isInteger() || operandType.isBool())
    {
      fail(file_, expression.location,
           "'" + std::string {expression.op == TokenKind::Plus ? "++" : "--"}
               + "' expects an integer variable, got "
               + formatType(operandType));
    }
    expression.type = operandType;
    break;
  }

  case ExprKind::Assign:
  {
    auto const leftType = checkExpression(*expression.lhs);
    if (!isLValue(*expression.lhs))
    {
      fail(file_, expression.location,
           "left side of assignment must be a variable or memory location");
    }
    if (lvalueReadOnly(*expression.lhs))
    {
      fail(file_, expression.location, "cannot assign to a constant");
    }

    if (leftType.isStruct())
    {
      checkStructInitializer(expression.rhs, leftType);
      expression.type = leftType;
      break;
    }

    auto const rightType = checkExpression(*expression.rhs);
    if (!canAssign(*expression.rhs, leftType))
    {
      fail(file_, expression.location,
           "cannot assign " + formatType(rightType) + " to "
               + formatType(leftType));
    }
    coerce(expression.rhs, leftType);

    expression.type = leftType;
    break;
  }

  case ExprKind::Call:
    checkCall(expression);
    break;
  }

  return expression.type;
}

} // namespace strella
