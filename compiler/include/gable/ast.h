// Gable compiler -- abstract syntax tree.
//
// The AST is a direct, untyped representation of the source. It is produced by
// the parser and consumed by semantic analysis, which never mutates it; the
// typed representation is built separately (see gable/ir.h).
#ifndef GABLE_AST_H
#define GABLE_AST_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gable/operators.h"
#include "gable/source.h"

namespace gable::ast {

/// A type as written in the source. Names are kept as written so that semantic
/// analysis, not the parser, decides which type names exist.
struct TypeSyntax {
  enum class Kind { Named, Array };

  Kind kind = Kind::Named;
  SourceSpan span;
  /// Kind::Named
  std::string name;
  /// Kind::Array, e.g. `[i32; 4]`
  std::unique_ptr<TypeSyntax> element;
  std::uint64_t arrayLength = 0;
  SourceSpan lengthSpan;

  bool isValid() const { return kind == Kind::Array ? element != nullptr : !name.empty(); }
};

struct Expr {
  enum class Kind {
    IntegerLiteral,
    BoolLiteral,
    StringLiteral,
    Name,
    Unary,
    Binary,
    Call,
    FieldAccess,
    Index,
    Cast,
    ArrayLiteral,
    Range,  // only produced inside `for` headers
  };

  Expr(Kind nodeKind, SourceSpan nodeSpan) : kind(nodeKind), span(nodeSpan) {}

  Kind kind;
  SourceSpan span;

  // IntegerLiteral
  std::uint64_t integerValue = 0;
  bool unsignedLiteral = false;
  // BoolLiteral
  bool boolValue = false;
  // StringLiteral
  std::string stringValue;
  // Name, and the callee name for Call
  std::string name;
  // Unary / Binary
  UnaryOp unaryOp = UnaryOp::Negate;
  BinaryOp binaryOp = BinaryOp::Add;
  // Unary: lhs. Binary: lhs, rhs. FieldAccess: lhs. Index: lhs, rhs.
  // Cast: lhs. Range: lhs (start), rhs (end).
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  // Call arguments, array literal elements
  std::vector<std::unique_ptr<Expr>> arguments;
  // FieldAccess
  std::string fieldName;
  SourceSpan fieldSpan;
  // Cast
  TypeSyntax targetType;
};

using ExprPtr = std::unique_ptr<Expr>;

struct Stmt {
  enum class Kind {
    Let,
    Assign,
    ExprStmt,
    Return,
    If,
    While,
    For,
    Break,
    Continue,
    Block,
  };

  Stmt(Kind nodeKind, SourceSpan nodeSpan) : kind(nodeKind), span(nodeSpan) {}

  Kind kind;
  SourceSpan span;

  // Let
  std::string name;
  SourceSpan nameSpan;
  bool isMutable = false;
  bool hasTypeAnnotation = false;
  TypeSyntax typeAnnotation;
  // Let initializer, Assign value, ExprStmt expression, Return value,
  // If/While condition, For iterable or range start
  ExprPtr value;
  // Assign target (a name, field access, or index expression)
  ExprPtr target;
  // For: range end when `value` is a Range-like iterable
  ExprPtr rangeEnd;
  // For: loop variable name (`for i in 0..n`, `for item in items`)
  std::string loopVariable;
  SourceSpan loopVariableSpan;
  bool isRangeLoop = false;
  // If then-branch and While/For body; Block contents
  std::vector<std::unique_ptr<Stmt>> body;
  // If else-branch; empty when there is no `else`
  std::vector<std::unique_ptr<Stmt>> elseBody;
  bool hasElse = false;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct Parameter {
  std::string name;
  SourceSpan span;
  TypeSyntax type;
};

struct FunctionDecl {
  std::string name;
  SourceSpan nameSpan;
  SourceSpan span;
  std::vector<Parameter> parameters;
  bool hasReturnType = false;
  TypeSyntax returnType;
  std::vector<StmtPtr> body;
  /// Index of the source file that declared this function.
  std::uint32_t file = 0;
};

struct FieldDecl {
  std::string name;
  SourceSpan nameSpan;
  TypeSyntax type;
};

struct StructDecl {
  std::string name;
  SourceSpan nameSpan;
  SourceSpan span;
  std::vector<FieldDecl> fields;
  std::uint32_t file = 0;
};

struct ImportDecl {
  std::string path;
  SourceSpan span;
};

struct Program {
  std::vector<ImportDecl> imports;
  std::vector<std::unique_ptr<FunctionDecl>> functions;
  std::vector<std::unique_ptr<StructDecl>> structs;
};

}  // namespace gable::ast

#endif  // GABLE_AST_H
