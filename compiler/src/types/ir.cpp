#include "gable/ir.h"

namespace gable::ir {

bool Type::isInteger() const {
  switch (kind_) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
      return true;
    default:
      return false;
  }
}

bool Type::isSigned() const {
  switch (kind_) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
      return true;
    default:
      return false;
  }
}

unsigned Type::bitWidth() const {
  switch (kind_) {
    case TypeKind::I8:
    case TypeKind::U8:
      return 8;
    case TypeKind::I16:
    case TypeKind::U16:
      return 16;
    case TypeKind::I32:
    case TypeKind::U32:
      return 32;
    case TypeKind::I64:
    case TypeKind::U64:
      return 64;
    default:
      return 0;
  }
}

std::string Type::name() const {
  switch (kind_) {
    case TypeKind::Error: return "error";
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "bool";
    case TypeKind::Str: return "str";
    case TypeKind::I8: return "i8";
    case TypeKind::I16: return "i16";
    case TypeKind::I32: return "i32";
    case TypeKind::I64: return "i64";
    case TypeKind::U8: return "u8";
    case TypeKind::U16: return "u16";
    case TypeKind::U32: return "u32";
    case TypeKind::U64: return "u64";
  }
  return "error";
}

Type Type::fromSourceName(std::string_view name) {
  if (name == "i8") return Type::i8();
  if (name == "i16") return Type::i16();
  if (name == "i32") return Type::i32();
  if (name == "i64") return Type::i64();
  if (name == "u8") return Type::u8();
  if (name == "u16") return Type::u16();
  if (name == "u32") return Type::u32();
  if (name == "u64") return Type::u64();
  if (name == "bool") return Type::boolean();
  if (name == "str") return Type::str();
  if (name == "void") return Type::voidType();
  return Type::error();
}

bool Type::isKnownSourceName(std::string_view name) {
  return fromSourceName(name).kind() != TypeKind::Error;
}

}  // namespace gable::ir
