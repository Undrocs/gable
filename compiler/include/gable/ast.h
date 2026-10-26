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

/// A type as written in the source, e.g. `i64`. Kept as a name so that the
/// parser does not need to know which type names exist.
struct TypeSyntax {
  std::string name;
  SourceSpan span;
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
  // Unary, Binary
  UnaryOp unaryOp = UnaryOp::Negate;
  BinaryOp binaryOp = BinaryOp::Add;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  // Call
  std::vector<std::unique_ptr<Expr>> arguments;
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
    Block,
  };

  Stmt(Kind nodeKind, SourceSpan nodeSpan) : kind(nodeKind), span(nodeSpan) {}

  Kind kind;
  SourceSpan span;

  // Let / Assign
  std::string name;
  SourceSpan nameSpan;
  bool isMutable = false;
  bool hasTypeAnnotation = false;
  TypeSyntax typeAnnotation;
  // Let initializer, Assign value, ExprStmt expression, Return value, If/While condition
  ExprPtr expression;
  // If then-branch and While body; Block contents
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
};

struct Program {
  std::vector<std::unique_ptr<FunctionDecl>> functions;
};

}  // namespace gable::ast

#endif  // GABLE_AST_H
