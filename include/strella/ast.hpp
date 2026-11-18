#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "strella/location.hpp"
#include "strella/token.hpp"
#include "strella/type.hpp"

namespace strella
{

struct Expr;
struct Stmt;
struct VarDecl;
struct FunctionDecl;
struct Symbol;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

enum class ExprKind : std::uint8_t
{
  IntLiteral,
  StringLiteral,
  Identifier,
  Unary,
  Binary,
  Ternary,
  Call,
  Assign,
  Index,
  Cast,
  IncDec,
  ArrayLiteral,
  StructLiteral,
  FieldAccess
};

struct Expr
{
  // Identifier
  Symbol* symbol = nullptr;

  // Unary / Binary / Ternary / Assign / Index / IncDec
  ExprPtr lhs;
  ExprPtr rhs;
  ExprPtr third;

  // Call
  FunctionDecl* function = nullptr;

  SourceLocation location;

  // Call / ArrayLiteral / StructLiteral
  std::vector<ExprPtr> arguments;

  // FieldAccess / StructLiteral
  int fieldIndex = -1;
  int dataAddress = -1;

  // StringLiteral
  std::string stringValue;

  // Identifier
  std::string name;

  // Cast
  Type targetType = Type::make(TypeKind::Void);

  // Filled by semantic analysis.
  Type type = Type::make(TypeKind::Void);

  // IntLiteral / CharLiteral / BoolLiteral
  std::uint32_t intValue = 0;

  // Constant value, if the expression is a compile-time constant.
  std::uint32_t constantValue = 0;

  ExprKind kind = ExprKind::IntLiteral;

  // Unary / Binary / Ternary / Assign / Index / IncDec
  TokenKind op = TokenKind::End;

  // IncDec
  bool prefix = false;

  // Call
  bool builtin = false;

  // Filled by semantic analysis.
  bool isConstant = false;
  bool isAggregateConstant = false;
};

enum class StmtKind : std::uint8_t
{
  Block,
  Declaration,
  Expression,
  If,
  While,
  DoWhile,
  For,
  Switch,
  Break,
  Continue,
  Return,
  Halt,
  StaticAssert,
  Empty
};

struct SwitchCase
{
  std::vector<ExprPtr> values;
  bool isDefault = false;
  std::vector<StmtPtr> body;
  SourceLocation location;
};

struct Stmt
{
  StmtKind kind = StmtKind::Empty;
  SourceLocation location;
  std::vector<StmtPtr> statements;
  ExprPtr expression;
  ExprPtr secondExpression;
  ExprPtr thirdExpression;
  StmtPtr thenBranch;
  StmtPtr elseBranch;
  std::unique_ptr<VarDecl> declaration;
  std::vector<SwitchCase> cases;
  std::string message;
};

enum class Storage : std::uint8_t
{
  Constant,
  Global,
  Local,
  Parameter,
  StackParameter,
  StackLocal
};

struct Symbol
{
  std::string name;
  Type type = Type::make(TypeKind::Void);
  Storage storage = Storage::Global;
  SourceLocation location;
  std::uint32_t constantValue = 0;
  int address = -1;
  int frameOffset = 0;
  bool readOnly = false;

  // Backing storage for an array declaration.  The symbol itself is a
  // pointer to the first element.
  int arrayLength = 0;
  int arrayAddress = -1;
  int arrayFrameOffset = 0;
};

enum class FunctionAnnotation : std::uint8_t
{
  None,
  Inline,
  Stack
};

struct Parameter
{
  std::string name;
  Type type = Type::make(TypeKind::Void);
  SourceLocation location;
  Symbol* symbol = nullptr;
};

struct VarDecl
{
  bool isConst = false;
  std::string name;
  Type type = Type::make(TypeKind::Void);
  ExprPtr initializer;
  SourceLocation location;
  Symbol* symbol = nullptr;
};

struct FunctionDecl
{
  std::string name;
  FunctionAnnotation annotation = FunctionAnnotation::None;
  Type returnType = Type::make(TypeKind::Void);
  std::vector<Parameter> parameters;
  StmtPtr body;
  SourceLocation location;

  // Filled by the code generator.
  int bank = -1;
  int entry = -1;

  // Filled by semantic analysis for @stack functions.
  int stackFrameSize = 0;
  int stackParamBytes = 0;

  // Storage for a struct returned by value.
  int structReturnAddress = -1;
};

struct Preimage
{
  bool present = false;
  int size = 0; // 0, 32 or 64
  std::vector<std::uint8_t> bytes;
  std::string sourcePath;
};

struct Program
{
  Preimage preimage;
  std::vector<std::unique_ptr<VarDecl>> globals;
  std::vector<std::unique_ptr<FunctionDecl>> functions;
  std::vector<std::unique_ptr<Symbol>> symbols;
  std::vector<StmtPtr> staticAsserts;
  std::vector<std::shared_ptr<StructInfo>> structs;

  auto newSymbol() -> Symbol*
  {
    symbols.push_back(std::make_unique<Symbol>());
    return symbols.back().get();
  }

  [[nodiscard]] auto
  findFunction(std::string const& name) const -> FunctionDecl*
  {
    for (auto const& function : functions)
    {
      if (function->name == name)
      {
        return function.get();
      }
    }
    return nullptr;
  }
};

} // namespace strella
