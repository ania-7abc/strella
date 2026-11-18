#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "strella/ast.hpp"
#include "strella/location.hpp"

namespace strella
{

/**
 * @brief Loads a preimage.
 *
 * The image must contain exactly 32 or 64 bytes.  Plain binary files are
 * supported directly.  PNG files are decoded through libpng when it is
 * available at build time; the resulting image must be 16x16 pixels, matching
 * the display memory layout.
 */
auto loadPreimage(std::string const& path, std::string const& file,
                  SourceLocation const& location) -> Preimage;

} // namespace strella
