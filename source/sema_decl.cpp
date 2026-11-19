#include "strella/sema.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "sema_internal.hpp"
#include "strella/ast.hpp"
#include "strella/diagnostics.hpp"
#include "strella/location.hpp"
#include "strella/preimage.hpp"
#include "strella/target.hpp"
#include "strella/type.hpp"

namespace strella
{

void Sema::declareBuiltinConstants()
{
  struct BuiltinConstant
  {
    char const* name;
    std::uint32_t value;
  };

  static constexpr std::array<BuiltinConstant, 5> builtins {{
      {"display", 48},
      {"display_mono", 16},
      {"terminal", 1},
      {"indicator", 4},
      {"indicator_signed", 12},
  }};

  for (auto const& builtin : builtins)
  {
    auto* symbol = program_.newSymbol();
    symbol->name = builtin.name;
    symbol->type = Type::make(TypeKind::Byte);
    symbol->storage = Storage::Constant;
    symbol->constantValue = builtin.value;
    symbol->readOnly = true;
    declareSymbol(symbol);
  }
}

void Sema::collectFunctions()
{
  for (auto const& function : program_.functions)
  {
    if (functions_.count(function->name) != 0)
    {
      fail(file_, function->location,
           "duplicate function '" + function->name + "'");
    }

    functions_[function->name] = function.get();

    if (function->annotation == FunctionAnnotation::Stack)
    {
      usesStack_ = true;
    }

    if (function->returnType.isAuto())
    {
      fail(file_, function->location,
           "a function return type cannot be 'auto'");
    }
    function->returnType = decayed(function->returnType);

    for (auto& parameter : function->parameters)
    {
      if (parameter.type.isAuto())
      {
        fail(file_, parameter.location, "a parameter type cannot be 'auto'");
      }
      parameter.type = decayed(parameter.type);
    }
  }

  auto const main = functions_.find("main");
  if (main == functions_.end())
  {
    throw CompileError {"error: no 'main' function found"};
  }

  if (!main->second->parameters.empty())
  {
    fail(file_, main->second->location, "'main' must not have parameters");
  }
}

void Sema::declareGlobals()
{
  for (auto const& declaration : program_.globals)
  {
    declareGlobal(*declaration);
  }
}

void Sema::declareGlobal(VarDecl& declaration)
{
  if (scopes_.back().count(declaration.name) != 0)
  {
    fail(file_, declaration.location,
         "duplicate global '" + declaration.name + "'");
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

  auto* symbol = makeStaticSymbol(declaration.name, declaration.type,
                                  Storage::Global, declaration.location);
  symbol->readOnly = declaration.isConst;
  declaration.symbol = symbol;

  if (declaration.type.isStruct())
  {
    if (declaration.initializer)
    {
      checkStructInitializer(declaration.initializer, declaration.type);
    }
    symbol->address
        = allocateData(declaration.type.size(), declaration.location);
    if (declaration.initializer && declaration.initializer->isAggregateConstant)
    {
      emitStructData(*declaration.initializer, declaration.type,
                     symbol->address);
    }
    else if (declaration.initializer)
    {
      if (declaration.isConst)
      {
        fail(file_, declaration.location,
             "a constant struct needs a constant initializer");
      }
      runtimeGlobalInitializers_.push_back(&declaration);
    }
    declareSymbol(symbol);
    return;
  }

  if (declaration.type.isArray())
  {
    auto const elementType = declaration.type.pointee();
    if (declaration.initializer)
    {
      if (declaration.initializer->kind != ExprKind::ArrayLiteral)
      {
        fail(file_, declaration.initializer->location,
             "an array initializer must be an array literal");
      }
      checkArrayLiteral(*declaration.initializer, elementType,
                        declaration.type.arrayLength());
    }

    symbol->arrayLength = declaration.type.arrayLength();
    symbol->arrayAddress
        = allocateData(declaration.type.arrayLength() * elementType.size(),
                       declaration.location);
    if (declaration.initializer)
    {
      emitArrayData(*declaration.initializer, elementType,
                    symbol->arrayAddress);
    }
    symbol->address = allocateData(2, declaration.location);
    dataInitializers_.push_back(DataInitializer {
        symbol->address, 2, static_cast<std::uint32_t>(symbol->arrayAddress)});
    declareSymbol(symbol);
    return;
  }

  bool foldedConstant = false;
  if (declaration.initializer)
  {
    auto const valueType = checkExpression(*declaration.initializer);
    if (!canAssign(*declaration.initializer, declaration.type))
    {
      fail(file_, declaration.initializer->location,
           "cannot initialize '" + declaration.name + "' of type "
               + formatType(declaration.type) + " with "
               + formatType(valueType));
    }
    coerce(declaration.initializer, declaration.type);

    if (declaration.initializer->isConstant)
    {
      if (!fitsType(declaration.type, declaration.initializer->constantValue))
      {
        fail(file_, declaration.initializer->location,
             "constant value "
                 + std::to_string(declaration.initializer->constantValue)
                 + " does not fit into " + formatType(declaration.type));
      }

      if (declaration.isConst)
      {
        symbol->storage = Storage::Constant;
        symbol->constantValue = declaration.initializer->constantValue;
        symbol->address = -1;
        foldedConstant = true;
      }
      else
      {
        symbol->address
            = allocateData(declaration.type.size(), declaration.location);
        dataInitializers_.push_back(
            DataInitializer {symbol->address, declaration.type.size(),
                             declaration.initializer->constantValue});
      }
    }
    else
    {
      runtimeGlobalInitializers_.push_back(&declaration);
    }
  }

  if (!foldedConstant && symbol->address < 0)
  {
    symbol->address
        = allocateData(declaration.type.size(), declaration.location);
  }

  declareSymbol(symbol);
}

void Sema::checkPreimage()
{
  if (!program_.preimage.present)
  {
    return;
  }

  if (target_ == Target::K32)
  {
    fail(file_, SourceLocation {},
         "preimage is not supported on the 32k target");
  }

  if (!program_.preimage.sourcePath.empty())
  {
    auto image
        = loadPreimage(program_.preimage.sourcePath, file_, SourceLocation {});
    image.present = true;
    program_.preimage = std::move(image);
  }

  // A preimage is shown on the display, so the display device is enabled by
  // the compiler: port 0x3E is set to the display bits (48).

  if (program_.preimage.size != 0)
  {
    if (program_.preimage.size != 32 && program_.preimage.size != 64)
    {
      fail(file_, SourceLocation {}, "preimage must be 32 or 64 bytes");
    }
    if (static_cast<int>(program_.preimage.bytes.size())
        != program_.preimage.size)
    {
      fail(file_, SourceLocation {}, "preimage has an invalid size");
    }
  }
}

} // namespace strella
