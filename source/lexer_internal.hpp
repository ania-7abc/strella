#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "strella/location.hpp"
#include "strella/token.hpp"

namespace strella::lex_detail
{

auto isIdentifierStart(char c) -> bool;
auto isIdentifierPart(char c) -> bool;
auto keywordKind(std::string const& text) -> TokenKind;
auto decodeUtf8(std::string const& text, std::size_t& index,
                std::string const& file, SourceLocation const& location)
    -> std::uint32_t;
auto cp1251Byte(std::uint32_t codePoint, std::string const& file,
                SourceLocation const& location) -> unsigned char;
auto escapeValue(char c) -> int;
auto parseHexEscape(std::string const& source, std::size_t& pos,
                    std::size_t& line, std::size_t& column,
                    std::string const& file, SourceLocation const& location)
    -> int;

} // namespace strella::lex_detail
