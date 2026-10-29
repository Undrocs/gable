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

/// Performs name resolution, type checking, and lowering for the prelude and
/// every user file of one program.
///
/// `analyze` reports every problem it finds and keeps going where it can, so a
/// single compilation surfaces several independent mistakes. The returned
/// module is only meaningful when no errors were reported.
class SemanticAnalyzer {
 public:
  explicit SemanticAnalyzer(Diagnostics& diagnostics);

  /// Analyzes the prelude declarations followed by every user program, in the
  /// order given.
  std::unique_ptr<ir::Module> analyze(const ast::Program& prelude,
                                      const std::vector<const ast::Program*>& programs);

 private:
  struct Binding {
    std::uint32_t slot = 0;
    ir::Type type;
    bool isMutable = false;
    bool isParameter = false;
    SourceSpan span;
    std::uint32_t file = 0;
  };

  struct FunctionInfo {
    const ast::FunctionDecl* declaration = nullptr;
    ir::Type returnType;
    std::vector<ir::Type> parameterTypes;
    std::uint32_t index = 0;  // index into Module::functions
  };

  struct BuiltinInfo {
    std::string_view name;
    ir::Builtin id = ir::Builtin::None;
    ir::Type returnType;
    std::vector<ir::Type> parameterTypes;
  };

  using Scope = std::unordered_map<std::string, Binding>;

  // Declarations shared by every file.
  void registerStructs(const ast::Program& program, bool fromPrelude);
  void resolveStructFields();
  void registerFunctions(const ast::Program& program);
  void registerBuiltins();
  void checkEntryPoint();

  void checkFunction(const ast::FunctionDecl& declaration,
                     const FunctionInfo& info, ir::Function& out);

  // Statements.
  ir::StmtPtr checkStmt(const ast::Stmt& stmt);
  ir::StmtPtr checkLet(const ast::Stmt& stmt);
  ir::StmtPtr checkAssign(const ast::Stmt& stmt);
  ir::StmtPtr checkReturn(const ast::Stmt& stmt);
  ir::StmtPtr checkIf(const ast::Stmt& stmt);
  ir::StmtPtr checkWhile(const ast::Stmt& stmt);
  ir::StmtPtr checkFor(const ast::Stmt& stmt);
  ir::StmtPtr checkBreakOrContinue(const ast::Stmt& stmt);
  ir::StmtPtr checkBlock(const ast::Stmt& stmt);
  std::vector<ir::StmtPtr> checkStatements(const std::vector<ast::StmtPtr>& statements);

  // Expressions. `expected` is the type demanded by the surrounding context and
  // is what allows an integer literal to be typed without a suffix; it never
  // permits an implicit conversion of a non-literal value.
  ir::ExprPtr checkExpr(const ast::Expr& expr, std::optional<ir::Type> expected);
  ir::ExprPtr checkUnary(const ast::Expr& expr, std::optional<ir::Type> expected);
  ir::ExprPtr checkBinary(const ast::Expr& expr, std::optional<ir::Type> expected);
  ir::ExprPtr checkCall(const ast::Expr& expr);
  ir::ExprPtr checkBuiltinCall(const ast::Expr& expr, const BuiltinInfo& builtin);
  ir::ExprPtr checkListPush(const ast::Expr& expr);
  ir::ExprPtr checkPrintCall(const ast::Expr& expr, ir::Builtin id);
  ir::ExprPtr checkStructInit(const ast::Expr& expr, const ir::StructInfo& info);
  ir::ExprPtr checkFieldAccess(const ast::Expr& expr);
  ir::ExprPtr checkIndex(const ast::Expr& expr);
  ir::ExprPtr checkCast(const ast::Expr& expr);
  ir::ExprPtr checkArrayLiteral(const ast::Expr& expr, std::optional<ir::Type> expected);
  /// `"a $b c"` becomes string concatenation, converting integers and booleans
  /// to text with the same built-ins a program would call itself.
  ir::ExprPtr checkInterpolatedString(const ast::Expr& expr);
  /// `values.push(x)`, `text.trim()`: the built-in operation with the receiver as
  /// its first argument.
  ir::ExprPtr checkMethodCall(const ast::Expr& expr);
  void reportUnknownMethod(const ast::Expr& expr, ir::Type receiverType);
  void noteAvailableMethods(SourceSpan span, ir::Type receiverType);
  ir::ExprPtr checkIntegerLiteral(const ast::Expr& expr, std::optional<ir::Type> expected);

  /// Resolves an assignment target and reports expressions that are not
  /// assignable.
  ir::ExprPtr checkLvalue(const ast::Expr& expr);

  /// An integer literal, possibly negated, as written in the source.
  struct LiteralInfo {
    std::uint64_t magnitude = 0;
    bool negate = false;
    bool hasUnsignedSuffix = false;
  };
  static std::optional<LiteralInfo> asIntegerLiteral(const ast::Expr& expr);
  ir::ExprPtr literalAs(const ast::Expr& node, const LiteralInfo& literal, ir::Type type);
  /// Checks `expr` in a context that demands `expected`; a literal takes the
  /// expected type and anything else must already have it.
  ir::ExprPtr conformTo(const ast::Expr& expr, ir::Type expected, const char* context);

  // Scope management.
  void pushScope();
  void popScope();
  Binding* lookup(const std::string& name);

  // Helpers.
  ir::Type resolveType(const ast::TypeSyntax& syntax);
  const ir::StructInfo* findStruct(const std::string& name) const;
  const ir::StructInfo* structOf(ir::Type type) const;
  int fieldIndex(const ir::StructInfo& info, const std::string& name) const;
  const BuiltinInfo* findBuiltin(std::string_view name) const;
  void reportUnknownField(SourceSpan span, const ir::StructInfo& info, const std::string& name);
  void reportTypeMismatch(SourceSpan span, const std::string& context, ir::Type expected,
                          ir::Type actual);
  std::string fieldListForHint(const ir::StructInfo& info) const;
  std::string nameOfSlot(std::uint32_t slot) const;
  bool alwaysReturns(const std::vector<ir::StmtPtr>& statements) const;
  bool stmtAlwaysReturns(const ir::Stmt& stmt) const;

  Diagnostics& diagnostics_;
  std::unique_ptr<ir::Module> module_;
  std::vector<Scope> scopes_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::vector<BuiltinInfo> builtins_;
  std::unordered_map<std::string, ir::Type> structTypesByName_;
  std::vector<std::pair<ir::StructInfo*, const ast::StructDecl*>> pendingStructFields_;

  // State for the function or declaration currently being checked.
  ir::Function* currentFunction_ = nullptr;
  ir::Type currentReturnType_ = ir::Type::voidType();
  std::string currentFunctionName_;
  std::uint32_t currentFile_ = 0;
  /// Number of enclosing loops; `break` and `continue` require at least one.
  int loopDepth_ = 0;
};

}  // namespace gable

#endif  // GABLE_SEMA_H
