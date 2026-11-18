#include "strella/parser.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "strella/ast.hpp"
#include "strella/diagnostics.hpp"
#include "strella/location.hpp"
#include "strella/token.hpp"

namespace strella
{

Parser::Parser(std::vector<Token> tokens, std::string file)
    : tokens_ {std::move(tokens)}, file_ {std::move(file)}
{
}

auto Parser::parse() -> Program
{
  Program program;

  while (!check(TokenKind::End))
  {
    if (check(TokenKind::KwPreimage))
    {
      if (program.preimage.present)
      {
        error("a project can contain only one preimage declaration");
      }
      program.preimage = parsePreimage();
      continue;
    }

    if (check(TokenKind::KwConst) || check(TokenKind::KwVar))
    {
      program.globals.push_back(parseGlobalDeclaration());
      continue;
    }

    if (check(TokenKind::KwStruct))
    {
      program.structs.push_back(parseStruct());
      continue;
    }

    if (check(TokenKind::Identifier) && peek().text == "type")
    {
      parseTypeAlias();
      continue;
    }

    if (check(TokenKind::Identifier) && peek().text == "static_assert")
    {
      program.staticAsserts.push_back(parseStaticAssert());
      continue;
    }

    auto annotation = FunctionAnnotation::None;
    if (match(TokenKind::At))
    {
      auto const& token = expect(TokenKind::Identifier,
                                 "expected 'inline' or 'stack' after '@'");
      if (token.text == "inline")
      {
        annotation = FunctionAnnotation::Inline;
      }
      else if (token.text == "stack")
      {
        annotation = FunctionAnnotation::Stack;
      }
      else
      {
        errorAt(token.location,
                "unknown function annotation '@" + token.text + "'");
      }
    }

    if (match(TokenKind::KwFunc))
    {
      program.functions.push_back(parseFunction(annotation));
      continue;
    }

    if (annotation != FunctionAnnotation::None)
    {
      error("expected 'func' after function annotation");
    }

    error("expected a declaration, function, or preimage");
  }

  return program;
}

auto Parser::peek(std::size_t lookahead) const -> Token const&
{
  auto const index = position_ + lookahead;
  return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
}

auto Parser::check(TokenKind kind) const -> bool { return peek().kind == kind; }

auto Parser::match(TokenKind kind) -> bool
{
  if (!check(kind))
  {
    return false;
  }
  advance();
  return true;
}

auto Parser::advance() -> Token const&
{
  auto const& token = tokens_[position_];
  if (position_ + 1 < tokens_.size())
  {
    ++position_;
  }
  return token;
}

auto Parser::expect(TokenKind kind, std::string const& message) -> Token const&
{
  if (!check(kind))
  {
    error(message + ", got " + tokenKindName(peek().kind));
  }
  return advance();
}

void Parser::error(std::string const& message) const
{
  fail(file_, peek().location, message);
}

void Parser::errorAt(SourceLocation const& loc,
                     std::string const& message) const
{
  fail(file_, loc, message);
}

auto Parser::isTypeStart() const -> bool
{
  if (!check(TokenKind::Identifier) && !check(TokenKind::KwAuto))
  {
    return false;
  }
  auto const& text = peek().text;
  return text == "byte" || text == "sbyte" || text == "word" || text == "sword"
         || text == "bool" || text == "ptr" || text == "auto"
         || aliases_.count(text) != 0 || structs_.count(text) != 0;
}

auto Parser::makeExpression(ExprKind kind, SourceLocation loc) -> ExprPtr
{
  auto expression = std::make_unique<Expr>();
  expression->kind = kind;
  expression->location = loc;
  return expression;
}

auto Parser::makeStatement(StmtKind kind, SourceLocation loc) -> StmtPtr
{
  auto statement = std::make_unique<Stmt>();
  statement->kind = kind;
  statement->location = loc;
  return statement;
}

} // namespace strella
