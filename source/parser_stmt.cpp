#include "strella/parser.hpp"

#include <utility>
#include <vector>

#include "strella/ast.hpp"
#include "strella/token.hpp"

namespace strella
{

auto Parser::parseStatement() -> StmtPtr
{
  if (match(TokenKind::Semicolon))
  {
    return makeStatement(StmtKind::Empty, peek().location);
  }

  if (check(TokenKind::LBrace))
  {
    return parseBlock();
  }

  if (check(TokenKind::KwConst) || check(TokenKind::KwVar))
  {
    auto const location = peek().location;
    auto statement = makeStatement(StmtKind::Declaration, location);
    statement->declaration = parseVarDeclaration();
    return statement;
  }

  if (check(TokenKind::Identifier) && peek().text == "static_assert")
  {
    return parseStaticAssert();
  }

  if (match(TokenKind::KwIf))
  {
    auto const location = tokens_[position_ - 1].location;
    auto statement = makeStatement(StmtKind::If, location);
    expect(TokenKind::LParen, "expected '(' after 'if'");
    statement->expression = parseExpression();
    expect(TokenKind::RParen, "expected ')' after condition");
    statement->thenBranch = parseStatement();
    if (match(TokenKind::KwElse))
    {
      statement->elseBranch = parseStatement();
    }
    return statement;
  }

  if (match(TokenKind::KwWhile))
  {
    auto const location = tokens_[position_ - 1].location;
    auto statement = makeStatement(StmtKind::While, location);
    expect(TokenKind::LParen, "expected '(' after 'while'");
    statement->expression = parseExpression();
    expect(TokenKind::RParen, "expected ')' after condition");
    statement->thenBranch = parseStatement();
    return statement;
  }

  if (match(TokenKind::KwDo))
  {
    auto const location = tokens_[position_ - 1].location;
    auto statement = makeStatement(StmtKind::DoWhile, location);
    statement->thenBranch = parseStatement();
    expect(TokenKind::KwWhile, "expected 'while' after 'do' body");
    expect(TokenKind::LParen, "expected '(' after 'while'");
    statement->expression = parseExpression();
    expect(TokenKind::RParen, "expected ')' after condition");
    expect(TokenKind::Semicolon, "expected ';' after do-while loop");
    return statement;
  }

  if (match(TokenKind::KwFor))
  {
    auto const location = tokens_[position_ - 1].location;
    auto statement = makeStatement(StmtKind::For, location);
    expect(TokenKind::LParen, "expected '(' after 'for'");

    if (match(TokenKind::Semicolon))
    {
      // no initializer
    }
    else if (check(TokenKind::KwConst) || check(TokenKind::KwVar))
    {
      statement->declaration = parseVarDeclaration();
    }
    else
    {
      statement->expression = parseExpression();
      expect(TokenKind::Semicolon, "expected ';' after for initializer");
    }

    if (!check(TokenKind::Semicolon))
    {
      statement->secondExpression = parseExpression();
    }
    expect(TokenKind::Semicolon, "expected ';' after for condition");

    if (!check(TokenKind::RParen))
    {
      statement->thirdExpression = parseExpression();
    }
    expect(TokenKind::RParen, "expected ')' after for clauses");

    statement->thenBranch = parseStatement();
    return statement;
  }

  if (match(TokenKind::KwSwitch))
  {
    auto const location = tokens_[position_ - 1].location;
    auto statement = makeStatement(StmtKind::Switch, location);
    expect(TokenKind::LParen, "expected '(' after 'switch'");
    statement->expression = parseExpression();
    expect(TokenKind::RParen, "expected ')' after switch value");
    expect(TokenKind::LBrace, "expected '{' after switch value");

    while (!check(TokenKind::RBrace) && !check(TokenKind::End))
    {
      if (check(TokenKind::KwCase) || check(TokenKind::KwDefault))
      {
        parseSwitchCase(statement->cases);
      }
      else
      {
        error("expected 'case' or 'default' inside switch");
      }
    }

    expect(TokenKind::RBrace, "expected '}' after switch");
    return statement;
  }

  if (match(TokenKind::KwBreak))
  {
    auto const location = tokens_[position_ - 1].location;
    expect(TokenKind::Semicolon, "expected ';' after 'break'");
    return makeStatement(StmtKind::Break, location);
  }

  if (match(TokenKind::KwContinue))
  {
    auto const location = tokens_[position_ - 1].location;
    expect(TokenKind::Semicolon, "expected ';' after 'continue'");
    return makeStatement(StmtKind::Continue, location);
  }

  if (match(TokenKind::KwReturn))
  {
    auto const location = tokens_[position_ - 1].location;
    auto statement = makeStatement(StmtKind::Return, location);
    if (!check(TokenKind::Semicolon))
    {
      statement->expression = parseExpression();
    }
    expect(TokenKind::Semicolon, "expected ';' after 'return'");
    return statement;
  }

  if (match(TokenKind::KwHalt))
  {
    auto const location = tokens_[position_ - 1].location;
    expect(TokenKind::Semicolon, "expected ';' after 'halt'");
    return makeStatement(StmtKind::Halt, location);
  }

  auto const location = peek().location;
  auto statement = makeStatement(StmtKind::Expression, location);
  statement->expression = parseExpression();
  expect(TokenKind::Semicolon, "expected ';' after expression");
  return statement;
}

void Parser::parseSwitchCase(std::vector<SwitchCase>& cases)
{
  auto const location = peek().location;

  if (match(TokenKind::KwCase))
  {
    auto value = parseExpression();
    expect(TokenKind::Colon, "expected ':' after case value");

    if (!cases.empty() && !cases.back().isDefault && cases.back().body.empty())
    {
      cases.back().values.push_back(std::move(value));
    }
    else
    {
      SwitchCase switchCase;
      switchCase.location = location;
      switchCase.values.push_back(std::move(value));
      cases.push_back(std::move(switchCase));
    }
  }
  else
  {
    expect(TokenKind::KwDefault, "expected 'case' or 'default'");
    expect(TokenKind::Colon, "expected ':' after 'default'");
    for (auto const& existing : cases)
    {
      if (existing.isDefault)
      {
        errorAt(location, "duplicate 'default' label in switch");
      }
    }

    SwitchCase switchCase;
    switchCase.location = location;
    switchCase.isDefault = true;
    cases.push_back(std::move(switchCase));
  }

  while (!check(TokenKind::KwCase) && !check(TokenKind::KwDefault)
         && !check(TokenKind::RBrace) && !check(TokenKind::End))
  {
    cases.back().body.push_back(parseStatement());
  }
}

} // namespace strella
