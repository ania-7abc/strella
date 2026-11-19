#pragma once

#include <cstdint>

namespace strella
{

/**
 * @brief RAM variant of the target machine.
 *
 * The 1k variant allows preimages and does not need display protection.  The
 * 32k variant has less room for startup data: preimages are rejected and
 * display protection is enabled.
 */
enum class Target : std::uint8_t
{
  K1,
  K32,
};

} // namespace strella
