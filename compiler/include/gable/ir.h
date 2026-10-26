// Gable compiler -- typed intermediate representation.
//
// Semantic analysis lowers the AST into this representation. Types, operators,
// local slots, struct layouts, and callee indices are fully resolved: code
// generation never performs name lookup or type inference. The tree is
// structured (no basic blocks), which keeps lowering direct and is sufficient
// for both the C and a future LLVM backend.
#ifndef GABLE_IR_H
#define GABLE_IR_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gable/operators.h"
#include "gable/source.h"

namespace gable::ir {

enum class TypeKind {
  Error,  // produced by a failed check; suppresses follow-on diagnostics
  Void,
  Bool,
  Str,  // immutable UTF-8 byte sequence with an explicit length
  I8,
  I16,
  I32,
  I64,
  U8,
  U16,
  U32,
  U64,
  Struct,
  Array,
};

/// Builtin functions. They are part of the language surface and are lowered to
/// calls into the runtime or, for `print`, to a runtime call selected by the
/// operand type.
enum class Builtin {
  None,
  Print,
  Eprint,
  ArgCount,
  Arg,
  FsReadText,
  FsWriteText,
  StrOfI64,
  StrOfBool,
  ParseI64,
  Find,
  Slice,
  Trim,
};

class Type {
 public:
  constexpr Type() = default;

  static constexpr Type error() { return Type(TypeKind::Error); }
  static constexpr Type voidType() { return Type(TypeKind::Void); }
  static constexpr Type boolean() { return Type(TypeKind::Bool); }
  static constexpr Type str() { return Type(TypeKind::Str); }
  static constexpr Type i8() { return Type(TypeKind::I8); }
  static constexpr Type i16() { return Type(TypeKind::I16); }
  static constexpr Type i32() { return Type(TypeKind::I32); }
  static constexpr Type i64() { return Type(TypeKind::I64); }
  static constexpr Type u8() { return Type(TypeKind::U8); }
  static constexpr Type u16() { return Type(TypeKind::U16); }
  static constexpr Type u32() { return Type(TypeKind::U32); }
  static constexpr Type u64() { return Type(TypeKind::U64); }
  static constexpr Type structType(std::uint32_t index) {
    Type type(TypeKind::Struct);
    type.index_ = index;
    return type;
  }
  static constexpr Type arrayType(std::uint32_t index) {
    Type type(TypeKind::Array);
    type.index_ = index;
    return type;
  }

  TypeKind kind() const { return kind_; }
  /// Struct index for TypeKind::Struct, array index for TypeKind::Array.
  std::uint32_t index() const { return index_; }

  bool isValid() const { return kind_ != TypeKind::Error; }
  bool isInteger() const;
  bool isSigned() const;
  bool isAggregate() const { return kind_ == TypeKind::Struct || kind_ == TypeKind::Array; }
  bool isScalar() const {
    return isInteger() || kind_ == TypeKind::Bool || kind_ == TypeKind::Str;
  }
  /// Bit width for integer types; 0 for everything else.
  unsigned bitWidth() const;

  bool operator==(const Type& other) const {
    return kind_ == other.kind_ && index_ == other.index_;
  }
  bool operator!=(const Type& other) const { return !(*this == other); }

  /// Parses a built-in type name. Returns TypeKind::Error for anything else.
  static Type fromSourceName(std::string_view name);
  static bool isScalarSourceName(std::string_view name);

 private:
  constexpr explicit Type(TypeKind kind) : kind_(kind) {}

  TypeKind kind_ = TypeKind::Error;
  std::uint32_t index_ = 0;
};

struct StructField {
  std::string name;
  Type type;
  SourceSpan span;
};

struct StructInfo {
  std::string name;
  SourceSpan span;
  std::uint32_t file = 0;
  std::vector<StructField> fields;
  /// Prelude structs describe values returned by builtin functions. The runtime
  /// header defines their C layout, so code generation does not declare them.
  bool fromPrelude = false;
};

/// Interns array types and records their element type and length, so an array
/// type is identified by a single index.
class TypeTable {
 public:
  Type array(Type element, std::uint64_t length);
  Type element(Type type) const;
  std::uint64_t length(Type type) const;

 private:
  struct Entry {
    Type element;
    std::uint64_t length = 0;
  };
  std::vector<Entry> entries_;
};

struct Module;

/// Human-readable spelling of a type, e.g. `i32`, `Point`, `[str; 4]`.
std::string typeName(Type type, const Module& module);

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  enum class Kind {
    IntegerLiteral,
    BoolLiteral,
    StringLiteral,
    Local,
    Unary,
    Binary,
    Call,
    StructInit,
    FieldAccess,
    Index,
    Cast,
    ArrayLiteral,
    Length,  // `.len` of a string (bytes) or array (elements)
  };

  Expr(Kind nodeKind, Type nodeType, SourceSpan nodeSpan)
      : kind(nodeKind), type(nodeType), span(nodeSpan) {}

  Kind kind;
  Type type;
  SourceSpan span;

  /// IntegerLiteral: two's-complement bit pattern that fits `type`.
  std::uint64_t integerValue = 0;
  /// BoolLiteral
  bool boolValue = false;
  /// StringLiteral: decoded UTF-8 bytes. May contain any byte except NUL.
  std::string stringValue;
  /// Local: index into the enclosing function's local table.
  std::uint32_t slot = 0;
  /// Call/StructInit/FieldAccess: name, kept for readable generated C only.
  std::string name;
  /// Unary and Binary operator.
  UnaryOp unaryOp = UnaryOp::Negate;
  BinaryOp binaryOp = BinaryOp::Add;
  /// Unary: lhs. Binary: lhs and rhs. FieldAccess/Index/Length/Cast: lhs.
  /// Index: rhs is the subscript.
  ExprPtr lhs;
  ExprPtr rhs;
  /// Call arguments, StructInit field values in declaration order,
  /// ArrayLiteral elements.
  std::vector<ExprPtr> arguments;
  /// Call: index into Module::functions.
  std::uint32_t calleeIndex = 0;
  /// Call: set for builtins.
  Builtin builtin = Builtin::None;
  /// StructInit and FieldAccess: field index within the struct.
  std::uint32_t fieldIndex = 0;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
  enum class Kind {
    Let,       // slot, type, isMutable, value = initializer (may be null)
    Assign,    // target = lvalue, value = new value
    Expr,      // value = expression statement
    Return,    // value = returned value, or null for `return;`
    If,        // value = condition, body, optional elseBody
    While,     // value = condition, body
    ForRange,  // slot/type = loop variable, value = start, limit = exclusive end
    ForEach,   // slot/type = element copy, value = array, body
    Block,     // body
    Break,
    Continue,
  };

  Stmt(Kind nodeKind, SourceSpan nodeSpan) : kind(nodeKind), span(nodeSpan) {}

  Kind kind;
  SourceSpan span;

  std::uint32_t slot = 0;
  Type type;
  bool isMutable = false;
  /// Retained for diagnostics and readable generated code only.
  std::string name;

  ExprPtr value;
  /// Assign: the assigned lvalue.
  ExprPtr target;
  /// ForRange: exclusive upper bound, evaluated once before the loop.
  ExprPtr limit;

  std::vector<StmtPtr> body;
  std::vector<StmtPtr> elseBody;
  bool hasElse = false;
};

struct LocalVariable {
  std::string name;
  Type type;
  SourceSpan span;
  bool isMutable = false;
  bool isParameter = false;
};

struct Function {
  std::string name;
  SourceSpan span;
  std::uint32_t file = 0;
  Type returnType;
  /// Parameters occupy the first `parameterCount` entries.
  std::vector<LocalVariable> locals;
  std::uint32_t parameterCount = 0;
  std::vector<StmtPtr> body;
  bool isEntryPoint = false;
};

struct Module {
  /// Every struct of the program in declaration order, prelude structs first.
  std::vector<std::unique_ptr<StructInfo>> structs;
  TypeTable types;
  /// Every function of the program in declaration order. A call refers to its
  /// callee by index into this vector.
  std::vector<std::unique_ptr<Function>> functions;
};

}  // namespace gable::ir

#endif  // GABLE_IR_H
