#include "strella/optimizer.hpp"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "strella/ast.hpp"

namespace strella
{
namespace
{

class Optimizer
{
public:
  auto run(Program& program) -> int
  {
    for (auto& global : program.globals)
    {
      optimizeExpression(global->initializer);
    }

    for (auto& assertion : program.staticAsserts)
    {
      optimizeStatement(assertion);
    }

    for (auto& function : program.functions)
    {
      optimizeStatement(function->body);
    }
    return changes_;
  }

private:
  int changes_ = 0;

  [[nodiscard]] static auto isTerminator(Stmt const& statement) -> bool
  {
    switch (statement.kind)
    {
    case StmtKind::Return:
    case StmtKind::Halt:
    case StmtKind::Break:
    case StmtKind::Continue:
      return true;
    default:
      return false;
    }
  }

  [[nodiscard]] static auto isConstantFalse(Expr const* expression) -> bool
  {
    return expression != nullptr && expression->isConstant
           && expression->constantValue == 0;
  }

  // Folds every constant subexpression that is not already a plain numeric
  // literal.  Semantic analysis has already marked such expressions with
  // `isConstant` and `constantValue`; rewriting them into integer literals
  // removes the need to evaluate them at runtime.
  void optimizeExpression(ExprPtr& expression)
  {
    if (!expression)
    {
      return;
    }

    auto& node = *expression;

    switch (node.kind)
    {
    case ExprKind::Unary:
    case ExprKind::Cast:
    case ExprKind::IncDec:
      optimizeExpression(node.lhs);
      break;

    case ExprKind::Index:
    case ExprKind::Binary:
      optimizeExpression(node.lhs);
      optimizeExpression(node.rhs);
      break;

    case ExprKind::Ternary:
      optimizeExpression(node.lhs);
      optimizeExpression(node.rhs);
      optimizeExpression(node.third);
      break;

    case ExprKind::Call:
    case ExprKind::ArrayLiteral:
      for (auto& argument : node.arguments)
      {
        optimizeExpression(argument);
      }
      break;

    case ExprKind::Assign:
      optimizeExpression(node.lhs);
      optimizeExpression(node.rhs);
      break;

    default:
      break;
    }

    // String literals stay as they are: their value is the address of the
    // string data, which the code generator resolves itself.
    if (node.isConstant && node.kind != ExprKind::IntLiteral
        && node.kind != ExprKind::StringLiteral
        && node.kind != ExprKind::ArrayLiteral)
    {
      auto const type = node.type;
      auto const location = node.location;
      auto const value = node.constantValue;

      expression = std::make_unique<Expr>();
      expression->kind = ExprKind::IntLiteral;
      expression->location = location;
      expression->type = type;
      expression->intValue = value;
      expression->isConstant = true;
      expression->constantValue = value;
      ++changes_;
    }
  }

  void optimizeStatement(StmtPtr& statement)
  {
    if (!statement)
    {
      return;
    }

    switch (statement->kind)
    {
    case StmtKind::Block:
      optimizeBlock(*statement);
      break;

    case StmtKind::Declaration:
      if (statement->declaration)
      {
        optimizeExpression(statement->declaration->initializer);
      }
      break;

    case StmtKind::Expression:
      optimizeExpression(statement->expression);
      break;

    case StmtKind::If:
      optimizeExpression(statement->expression);
      optimizeIf(statement);
      break;

    case StmtKind::While:
      optimizeExpression(statement->expression);
      optimizeStatement(statement->thenBranch);
      if (isConstantFalse(statement->expression.get()))
      {
        statement.reset();
        ++changes_;
      }
      break;

    case StmtKind::DoWhile:
      optimizeStatement(statement->thenBranch);
      optimizeExpression(statement->expression);
      if (isConstantFalse(statement->expression.get()))
      {
        auto body = std::move(statement->thenBranch);
        ++changes_;
        if (body)
        {
          optimizeStatement(body);
          statement = std::move(body);
        }
        else
        {
          statement.reset();
        }
      }
      break;

    case StmtKind::For:
      if (statement->declaration)
      {
        optimizeExpression(statement->declaration->initializer);
      }
      optimizeExpression(statement->expression);
      optimizeExpression(statement->secondExpression);
      optimizeExpression(statement->thirdExpression);
      // The initializer still has to run when the condition is false, so the
      // loop is left intact.
      optimizeStatement(statement->thenBranch);
      break;

    case StmtKind::Switch:
      optimizeExpression(statement->expression);
      for (auto& switchCase : statement->cases)
      {
        for (auto& value : switchCase.values)
        {
          optimizeExpression(value);
        }
        optimizeBlockBody(switchCase.body);
      }
      break;

    case StmtKind::Return:
    case StmtKind::StaticAssert:
      optimizeExpression(statement->expression);
      break;

    default:
      break;
    }
  }

  void optimizeIf(StmtPtr& statement)
  {
    auto& branch = *statement;
    optimizeStatement(branch.thenBranch);
    optimizeStatement(branch.elseBranch);

    if (branch.expression != nullptr && branch.expression->isConstant)
    {
      StmtPtr chosen = branch.expression->constantValue != 0
                           ? std::move(branch.thenBranch)
                           : std::move(branch.elseBranch);
      ++changes_;
      if (chosen)
      {
        optimizeStatement(chosen);
        statement = std::move(chosen);
      }
      else
      {
        statement.reset();
      }
    }
  }

  void optimizeBlock(Stmt& block) { optimizeBlockBody(block.statements); }

  void optimizeBlockBody(std::vector<StmtPtr>& statements)
  {
    for (std::size_t i = 0; i < statements.size();)
    {
      optimizeStatement(statements[i]);
      if (!statements[i])
      {
        statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }

      if (isTerminator(*statements[i]))
      {
        auto const removed = statements.size() - i - 1;
        if (removed > 0)
        {
          statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(i)
                               + 1,
                           statements.end());
          changes_ += static_cast<int>(removed);
        }
        break;
      }
      ++i;
    }
  }
};

} // namespace

auto optimizeProgram(Program& program) -> int
{
  Optimizer optimizer;
  return optimizer.run(program);
}

} // namespace strella
