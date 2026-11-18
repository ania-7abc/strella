#pragma once

#include <stdexcept>
#include <string>

#include "strella/location.hpp"

namespace strella
{

/**
 * @brief Exception thrown for any user-visible compiler error.
 *
 * The message is already formatted and contains a source location when one
 * is available.
 */
class CompileError : public std::runtime_error
{
public:
  explicit CompileError(const std::string& message)
      : std::runtime_error {message}
  {
  }
};

[[noreturn]] inline auto fail(std::string const& file,
                              SourceLocation const& loc,
                              std::string const& message) -> void
{
  throw CompileError {formatLocation(file, loc) + ": error: " + message};
}

} // namespace strella
