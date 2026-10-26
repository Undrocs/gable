// Gable compiler -- typed intermediate representation.
//
// Semantic analysis lowers the AST into this representation. Types, operators,
// and variable slots are fully resolved: code generation never performs name
// lookup or type inference. The tree is structured (no basic blocks), which is
// sufficient for v0.1 and maps directly onto either structured LLVM IR or C.
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
  Error,  // produced by a failed type check; suppresses follow-on diagnostics
  Void,
  Bool,
  Str,
  I8,
  I16,
  I32,
  I64,
  U8,
  U16,
  U32,
  U64,
};

/// A resolved type. Types in Gable v0.1 are a closed set of builtins, so this
/// is a plain value type rather than an interned node.
class Type {
 public:
  constexpr Type() = default;
  constexpr explicit Type(TypeKind kind) : kind_(kind) {}

  TypeKind kind() const { return kind_; }
  bool isValid() const { return kind_ != TypeKind::Error; }
  bool isInteger() const;
  bool isSigned() const;
  /// Bit width for integer types; 0 for non-integers.
  unsigned bitWidth() const;
  /// Human-readable spelling, e.g. "i32". Returns "error" for TypeKind::Error.
  std::string name() const;

  bool operator==(const Type& other) const { return kind_ == other.kind_; }
  bool operator!=(const Type& other) const { return kind_ != other.kind_; }

  /// Parses a source-level type name. Returns TypeKind::Error for unknown names.
  static Type fromSourceName(std::string_view name);
  static bool isKnownSourceName(std::string_view name);

  static constexpr Type i8() { return Type(TypeKind::I8); }
  static constexpr Type i16() { return Type(TypeKind::I16); }
  static constexpr Type i32() { return Type(TypeKind::I32); }
  static constexpr Type i64() { return Type(TypeKind::I64); }
  static constexpr Type u8() { return Type(TypeKind::U8); }
  static constexpr Type u16() { return Type(TypeKind::U16); }
  static constexpr Type u32() { return Type(TypeKind::U32); }
  static constexpr Type u64() { return Type(TypeKind::U64); }
  static constexpr Type boolean() { return Type(TypeKind::Bool); }
  static constexpr Type str() { return Type(TypeKind::Str); }
  static constexpr Type voidType() { return Type(TypeKind::Void); }
  static constexpr Type error() { return Type(TypeKind::Error); }

 private:
  TypeKind kind_ = TypeKind::Error;
};

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
  };

  Expr(Kind nodeKind, Type nodeType, SourceSpan nodeSpan)
      : kind(nodeKind), type(nodeType), span(nodeSpan) {}

  Kind kind;
  Type type;
  SourceSpan span;

  // IntegerLiteral: two's-complement bit pattern of the literal value. The
  // value is guaranteed by semantic analysis to fit `type`.
  std::uint64_t integerValue = 0;
  // BoolLiteral
  bool boolValue = false;
  // StringLiteral: decoded UTF-8 bytes, NUL-terminated at runtime.
  std::string stringValue;
  // Local: index into the enclosing function's local table.
  std::uint32_t slot = 0;
  // Call: callee name, retained for readable generated code only.
  std::string name;
  // Unary / Binary
  UnaryOp unaryOp = UnaryOp::Negate;
  BinaryOp binaryOp = BinaryOp::Add;
  ExprPtr lhs;
  ExprPtr rhs;
  // Call: index into Module::functions, or the print builtin.
  std::vector<ExprPtr> arguments;
  std::uint32_t calleeIndex = 0;
  bool callsPrintBuiltin = false;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
  enum class Kind {
    Let,     // slot = <n>, type, isMutable, expression = initializer
    Assign,  // slot = <n>, expression = new value
    Expr,    // expression = expression statement
    Return,  // expression = returned value, or null for `return;`
    If,      // expression = condition, body, optional elseBody
    While,   // expression = condition, body
    Block,   // body
  };

  Stmt(Kind nodeKind, SourceSpan nodeSpan) : kind(nodeKind), span(nodeSpan) {}

  Kind kind;
  SourceSpan span;

  std::uint32_t slot = 0;
  Type type;
  bool isMutable = false;
  // Name is retained for diagnostics and readable generated code only.
  std::string name;

  ExprPtr expression;
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
  Type returnType;
  /// Parameters occupy the first `parameterCount` entries.
  std::vector<LocalVariable> locals;
  std::uint32_t parameterCount = 0;
  std::vector<StmtPtr> body;
  bool isEntryPoint = false;

  const LocalVariable& parameter(std::uint32_t index) const { return locals[index]; }
};

struct Module {
  std::vector<std::unique_ptr<Function>> functions;
};

}  // namespace gable::ir

#endif  // GABLE_IR_H
