#pragma once

#include <string>

#include "strella/type.hpp"

namespace strella
{

inline auto integerOrBool(Type const& type) -> bool
{
  return type.isInteger() || type.isBool();
}

inline auto conditionType(Type const& type) -> bool
{
  return integerOrBool(type) || type.isPointer();
}

inline auto formatType(Type const& type) -> std::string
{
  return type.toString();
}

} // namespace strella
