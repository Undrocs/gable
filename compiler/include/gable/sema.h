// Gable compiler -- semantic analysis, type checking, and lowering to the
// typed IR.
#ifndef GABLE_SEMA_H
#define GABLE_SEMA_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gable/ast.h"
#include "gable/diagnostics.h"
#include "gable/ir.h"

namespace gable {

/// Performs name resolution, type checking, and lowering for a parsed program.
///
/// `analyze` appends diagnostics for every problem it finds; it continues after
/// errors where it can so that a single compilation reports several mistakes.
/// The returned module is only complete when no errors were reported.
class SemanticAnalyzer {
 public:
  SemanticAnalyzer(DiagnosticEngine& diagnostics);

  /// Lowering result. An empty module with no diagnostics means the program
  /// declared no functions, which analyze() rejects as a missing `main`.
  std::unique_ptr<ir::Module> analyze(const ast::Program& program);

 private:
  struct Binding {
    std::uint32_t slot = 0;
    ir::Type type;
    bool isMutable = false;
    bool isParameter = false;
    SourceSpan span;
  };

  struct FunctionInfo {
    const ast::FunctionDecl* declaration = nullptr;
    ir::Type returnType;
    std::vector<ir::Type> parameterTypes;
    std::size_t moduleIndex = 0;
  };

  using Scope = std::unordered_map<std::string, Binding>;

  // Program-level checks.
  void declareFunctions(const ast::Program& program);
  void checkEntryPoint();
  std::unique_ptr<ir::Function> checkFunction(const ast::FunctionDecl& declaration);

  // Statements.
  ir::StmtPtr checkStmt(const ast::Stmt& stmt);
  ir::StmtPtr checkLet(const ast::Stmt& stmt);
  ir::StmtPtr checkAssign(const ast::Stmt& stmt);
  ir::StmtPtr checkReturn(const ast::Stmt& stmt);
  ir::StmtPtr checkIf(const ast::Stmt& stmt);
  ir::StmtPtr checkWhile(const ast::Stmt& stmt);
  ir::StmtPtr checkBlock(const ast::Stmt& stmt);
  std::vector<ir::StmtPtr> checkStatements(const std::vector<ast::StmtPtr>& statements);

  // Expressions. `expected` carries the type demanded by the surrounding
  // context, which is what allows integer literals to be typed without a suffix
  // syntax; it never permits implicit conversion of a non-literal.
  ir::ExprPtr checkExpr(const ast::Expr& expr, std::optional<ir::Type> expected);
  ir::ExprPtr checkUnary(const ast::Expr& expr, std::optional<ir::Type> expected);
  ir::ExprPtr checkBinary(const ast::Expr& expr, std::optional<ir::Type> expected);
  ir::ExprPtr checkCall(const ast::Expr& expr, std::optional<ir::Type> expected);
  ir::ExprPtr checkIntegerLiteral(const ast::Expr& expr, std::optional<ir::Type> expected);

  /// Describes an expression that is an integer literal, optionally negated.
  struct LiteralInfo {
    std::uint64_t magnitude = 0;
    bool negate = false;
    bool hasUnsignedSuffix = false;
  };

  /// Recognises `42` and `-42` (an integer literal under a unary minus).
  static std::optional<LiteralInfo> asIntegerLiteral(const ast::Expr& expr);

  /// Converts a literal to `type` after a range check.
  ir::ExprPtr literalAs(const ast::Expr& node, LiteralInfo literal, ir::Type type);

  // Scope management.
  void pushScope();
  void popScope();
  Binding* lookup(const std::string& name);

  // Helpers.
  ir::Type resolveType(const ast::TypeSyntax& syntax);
  void reportTypeMismatch(SourceSpan span, const std::string& context, ir::Type expected,
                          ir::Type actual);
  bool alwaysReturns(const std::vector<ir::StmtPtr>& statements) const;
  bool stmtAlwaysReturns(const ir::Stmt& stmt) const;

  DiagnosticEngine& diagnostics_;
  std::vector<Scope> scopes_;
  std::unordered_map<std::string, FunctionInfo> functions_;

  // State for the function currently being checked.
  ir::Function* currentFunction_ = nullptr;
  const ast::FunctionDecl* currentDeclaration_ = nullptr;
  ir::Type currentReturnType_ = ir::Type::voidType();
};

}  // namespace gable

#endif  // GABLE_SEMA_H
