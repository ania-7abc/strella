#include "strella/sema.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

#include "sema_internal.hpp"
#include "strella/ast.hpp"
#include "strella/diagnostics.hpp"
#include "strella/type.hpp"

namespace strella
{
namespace
{

auto isBuiltin(std::string const& name) -> bool
{
  static std::unordered_set<std::string> const builtins {
      "puts",   "put", "draw", "indicator", "getchar", "set_devices", "random",
      "sizeof", "len", "hi",   "lo",        "rol8",    "ror8",
  };
  return builtins.count(name) != 0;
}

} // namespace

auto Sema::checkCall(Expr& expression) -> Type
{
  if (isBuiltin(expression.name))
  {
    expression.builtin = true;
    return checkBuiltinCall(expression);
  }

  auto const it = functions_.find(expression.name);
  if (it == functions_.end())
  {
    fail(file_, expression.location,
         "unknown function '" + expression.name + "'");
  }

  auto* function = it->second;
  expression.function = function;

  if (function->name == "main" && currentFunction_ != function)
  {
    fail(file_, expression.location, "cannot call 'main'");
  }

  if (expression.arguments.size() != function->parameters.size())
  {
    fail(file_, expression.location,
         "function '" + function->name + "' expects "
             + std::to_string(function->parameters.size())
             + " argument(s), got "
             + std::to_string(expression.arguments.size()));
  }

  for (std::size_t i = 0; i < expression.arguments.size(); ++i)
  {
    auto& argument = expression.arguments[i];
    auto const& parameter = function->parameters[i];
    if (parameter.type.isStruct())
    {
      checkStructInitializer(argument, parameter.type);
      continue;
    }
    auto const argumentType = checkExpression(*argument);
    if (!canAssign(*argument, parameter.type))
    {
      fail(file_, argument->location,
           "argument " + std::to_string(i + 1) + " of '" + function->name
               + "' expects " + formatType(parameter.type) + ", got "
               + formatType(argumentType));
    }
    coerce(expression.arguments[i], parameter.type);
  }

  if (currentFunction_ != nullptr
      && currentFunction_->annotation != FunctionAnnotation::Stack
      && function->annotation == FunctionAnnotation::None)
  {
    fail(file_, expression.location,
         "function '" + currentFunction_->name
             + "' is not @stack and can only call @inline and @stack "
               "functions; mark '"
             + function->name + "' as @inline or @stack");
  }

  addCallEdge(function);
  expression.type = function->returnType;
  return expression.type;
}

auto Sema::checkBuiltinCall(Expr& expression) -> Type
{
  auto const name = expression.name;

  auto checkInteger = [&](std::size_t index, char const* what) -> void
  {
    if (index >= expression.arguments.size())
    {
      fail(file_, expression.location,
           std::string {"missing "} + what + " argument");
    }
    auto const type = checkExpression(*expression.arguments[index]);
    if (!integerOrBool(type))
    {
      fail(file_, expression.arguments[index]->location,
           std::string {what} + " argument must be an integer");
    }
  };

  if (name == "sizeof")
  {
    if (!expression.arguments.empty())
    {
      fail(file_, expression.location, "'sizeof' takes a type, not a value");
    }
    auto const& type = expression.targetType;
    if (type.isVoid() || type.isAuto())
    {
      fail(file_, expression.location, "'sizeof' expects a concrete type");
    }
    auto const size = type.isArray()
                          ? type.arrayLength() * type.pointee().size()
                          : type.size();
    expression.type = Type::make(TypeKind::Byte);
    recordConstant(expression, static_cast<std::uint32_t>(size));
  }
  else if (name == "len")
  {
    if (expression.arguments.size() != 1)
    {
      fail(file_, expression.location, "'len' expects 1 argument");
    }
    auto const type = checkExpression(*expression.arguments[0]);
    int length = -1;
    if (type.isArray())
    {
      length = type.arrayLength();
    }
    else if (expression.arguments[0]->kind == ExprKind::StringLiteral)
    {
      length = static_cast<int>(expression.arguments[0]->stringValue.size());
    }
    if (length < 0)
    {
      fail(file_, expression.arguments[0]->location,
           "'len' expects an array or string literal, got " + formatType(type));
    }
    expression.type = Type::make(TypeKind::Byte);
    recordConstant(expression, static_cast<std::uint32_t>(length));
  }
  else if (name == "hi" || name == "lo")
  {
    if (expression.arguments.size() != 1)
    {
      fail(file_, expression.location,
           std::string {"'"} + name + "' expects 1 argument");
    }
    auto const type = checkExpression(*expression.arguments[0]);
    if (!integerOrBool(type) && !type.isPointer())
    {
      fail(file_, expression.arguments[0]->location,
           std::string {"'"} + name + "' expects an integer or pointer");
    }
    expression.type = Type::make(TypeKind::Byte);
    if (expression.arguments[0]->isConstant)
    {
      auto const value = expression.arguments[0]->constantValue;
      recordConstant(expression,
                     name == "hi" ? (value >> 8) & 0xFFU : value & 0xFFU);
    }
  }
  else if (name == "rol8" || name == "ror8")
  {
    if (expression.arguments.size() != 1)
    {
      fail(file_, expression.location,
           std::string {"'"} + name + "' expects 1 argument");
    }
    auto const type = checkExpression(*expression.arguments[0]);
    if (!integerOrBool(type))
    {
      fail(file_, expression.arguments[0]->location,
           std::string {"'"} + name + "' expects an integer");
    }
    expression.type = Type::make(TypeKind::Byte);
    if (expression.arguments[0]->isConstant)
    {
      auto const value = static_cast<std::uint8_t>(
          expression.arguments[0]->constantValue & 0xFFU);
      auto const rotated
          = name == "rol8"
                ? static_cast<std::uint8_t>((value << 1) | (value >> 7))
                : static_cast<std::uint8_t>((value >> 1) | (value << 7));
      recordConstant(expression, rotated);
    }
  }
  else if (name == "puts")
  {
    if (expression.arguments.size() != 1)
    {
      fail(file_, expression.location, "'puts' expects 1 argument");
    }
    auto const type = checkExpression(*expression.arguments[0]);
    if (!sameType(type, Type::pointerTo(Type::make(TypeKind::Byte))))
    {
      fail(file_, expression.arguments[0]->location,
           "'puts' expects ptr<byte>");
    }
    expression.type = Type::make(TypeKind::Void);
  }
  else if (name == "put")
  {
    if (expression.arguments.size() != 1 && expression.arguments.size() != 6)
    {
      fail(file_, expression.location, "'put' expects 1 or 6 arguments");
    }
    for (std::size_t i = 0; i < expression.arguments.size(); ++i)
    {
      checkInteger(i, "put");
    }
    expression.type = Type::make(TypeKind::Void);
  }
  else if (name == "draw")
  {
    if (expression.arguments.size() != 2)
    {
      fail(file_, expression.location, "'draw' expects 2 arguments");
    }
    checkInteger(0, "draw");
    checkInteger(1, "draw");
    expression.type = Type::make(TypeKind::Void);
  }
  else if (name == "indicator")
  {
    if (expression.arguments.size() != 1)
    {
      fail(file_, expression.location, "'indicator' expects 1 argument");
    }
    checkInteger(0, "indicator");
    expression.type = Type::make(TypeKind::Void);
  }
  else if (name == "getchar")
  {
    if (!expression.arguments.empty())
    {
      fail(file_, expression.location, "'getchar' expects no arguments");
    }
    expression.type = Type::make(TypeKind::Byte);
  }
  else if (name == "set_devices")
  {
    if (expression.arguments.size() != 1)
    {
      fail(file_, expression.location, "'set_devices' expects 1 argument");
    }
    checkInteger(0, "set_devices");
    // The argument may be a runtime value that enables the display, so the
    // display memory range is always reserved.
    expression.type = Type::make(TypeKind::Void);
  }
  else if (name == "random")
  {
    if (!expression.arguments.empty())
    {
      fail(file_, expression.location, "'random' expects no arguments");
    }
    expression.type = Type::make(TypeKind::Byte);
  }
  else
  {
    fail(file_, expression.location, "unknown builtin '" + name + "'");
  }

  return expression.type;
}

} // namespace strella
