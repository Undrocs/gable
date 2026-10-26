#include "gable/ir.h"

#include <algorithm>

namespace gable::ir {
namespace {

struct ScalarName {
  std::string_view name;
  TypeKind kind;
};

constexpr ScalarName kScalarNames[] = {
    {"i8", TypeKind::I8},   {"i16", TypeKind::I16}, {"i32", TypeKind::I32},
    {"i64", TypeKind::I64}, {"u8", TypeKind::U8},   {"u16", TypeKind::U16},
    {"u32", TypeKind::U32}, {"u64", TypeKind::U64}, {"bool", TypeKind::Bool},
    {"str", TypeKind::Str}, {"void", TypeKind::Void},
};

}  // namespace

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

Type Type::fromSourceName(std::string_view name) {
  for (const ScalarName& entry : kScalarNames) {
    if (entry.name == name) {
      return Type(entry.kind);
    }
  }
  return Type::error();
}

bool Type::isScalarSourceName(std::string_view name) {
  return fromSourceName(name).kind() != TypeKind::Error;
}

Type TypeTable::array(Type element, std::uint64_t length) {
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].element == element && entries_[i].length == length) {
      return Type::arrayType(static_cast<std::uint32_t>(i));
    }
  }
  entries_.push_back(Entry{element, length});
  return Type::arrayType(static_cast<std::uint32_t>(entries_.size() - 1));
}

Type TypeTable::element(Type arrayType) const {
  const std::size_t index = arrayType.index();
  return index < entries_.size() ? entries_[index].element : Type::error();
}

std::uint64_t TypeTable::length(Type arrayType) const {
  const std::size_t index = arrayType.index();
  return index < entries_.size() ? entries_[index].length : 0;
}

std::string typeName(Type type, const Module& module) {
  switch (type.kind()) {
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
    case TypeKind::Struct: {
      const std::size_t index = type.index();
      if (index < module.structs.size()) {
        return module.structs[index]->name;
      }
      return "<struct?>";
    }
    case TypeKind::Array: {
      const Type element = module.types.element(type);
      return "[" + typeName(element, module) + "; " +
             std::to_string(module.types.length(type)) + "]";
    }
  }
  return "error";
}

}  // namespace gable::ir
