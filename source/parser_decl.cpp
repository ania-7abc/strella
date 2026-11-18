#include "strella/parser.hpp"

#include <memory>
#include <utility>

#include "strella/ast.hpp"
#include "strella/token.hpp"
#include "strella/type.hpp"

namespace strella
{

auto Parser::parseType() -> Type
{
  if (!isTypeStart())
  {
    error("expected a type");
  }

  auto const& token = advance();
  Type type;
  auto const alias = aliases_.find(token.text);
  auto const structure = structs_.find(token.text);
  if (alias != aliases_.end())
  {
    type = alias->second;
  }
  else if (structure != structs_.end())
  {
    type = Type::structure(structure->second);
  }
  else if (token.text == "byte")
  {
    type = Type::make(TypeKind::Byte);
  }
  else if (token.text == "sbyte")
  {
    type = Type::make(TypeKind::SByte);
  }
  else if (token.text == "word")
  {
    type = Type::make(TypeKind::Word);
  }
  else if (token.text == "sword")
  {
    type = Type::make(TypeKind::Sword);
  }
  else if (token.text == "bool")
  {
    type = Type::make(TypeKind::Bool);
  }
  else if (token.text == "auto")
  {
    type = Type::make(TypeKind::Auto);
  }
  else if (token.text == "ptr")
  {
    if (match(TokenKind::Less))
    {
      auto pointee = parseType();
      if (pointee.isAuto())
      {
        errorAt(token.location,
                "auto cannot be used as a pointer element type");
      }
      expect(TokenKind::Greater, "expected '>' after pointer element type");
      type = Type::pointerTo(std::move(pointee));
    }
    else
    {
      type = Type::make(TypeKind::Ptr);
    }
  }

  if (match(TokenKind::LBracket))
  {
    if (check(TokenKind::RBracket))
    {
      error("array types require a size; 'T[]' is not supported");
    }
    auto const& length
        = expect(TokenKind::IntLiteral, "expected an array length");
    expect(TokenKind::RBracket, "expected ']' after array length");
    if (check(TokenKind::LBracket))
    {
      error("multi-dimensional arrays are not supported");
    }
    if (length.intValue == 0)
    {
      errorAt(length.location, "array length must be positive");
    }
    if (type.isAuto())
    {
      errorAt(token.location, "auto cannot be used as an array element type");
    }
    type = Type::arrayOf(std::move(type), static_cast<int>(length.intValue));
  }

  return type;
}

auto Parser::parsePreimage() -> Preimage
{
  expect(TokenKind::KwPreimage, "expected 'preimage'");
  expect(TokenKind::Colon, "expected ':' after 'preimage'");

  Preimage preimage;
  preimage.present = true;

  if (check(TokenKind::StringLiteral))
  {
    auto const& path = advance();
    preimage.sourcePath = path.text;
  }
  else
  {
    error("expected a preimage file path");
  }

  expect(TokenKind::Semicolon, "expected ';' after preimage");
  return preimage;
}

auto Parser::parseTypeAlias() -> void
{
  expect(TokenKind::Identifier, "expected 'type'");
  auto const& name = expect(TokenKind::Identifier, "expected a type name");
  expect(TokenKind::Assign, "expected '=' after the type name");
  auto const type = parseType();
  expect(TokenKind::Semicolon, "expected ';' after a type alias");

  if (name.text == "byte" || name.text == "sbyte" || name.text == "word"
      || name.text == "sword" || name.text == "bool" || name.text == "ptr"
      || name.text == "auto")
  {
    errorAt(name.location, "cannot redefine a built-in type name");
  }
  if (aliases_.count(name.text) != 0)
  {
    errorAt(name.location, "duplicate type alias '" + name.text + "'");
  }
  aliases_.emplace(name.text, type);
}

auto Parser::parseStruct() -> std::shared_ptr<StructInfo>
{
  expect(TokenKind::KwStruct, "expected 'struct'");
  auto const& name = expect(TokenKind::Identifier, "expected a struct name");

  if (name.text == "byte" || name.text == "sbyte" || name.text == "word"
      || name.text == "sword" || name.text == "bool" || name.text == "ptr"
      || name.text == "auto" || aliases_.count(name.text) != 0
      || structs_.count(name.text) != 0)
  {
    errorAt(name.location, "duplicate type name '" + name.text + "'");
  }

  auto info = std::make_shared<StructInfo>();
  info->name = name.text;
  structs_.emplace(name.text, info);

  expect(TokenKind::LBrace, "expected '{' after struct name");
  while (!check(TokenKind::RBrace) && !check(TokenKind::End))
  {
    if (!isTypeStart())
    {
      error("expected a field type in struct '" + name.text + "'");
    }
    auto const typeLocation = peek().location;
    auto fieldType = parseType();
    auto const& fieldName
        = expect(TokenKind::Identifier, "expected a field name");
    expect(TokenKind::Semicolon, "expected ';' after struct field");

    auto const byValue = [&](Type const& type) -> bool
    {
      if (type.isStruct())
      {
        if (!type.structure() || !type.structure()->complete)
        {
          errorAt(typeLocation, "incomplete struct '" + type.toString()
                                    + "' cannot be a field by value");
        }
        return true;
      }
      return false;
    };

    byValue(fieldType);
    if (fieldType.isArray())
    {
      byValue(fieldType.pointee());
    }
    if (fieldType.isAuto() || fieldType.isVoid())
    {
      errorAt(typeLocation, "invalid field type in struct '" + name.text + "'");
    }

    for (auto const& field : info->fields)
    {
      if (field.name == fieldName.text)
      {
        errorAt(fieldName.location, "duplicate field '" + fieldName.text
                                        + "' in struct '" + name.text + "'");
      }
    }

    auto const size = fieldType.isArray()
                          ? fieldType.arrayLength() * fieldType.pointee().size()
                          : fieldType.size();
    if (size <= 0)
    {
      errorAt(typeLocation, "field '" + fieldName.text + "' has no size");
    }
    if (info->size + size > 255)
    {
      errorAt(fieldName.location, "struct '" + name.text + "' is too large");
    }

    info->fields.push_back(
        StructField {fieldName.text, std::move(fieldType), info->size});
    info->size += size;
  }
  expect(TokenKind::RBrace, "expected '}' after struct fields");
  expect(TokenKind::Semicolon, "expected ';' after struct declaration");

  info->complete = true;
  return info;
}

auto Parser::parseStaticAssert() -> StmtPtr
{
  auto const& keyword
      = expect(TokenKind::Identifier, "expected 'static_assert'");
  auto statement = makeStatement(StmtKind::StaticAssert, keyword.location);
  expect(TokenKind::LParen, "expected '(' after 'static_assert'");
  statement->expression = parseExpression();
  expect(TokenKind::Comma, "expected ',' after static_assert condition");
  auto const& message
      = expect(TokenKind::StringLiteral, "expected a static_assert message");
  statement->message = message.text;
  expect(TokenKind::RParen, "expected ')' after static_assert");
  expect(TokenKind::Semicolon, "expected ';' after static_assert");
  return statement;
}

auto Parser::parseGlobalDeclaration() -> std::unique_ptr<VarDecl>
{
  auto declaration = parseVarDeclaration();
  return declaration;
}

auto Parser::parseVarDeclaration() -> std::unique_ptr<VarDecl>
{
  auto const& keyword = peek();
  auto const isConst = check(TokenKind::KwConst);
  if (!match(TokenKind::KwConst) && !match(TokenKind::KwVar))
  {
    error("expected 'const' or 'var'");
  }

  auto declaration = std::make_unique<VarDecl>();
  declaration->isConst = isConst;
  declaration->location = keyword.location;

  auto const& name = expect(TokenKind::Identifier, "expected a variable name");
  declaration->name = name.text;

  expect(TokenKind::Colon, "expected ':' after variable name");
  declaration->type = parseType();

  if (match(TokenKind::Assign))
  {
    declaration->initializer = parseExpression();
  }

  expect(TokenKind::Semicolon, "expected ';' after variable declaration");
  return declaration;
}

auto Parser::parseFunction(FunctionAnnotation annotation)
    -> std::unique_ptr<FunctionDecl>
{
  auto function = std::make_unique<FunctionDecl>();
  function->annotation = annotation;
  function->location = peek().location;

  auto const& name = expect(TokenKind::Identifier, "expected a function name");
  function->name = name.text;

  expect(TokenKind::LParen, "expected '(' after function name");
  if (!check(TokenKind::RParen))
  {
    while (true)
    {
      Parameter parameter;
      parameter.location = peek().location;
      parameter.type = parseType();
      auto const& parameterName
          = expect(TokenKind::Identifier, "expected a parameter name");
      parameter.name = parameterName.text;
      function->parameters.push_back(std::move(parameter));

      if (!match(TokenKind::Comma))
      {
        break;
      }
    }
  }
  expect(TokenKind::RParen, "expected ')' after function parameters");

  if (isTypeStart())
  {
    function->returnType = parseType();
  }
  else
  {
    function->returnType = Type::make(TypeKind::Void);
  }

  function->body = parseBlock();
  return function;
}

auto Parser::parseBlock() -> StmtPtr
{
  auto const& start = expect(TokenKind::LBrace, "expected '{'");
  auto statement = makeStatement(StmtKind::Block, start.location);
  while (!check(TokenKind::RBrace) && !check(TokenKind::End))
  {
    statement->statements.push_back(parseStatement());
  }
  expect(TokenKind::RBrace, "expected '}'");
  return statement;
}

} // namespace strella
