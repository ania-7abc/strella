#pragma once

#include <cstddef>
#include <string>

namespace strella
{

/**
 * @brief A position in a source file.
 *
 * Lines and columns are 1-based. Columns are counted in Unicode code points
 * so that diagnostics line up for sources containing Cyrillic text.
 */
struct SourceLocation
{
  std::size_t line = 1;
  std::size_t column = 1;
};

/**
 * @brief Formats a source location as `file:line:column`.
 */
inline auto formatLocation(std::string const& file,
                           SourceLocation const& loc) -> std::string
{
  return file + ':' + std::to_string(loc.line) + ':'
         + std::to_string(loc.column);
}

} // namespace strella
