#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace strella
{

class Type;
struct StructInfo;

/**
 * @brief Kinds of values understood by the language.
 *
 * A pointer type may or may not name its pointee. A bare `ptr` is an untyped
 * pointer; `ptr<T>` names the element type. `T[N]` is represented as a
 * pointer to `T` with an array length attached; it is interchangeable with
 * `ptr<T>` (see STRELLA.md).
 */
enum class TypeKind : std::uint8_t
{
  Void,
  Byte,
  SByte,
  Word,
  Sword,
  Bool,
  Ptr,
  Struct,
  Auto
};

class Type
{
public:
  Type() = default;

  static auto make(TypeKind kind) -> Type
  {
    Type type;
    type.kind_ = kind;
    return type;
  }

  static auto pointerTo(Type pointee) -> Type
  {
    Type type;
    type.kind_ = TypeKind::Ptr;
    type.pointee_ = std::make_shared<Type>(std::move(pointee));
    return type;
  }

  static auto arrayOf(Type element, int length) -> Type
  {
    Type type = pointerTo(std::move(element));
    type.arrayLength_ = length;
    return type;
  }

  static auto structure(std::shared_ptr<StructInfo> info) -> Type
  {
    Type type;
    type.kind_ = TypeKind::Struct;
    type.struct_ = std::move(info);
    return type;
  }

  [[nodiscard]] auto kind() const -> TypeKind { return kind_; }

  [[nodiscard]] auto structure() const -> std::shared_ptr<StructInfo> const&
  {
    return struct_;
  }

  [[nodiscard]] auto hasPointee() const -> bool
  {
    return kind_ == TypeKind::Ptr && pointee_ != nullptr;
  }

  [[nodiscard]] auto pointee() const -> Type const&
  {
    return pointee_ ? *pointee_ : voidType();
  }

  [[nodiscard]] auto isVoid() const -> bool { return kind_ == TypeKind::Void; }

  [[nodiscard]] auto isBool() const -> bool { return kind_ == TypeKind::Bool; }

  [[nodiscard]] auto isAuto() const -> bool { return kind_ == TypeKind::Auto; }

  [[nodiscard]] auto isPointer() const -> bool
  {
    return kind_ == TypeKind::Ptr;
  }

  [[nodiscard]] auto isGenericPointer() const -> bool
  {
    return kind_ == TypeKind::Ptr && pointee_ == nullptr;
  }

  [[nodiscard]] auto isStruct() const -> bool
  {
    return kind_ == TypeKind::Struct;
  }

  [[nodiscard]] auto isArray() const -> bool
  {
    return kind_ == TypeKind::Ptr && arrayLength_ > 0;
  }

  [[nodiscard]] auto arrayLength() const -> int { return arrayLength_; }

  [[nodiscard]] auto isWide() const -> bool
  {
    return kind_ == TypeKind::Word || kind_ == TypeKind::Sword
           || kind_ == TypeKind::Ptr;
  }

  [[nodiscard]] auto isSigned() const -> bool
  {
    return kind_ == TypeKind::SByte || kind_ == TypeKind::Sword;
  }

  [[nodiscard]] auto isInteger() const -> bool
  {
    return kind_ == TypeKind::Byte || kind_ == TypeKind::SByte
           || kind_ == TypeKind::Word || kind_ == TypeKind::Sword
           || kind_ == TypeKind::Bool;
  }

  [[nodiscard]] auto size() const -> int;

  [[nodiscard]] auto toString() const -> std::string;

  [[nodiscard]] static auto voidType() -> Type const&
  {
    static Type const value = make(TypeKind::Void);
    return value;
  }

private:
  TypeKind kind_ = TypeKind::Void;
  std::shared_ptr<Type> pointee_;
  std::shared_ptr<StructInfo> struct_;
  int arrayLength_ = 0;
};

/**
 * @brief A packed struct field.  Fields are placed without alignment padding.
 */
struct StructField
{
  std::string name;
  Type type;
  int offset = 0;
};

/**
 * @brief Nominal type identity for a struct declaration.
 */
struct StructInfo
{
  std::string name;
  std::vector<StructField> fields;
  int size = 0;
  bool complete = false;

  [[nodiscard]] auto findField(std::string const& fieldName) const -> int
  {
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
      if (fields[i].name == fieldName)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }
};

inline auto Type::size() const -> int
{
  switch (kind_)
  {
  case TypeKind::Void:
  case TypeKind::Auto:
    return 0;
  case TypeKind::Word:
  case TypeKind::Sword:
  case TypeKind::Ptr:
    return 2;
  case TypeKind::Struct:
    return struct_ ? struct_->size : 0;
  default:
    return 1;
  }
}

inline auto Type::toString() const -> std::string
{
  switch (kind_)
  {
  case TypeKind::Void:
    return "void";
  case TypeKind::Byte:
    return "byte";
  case TypeKind::SByte:
    return "sbyte";
  case TypeKind::Word:
    return "word";
  case TypeKind::Sword:
    return "sword";
  case TypeKind::Bool:
    return "bool";
  case TypeKind::Auto:
    return "auto";
  case TypeKind::Ptr:
    if (arrayLength_ > 0)
    {
      return pointee().toString() + '[' + std::to_string(arrayLength_) + ']';
    }
    if (pointee_ == nullptr)
    {
      return "ptr";
    }
    return "ptr<" + pointee().toString() + '>';
  case TypeKind::Struct:
    return struct_ ? struct_->name : "struct?";
  }
  return "?";
}

/**
 * @brief Removes the array length from a type.
 *
 * Arrays are interchangeable with pointers to their element type, so a value
 * of array type behaves like the corresponding pointer whenever it is stored,
 * passed or returned.
 */
inline auto decayed(Type type) -> Type
{
  if (type.isArray())
  {
    return Type::pointerTo(type.pointee());
  }
  return type;
}

inline auto sameType(Type const& lhs, Type const& rhs) -> bool
{
  if (lhs.kind() != rhs.kind())
  {
    return false;
  }
  if (lhs.kind() == TypeKind::Ptr)
  {
    if (lhs.hasPointee() != rhs.hasPointee())
    {
      return false;
    }
    return !lhs.hasPointee() || sameType(lhs.pointee(), rhs.pointee());
  }
  if (lhs.kind() == TypeKind::Struct)
  {
    return lhs.structure() == rhs.structure();
  }
  return true;
}

/**
 * @brief Returns true when a value of type `source` converts implicitly to
 *        `dest`.
 *
 * Implicit conversions are value-preserving widening plus conversion of any
 * value to `bool` (nonzero/non-null means true). Narrowing and signedness
 * changes require an explicit `as` cast.
 */
inline auto assignable(Type const& dest, Type const& source) -> bool
{
  if (sameType(dest, source))
  {
    return true;
  }

  if (source.isVoid() || dest.isVoid() || source.isAuto() || dest.isAuto())
  {
    return false;
  }

  // Any value converts implicitly to bool.
  if (dest.isBool())
  {
    return true;
  }

  // bool is a 0/1 value, so widening it is always safe.
  if (source.isBool())
  {
    return dest.isInteger();
  }

  // Value-preserving integer widening.
  if (source.kind() == TypeKind::Byte)
  {
    return dest.kind() == TypeKind::Word || dest.kind() == TypeKind::Sword;
  }
  if (source.kind() == TypeKind::SByte)
  {
    return dest.kind() == TypeKind::Sword;
  }

  return false;
}

/**
 * @brief Returns true when an explicit `as` cast from `source` to `dest` is
 *        allowed.
 *
 * Integer types convert to one another, pointers convert to one another, and
 * pointers convert to and from word-sized integers.
 */
inline auto explicitlyConvertible(Type const& dest, Type const& source) -> bool
{
  if (sameType(dest, source))
  {
    return true;
  }

  if (dest.isInteger() && source.isInteger())
  {
    return true;
  }

  if (dest.isPointer() && source.isPointer())
  {
    return true;
  }

  if (dest.isPointer() && source.isInteger())
  {
    return source.size() == 2;
  }

  if (dest.isInteger() && source.isPointer())
  {
    return dest.size() == 2;
  }

  return false;
}

/**
 * @brief Computes the type used for a binary arithmetic/bitwise operation.
 *
 * The wider operand wins. When both operands have the same width, a signed
 * operand makes the result signed. `bool` behaves as an unsigned byte.
 */
inline auto commonType(Type const& lhs, Type const& rhs) -> Type
{
  if (lhs.isWide() || rhs.isWide())
  {
    if (lhs.isSigned() || rhs.isSigned())
    {
      return Type::make(TypeKind::Sword);
    }
    return Type::make(TypeKind::Word);
  }

  if (lhs.isSigned() || rhs.isSigned())
  {
    return Type::make(TypeKind::SByte);
  }
  return Type::make(TypeKind::Byte);
}

} // namespace strella
