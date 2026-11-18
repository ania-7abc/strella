#include "strella/parser.hpp"

#include <utility>

#include "strella/ast.hpp"
#include "strella/token.hpp"
#include "strella/type.hpp"

namespace strella
{

auto Parser::parseExpression() -> ExprPtr { return parseAssignment(); }

auto Parser::parseAssignment() -> ExprPtr
{
  auto left = parseTernary();
  if (match(TokenKind::Assign))
  {
    auto const location = tokens_[position_ - 1].location;
    auto expression = makeExpression(ExprKind::Assign, location);
    expression->op = TokenKind::Assign;
    expression->lhs = std::move(left);
    expression->rhs = parseAssignment();
    return expression;
  }
  return left;
}

auto Parser::parseTernary() -> ExprPtr
{
  auto condition = parseLogicalOr();
  if (match(TokenKind::Question))
  {
    auto const location = tokens_[position_ - 1].location;
    auto expression = makeExpression(ExprKind::Ternary, location);
    expression->lhs = std::move(condition);
    expression->rhs = parseExpression();
    expect(TokenKind::Colon, "expected ':' in ternary expression");
    expression->third = parseTernary();
    return expression;
  }
  return condition;
}

auto Parser::parseLogicalOr() -> ExprPtr
{
  auto expression = parseLogicalAnd();
  while (match(TokenKind::LogicalOr))
  {
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = TokenKind::LogicalOr;
    binary->lhs = std::move(expression);
    binary->rhs = parseLogicalAnd();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseLogicalAnd() -> ExprPtr
{
  auto expression = parseBitwiseOr();
  while (match(TokenKind::LogicalAnd))
  {
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = TokenKind::LogicalAnd;
    binary->lhs = std::move(expression);
    binary->rhs = parseBitwiseOr();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseBitwiseOr() -> ExprPtr
{
  auto expression = parseBitwiseXor();
  while (match(TokenKind::Pipe))
  {
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = TokenKind::Pipe;
    binary->lhs = std::move(expression);
    binary->rhs = parseBitwiseXor();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseBitwiseXor() -> ExprPtr
{
  auto expression = parseBitwiseAnd();
  while (match(TokenKind::Caret))
  {
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = TokenKind::Caret;
    binary->lhs = std::move(expression);
    binary->rhs = parseBitwiseAnd();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseBitwiseAnd() -> ExprPtr
{
  auto expression = parseEquality();
  while (match(TokenKind::Amp))
  {
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = TokenKind::Amp;
    binary->lhs = std::move(expression);
    binary->rhs = parseEquality();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseEquality() -> ExprPtr
{
  auto expression = parseRelational();
  while (check(TokenKind::Equal) || check(TokenKind::NotEqual))
  {
    auto const op = advance().kind;
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = op;
    binary->lhs = std::move(expression);
    binary->rhs = parseRelational();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseRelational() -> ExprPtr
{
  auto expression = parseShift();
  while (check(TokenKind::Less) || check(TokenKind::LessEqual)
         || check(TokenKind::Greater) || check(TokenKind::GreaterEqual))
  {
    auto const op = advance().kind;
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = op;
    binary->lhs = std::move(expression);
    binary->rhs = parseShift();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseShift() -> ExprPtr
{
  auto expression = parseAdditive();
  while (check(TokenKind::ShiftLeft) || check(TokenKind::ShiftRight))
  {
    auto const op = advance().kind;
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = op;
    binary->lhs = std::move(expression);
    binary->rhs = parseAdditive();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseAdditive() -> ExprPtr
{
  auto expression = parseUnary();
  while (check(TokenKind::Plus) || check(TokenKind::Minus))
  {
    auto const op = advance().kind;
    auto const location = tokens_[position_ - 1].location;
    auto binary = makeExpression(ExprKind::Binary, location);
    binary->op = op;
    binary->lhs = std::move(expression);
    binary->rhs = parseUnary();
    expression = std::move(binary);
  }
  return expression;
}

auto Parser::parseUnary() -> ExprPtr
{
  if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus))
  {
    auto const op = advance().kind;
    auto const location = tokens_[position_ - 1].location;
    auto expression = makeExpression(ExprKind::IncDec, location);
    expression->op
        = op == TokenKind::PlusPlus ? TokenKind::Plus : TokenKind::Minus;
    expression->prefix = true;
    expression->lhs = parseUnary();
    return parseCastSuffix(std::move(expression));
  }

  if (check(TokenKind::Bang) || check(TokenKind::Tilde)
      || check(TokenKind::Plus) || check(TokenKind::Minus)
      || check(TokenKind::Amp) || check(TokenKind::Star))
  {
    auto const op = advance().kind;
    auto const location = tokens_[position_ - 1].location;
    auto expression = makeExpression(ExprKind::Unary, location);
    expression->op = op;
    expression->lhs = parseUnary();
    return parseCastSuffix(std::move(expression));
  }

  return parseCastSuffix(parsePostfix());
}

auto Parser::parseCastSuffix(ExprPtr expression) -> ExprPtr
{
  while (match(TokenKind::KwAs))
  {
    auto const location = tokens_[position_ - 1].location;
    auto cast = makeExpression(ExprKind::Cast, location);
    cast->lhs = std::move(expression);
    cast->targetType = parseType();
    expression = std::move(cast);
  }
  return expression;
}

auto Parser::parsePostfix() -> ExprPtr
{
  auto expression = parsePrimary();

  while (true)
  {
    if (check(TokenKind::LParen))
    {
      if (expression->kind != ExprKind::Identifier)
      {
        error("only named functions can be called");
      }

      auto const location = peek().location;
      auto call = makeExpression(ExprKind::Call, location);
      call->name = expression->name;
      advance();

      if (!check(TokenKind::RParen))
      {
        while (true)
        {
          call->arguments.push_back(parseExpression());
          if (!match(TokenKind::Comma))
          {
            break;
          }
        }
      }

      expect(TokenKind::RParen, "expected ')' after call arguments");
      expression = std::move(call);
      continue;
    }

    if (match(TokenKind::LBracket))
    {
      auto const location = tokens_[position_ - 1].location;
      auto index = makeExpression(ExprKind::Index, location);
      index->lhs = std::move(expression);
      index->rhs = parseExpression();
      expect(TokenKind::RBracket, "expected ']' after index");
      expression = std::move(index);
      continue;
    }

    if (check(TokenKind::Dot) || check(TokenKind::Arrow))
    {
      auto const op = advance().kind;
      auto const location = tokens_[position_ - 1].location;
      auto field = makeExpression(ExprKind::FieldAccess, location);
      field->op = op;
      field->lhs = std::move(expression);
      auto const& name = expect(TokenKind::Identifier, "expected a field name");
      field->name = name.text;
      expression = std::move(field);
      continue;
    }

    if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus))
    {
      auto const op = advance().kind;
      auto const location = tokens_[position_ - 1].location;
      auto increment = makeExpression(ExprKind::IncDec, location);
      increment->op
          = op == TokenKind::PlusPlus ? TokenKind::Plus : TokenKind::Minus;
      increment->prefix = false;
      increment->lhs = std::move(expression);
      expression = std::move(increment);
      continue;
    }

    break;
  }

  return expression;
}

auto Parser::parsePrimary() -> ExprPtr
{
  if (check(TokenKind::Identifier) && peek().text == "sizeof"
      && peek(1).kind == TokenKind::LParen)
  {
    auto const& keyword = advance();
    auto call = makeExpression(ExprKind::Call, keyword.location);
    call->name = "sizeof";
    advance();
    call->targetType = parseType();
    expect(TokenKind::RParen, "expected ')' after sizeof type");
    return call;
  }

  if (match(TokenKind::LBracket))
  {
    auto const location = tokens_[position_ - 1].location;
    auto literal = makeExpression(ExprKind::ArrayLiteral, location);
    if (!check(TokenKind::RBracket))
    {
      while (true)
      {
        literal->arguments.push_back(parseExpression());
        if (!match(TokenKind::Comma) || check(TokenKind::RBracket))
        {
          break;
        }
      }
    }
    expect(TokenKind::RBracket, "expected ']' after array literal");
    return literal;
  }

  if (match(TokenKind::LBrace))
  {
    auto const location = tokens_[position_ - 1].location;
    auto literal = makeExpression(ExprKind::StructLiteral, location);
    if (!check(TokenKind::RBrace))
    {
      while (true)
      {
        literal->arguments.push_back(parseExpression());
        if (!match(TokenKind::Comma) || check(TokenKind::RBrace))
        {
          break;
        }
      }
    }
    expect(TokenKind::RBrace, "expected '}' after struct literal");
    return literal;
  }

  if (check(TokenKind::IntLiteral))
  {
    auto const& token = advance();
    auto expression = makeExpression(ExprKind::IntLiteral, token.location);
    expression->intValue = token.intValue;
    return expression;
  }

  if (check(TokenKind::CharLiteral))
  {
    auto const& token = advance();
    auto expression = makeExpression(ExprKind::IntLiteral, token.location);
    expression->intValue = token.intValue;
    return expression;
  }

  if (check(TokenKind::StringLiteral))
  {
    auto const& token = advance();
    auto expression = makeExpression(ExprKind::StringLiteral, token.location);
    expression->stringValue = token.text;
    return expression;
  }

  if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse))
  {
    auto const& token = tokens_[position_ - 1];
    auto expression = makeExpression(ExprKind::IntLiteral, token.location);
    expression->intValue = token.kind == TokenKind::KwTrue ? 1 : 0;
    expression->type = Type::make(TypeKind::Bool);
    return expression;
  }

  if (check(TokenKind::Identifier))
  {
    auto const& token = advance();
    auto expression = makeExpression(ExprKind::Identifier, token.location);
    expression->name = token.text;
    return expression;
  }

  if (match(TokenKind::LParen))
  {
    auto expression = parseExpression();
    expect(TokenKind::RParen, "expected ')' after expression");
    return expression;
  }

  error("expected an expression, got " + tokenKindName(peek().kind));
}

} // namespace strella
