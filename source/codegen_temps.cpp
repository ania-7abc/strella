#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/token.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace strella::codegen_detail
{

namespace
{
auto analyzeBuildStructTemps(Expr& literal) -> int;
} // namespace

void collectLocalSymbols(Stmt& statement, std::vector<Symbol const*>& symbols)
{
  switch (statement.kind)
  {
  case StmtKind::Block:
    for (auto& child : statement.statements)
    {
      collectLocalSymbols(*child, symbols);
    }
    break;

  case StmtKind::Declaration:
    if (statement.declaration && statement.declaration->symbol != nullptr
        && statement.declaration->symbol->storage == Storage::Local)
    {
      symbols.push_back(statement.declaration->symbol);
    }
    break;

  case StmtKind::If:
    if (statement.thenBranch)
    {
      collectLocalSymbols(*statement.thenBranch, symbols);
    }
    if (statement.elseBranch)
    {
      collectLocalSymbols(*statement.elseBranch, symbols);
    }
    break;

  case StmtKind::While:
  case StmtKind::DoWhile:
    if (statement.thenBranch)
    {
      collectLocalSymbols(*statement.thenBranch, symbols);
    }
    break;

  case StmtKind::For:
    if (statement.declaration && statement.declaration->symbol != nullptr
        && statement.declaration->symbol->storage == Storage::Local)
    {
      symbols.push_back(statement.declaration->symbol);
    }
    if (statement.thenBranch)
    {
      collectLocalSymbols(*statement.thenBranch, symbols);
    }
    break;

  case StmtKind::Switch:
    for (auto& switchCase : statement.cases)
    {
      for (auto& child : switchCase.body)
      {
        collectLocalSymbols(*child, symbols);
      }
    }
    break;

  default:
    break;
  }
}

auto inlineSlotCount(Symbol const& symbol) -> int
{
  if (symbol.type.isArray())
  {
    auto const bytes
        = 2 + (symbol.type.arrayLength() * symbol.type.pointee().size());
    return (bytes + 1) / 2;
  }
  if (symbol.type.isStruct())
  {
    return (symbol.type.size() + 1) / 2;
  }
  return 1;
}

namespace
{
auto analyzeInlineCallTemps(Expr& expression) -> int
{
  auto* callee = expression.function;
  std::vector<Symbol const*> locals;
  if (callee->body)
  {
    collectLocalSymbols(*callee->body, locals);
  }

  // Parameters, locals and the result are all mapped to temporaries of the
  // caller for the whole expansion.
  int overhead = (callee->returnType.isVoid() ? 0 : 1);
  for (auto const& parameter : callee->parameters)
  {
    overhead += (parameter.type.size() + 1) / 2;
  }
  for (auto const* symbol : locals)
  {
    overhead += inlineSlotCount(*symbol);
  }

  int usage = 0;
  for (auto& argument : expression.arguments)
  {
    usage = std::max(usage, analyzeExpressionTemps(*argument));
  }
  if (callee->body)
  {
    usage = std::max(usage, analyzeStatementTemps(*callee->body));
  }
  return overhead + usage;
}

auto analyzeCallTemps(Expr& expression) -> int
{
  if (!expression.builtin && expression.function != nullptr
      && expression.function->annotation == FunctionAnnotation::Inline)
  {
    return analyzeInlineCallTemps(expression);
  }

  int result = 0;
  for (std::size_t i = 0; i < expression.arguments.size(); ++i)
  {
    auto const usage = analyzeExpressionTemps(*expression.arguments[i]);
    result = std::max(result, static_cast<int>(i) + usage);
  }

  return result;
}
} // namespace

// Mirrors emitBuildStructLiteral: the destination slots are already live, so
// this counts only the temporaries allocated while filling each field.
namespace
{
auto analyzeBuildStructTemps(Expr& literal) -> int
{
  auto const info = literal.type.structure();
  if (info == nullptr)
  {
    return 0;
  }
  if (literal.isAggregateConstant)
  {
    return 1; // source pointer temporary for the ROM copy
  }

  int result = 1;
  for (std::size_t i = 0; i < info->fields.size(); ++i)
  {
    auto const& field = info->fields[i];
    auto& value = *literal.arguments[i];
    int fieldUsage = 0;
    if (field.type.isStruct())
    {
      auto const nestedSlots = (field.type.size() + 1) / 2;
      fieldUsage = nestedSlots + 1
                   + (value.kind == ExprKind::StructLiteral
                          ? analyzeBuildStructTemps(value)
                          : analyzeExpressionTemps(value));
    }
    else
    {
      // emitWriteValueAtTemp and emitCopyBytesFromExpr each allocate one temp
      // while the argument is evaluated.
      fieldUsage = 1 + analyzeExpressionTemps(value);
    }
    result = std::max(result, fieldUsage);
  }
  return result;
}
} // namespace

auto analyzeExpressionTemps(Expr& expression) -> int
{
  switch (expression.kind)
  {
  case ExprKind::IntLiteral:
  case ExprKind::StringLiteral:
  case ExprKind::ArrayLiteral:
  case ExprKind::Identifier:
    return 0;

  case ExprKind::StructLiteral:
  {
    auto const slots = (expression.type.size() + 1) / 2;
    return slots + 1 + analyzeBuildStructTemps(expression);
  }

  case ExprKind::FieldAccess:
  {
    auto const direct
        = expression.lhs->kind == ExprKind::Identifier
          && expression.lhs->symbol != nullptr
          && expression.op != TokenKind::Arrow
          && !expression.lhs->type.isPointer()
          && expression.lhs->symbol->storage != Storage::Constant
          && (expression.lhs->symbol->address >= 0
              || expression.lhs->symbol->storage == Storage::StackParameter
              || expression.lhs->symbol->storage == Storage::StackLocal);
    return direct ? 0 : 1 + analyzeExpressionTemps(*expression.lhs);
  }

  case ExprKind::Unary:
    return analyzeExpressionTemps(*expression.lhs);

  case ExprKind::Index:
  {
    auto const base = analyzeExpressionTemps(*expression.lhs);
    auto const index = analyzeExpressionTemps(*expression.rhs);
    // Base pointer, index and scaled index each occupy a temporary.
    return std::max({1 + base, 2 + index, 3});
  }

  case ExprKind::Cast:
    return analyzeExpressionTemps(*expression.lhs);

  case ExprKind::IncDec:
    return std::max(3, 1 + analyzeExpressionTemps(*expression.lhs));

  case ExprKind::Binary:
  {
    auto const left = analyzeExpressionTemps(*expression.lhs);
    auto const right = analyzeExpressionTemps(*expression.rhs);
    if (expression.op == TokenKind::LogicalAnd
        || expression.op == TokenKind::LogicalOr)
    {
      // The result temporary is live while both operands are evaluated.
      return std::max({1 + left, 1 + right, 1});
    }
    return std::max({1 + left, 2 + right, 2});
  }

  case ExprKind::Ternary:
  {
    auto const condition = analyzeExpressionTemps(*expression.lhs);
    auto const thenUsage = analyzeExpressionTemps(*expression.rhs);
    auto const elseUsage = analyzeExpressionTemps(*expression.third);
    // The result temporary is live while the condition and both branches run.
    return std::max({1 + condition, 1 + thenUsage, 1 + elseUsage, 1});
  }

  case ExprKind::Assign:
  {
    auto const rhs = analyzeExpressionTemps(*expression.rhs);
    auto const lhs = analyzeExpressionTemps(*expression.lhs);
    if (expression.type.isStruct())
    {
      // Destination pointer stays live while the source is copied.
      return std::max({1 + lhs, 2 + rhs, 2});
    }
    // The value stays live while the left-hand side address is computed.
    return std::max({rhs, 1 + lhs, 1});
  }

  case ExprKind::Call:
    return analyzeCallTemps(expression);
  }

  return 0;
}

namespace
{
auto analyzeBlockTemps(Stmt& statement) -> int
{
  int result = 0;
  for (auto& child : statement.statements)
  {
    result = std::max(result, analyzeStatementTemps(*child));
  }
  return result;
}
} // namespace

auto analyzeStatementTemps(Stmt& statement) -> int
{
  switch (statement.kind)
  {
  case StmtKind::Block:
    return analyzeBlockTemps(statement);

  case StmtKind::Declaration:
    if (!statement.declaration || !statement.declaration->initializer)
    {
      return 0;
    }
    if (statement.declaration->type.isStruct())
    {
      // emitCopyToSymbol holds a destination pointer; struct expressions
      // provide their source pointer directly in A/B.
      return 1 + analyzeExpressionTemps(*statement.declaration->initializer);
    }
    return analyzeExpressionTemps(*statement.declaration->initializer);

  case StmtKind::Expression:
    return statement.expression ? analyzeExpressionTemps(*statement.expression)
                                : 0;

  case StmtKind::If:
  {
    auto result = analyzeExpressionTemps(*statement.expression);
    result = std::max(result, analyzeStatementTemps(*statement.thenBranch));
    if (statement.elseBranch)
    {
      result = std::max(result, analyzeStatementTemps(*statement.elseBranch));
    }
    return result;
  }

  case StmtKind::While:
  case StmtKind::DoWhile:
  {
    auto result = analyzeExpressionTemps(*statement.expression);
    result = std::max(result, analyzeStatementTemps(*statement.thenBranch));
    return result;
  }

  case StmtKind::For:
  {
    int result = 0;
    if (statement.declaration && statement.declaration->initializer)
    {
      result = std::max(
          result, analyzeExpressionTemps(*statement.declaration->initializer));
    }
    if (statement.expression)
    {
      result = std::max(result, analyzeExpressionTemps(*statement.expression));
    }
    if (statement.secondExpression)
    {
      result = std::max(result,
                        analyzeExpressionTemps(*statement.secondExpression));
    }
    if (statement.thirdExpression)
    {
      result = std::max(result,
                        analyzeExpressionTemps(*statement.thirdExpression));
    }
    result = std::max(result, analyzeStatementTemps(*statement.thenBranch));
    return result;
  }

  case StmtKind::Switch:
  {
    auto result = analyzeExpressionTemps(*statement.expression);
    int bodyUsage = 0;
    for (auto& switchCase : statement.cases)
    {
      for (auto& value : switchCase.values)
      {
        bodyUsage = std::max(bodyUsage, analyzeExpressionTemps(*value));
      }
      for (auto& child : switchCase.body)
      {
        bodyUsage = std::max(bodyUsage, analyzeStatementTemps(*child));
      }
    }
    // The switch value is kept in one temporary for the whole statement.
    return std::max({1 + result, bodyUsage + 1, 1});
  }

  case StmtKind::Return:
    if (!statement.expression)
    {
      return 0;
    }
    if (statement.expression->type.isStruct())
    {
      return 2 + analyzeExpressionTemps(*statement.expression);
    }
    return analyzeExpressionTemps(*statement.expression);

  case StmtKind::Break:
  case StmtKind::Continue:
  case StmtKind::Halt:
  case StmtKind::StaticAssert:
  case StmtKind::Empty:
    return 0;
  }

  return 0;
}

auto analyzeFunctionTemps(FunctionDecl& function) -> int
{
  auto usage = function.body ? analyzeStatementTemps(*function.body) : 0;
  return std::max(usage, 4);
}

auto itemSize(Item const& item) -> int
{
  switch (item.kind)
  {
  case Item::Kind::Bytes:
  case Item::Kind::RawBlock:
    return static_cast<int>(item.bytes.size());
  case Item::Kind::Label:
    return 0;
  case Item::Kind::Jump:
    return 8;
  case Item::Kind::StaticCall:
    return 10;
  case Item::Kind::StackCall:
    return 18;
  }
  return 0;
}

auto hexByte(std::uint8_t value) -> std::string
{
  static constexpr std::array<char, 16> digits {'0', '1', '2', '3', '4', '5',
                                                '6', '7', '8', '9', 'A', 'B',
                                                'C', 'D', 'E', 'F'};
  std::string result;
  result += digits.at(static_cast<std::size_t>(value >> 4));
  result += digits.at(static_cast<std::size_t>(value & 0x0F));
  return result;
}
} // namespace strella::codegen_detail
