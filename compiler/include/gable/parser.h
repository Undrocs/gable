// Gable compiler -- recursive descent + Pratt parser.
#ifndef GABLE_PARSER_H
#define GABLE_PARSER_H

#include <cstdint>
#include <memory>
#include <vector>

#include "gable/ast.h"
#include "gable/diagnostics.h"
#include "gable/token.h"

namespace gable {

/// Parses one token sequence into declarations.
///
/// The parser reports syntax errors through Diagnostics and recovers at
/// statement and declaration boundaries so that one run can surface several
/// independent mistakes. Callers must check `diagnostics.hasErrors()` before
/// using the result; a program containing errors is not guaranteed to be
/// complete.
class Parser {
 public:
  Parser(const std::vector<Token>& tokens, Diagnostics& diagnostics, std::uint32_t fileIndex);

  ast::Program parseProgram();

 private:
  /// RAII guard that counts nesting depth so that hostile input cannot exhaust
  /// the process stack through deeply nested expressions or blocks.
  struct DepthGuard {
    explicit DepthGuard(Parser& owner);
    ~DepthGuard();
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
    Parser& parser;
  };

  bool depthExceeded(const char* context);

  /// Reports a resource-limit error once and returns true, meaning "stop".
  bool limitReached(SourceSpan span, std::string message);

  /// True when an expression tree of `depth` levels is still within the limit;
  /// otherwise reports the diagnostic once and returns false.
  bool growTreeDepth(int depth, SourceSpan span);

  /// Counts one syntax node against the per-file budget. Parsing stops with a
  /// diagnostic when the budget is exhausted, so a large input produces one
  /// error instead of exhausting memory.
  bool consumeNodeBudget();


  // Declarations.
  std::unique_ptr<ast::FunctionDecl> parseFunction();
  std::unique_ptr<ast::StructDecl> parseStruct();
  void parseImport(ast::Program& program);
  ast::TypeSyntax parseTypeAnnotation();

  // Statements.
  std::vector<std::unique_ptr<ast::Stmt>> parseBlock();
  std::unique_ptr<ast::Stmt> parseStatement();
  std::unique_ptr<ast::Stmt> parseLetStatement(bool isMutable);
  std::unique_ptr<ast::Stmt> parseReturnStatement();
  /// True when the current token ends a statement: a line break, ';', the end of
  /// the enclosing block, or the end of the file.
  bool atStatementEnd() const;
  /// Consumes an optional ';' and reports a missing statement end otherwise.
  void endStatement(const char* context);
  /// Reports `if x = 2` where a comparison belongs and consumes the assignment.
  void discardComparisonAssignment(const char* statement);
  std::unique_ptr<ast::Stmt> parseIfStatement();
  std::unique_ptr<ast::Stmt> parseWhileStatement();
  std::unique_ptr<ast::Stmt> parseForStatement();

  // Expressions (Pratt).
  ast::ExprPtr parseExpression(int minPrecedence);
  ast::ExprPtr parsePrefix();
  ast::ExprPtr parsePrimary();
  /// Member access, indexing and casts: the tightest-binding operators.
  ast::ExprPtr parsePostfix(ast::ExprPtr base);

  int precedenceOf(TokenKind kind) const;
  BinaryOp binaryOpFor(TokenKind kind) const;

  // Token helpers.
  const Token& current() const { return tokens_[index_]; }
  const Token& peek(std::size_t lookahead = 1) const;
  bool check(TokenKind kind) const { return current().kind == kind; }
  bool checkAt(std::size_t lookahead, TokenKind kind) const;
  const Token& advance();
  bool match(TokenKind kind);
  const Token& expect(TokenKind kind, const char* context);

  void error(SourceSpan span, std::string message);
  void note(SourceSpan span, std::string message);

  /// Reports an error and skips tokens until a plausible statement boundary.
  void synchronize();

  SourceSpan spanFrom(const Token& begin) const {
    return SourceSpan{begin.span.begin, previousEnd_};
  }

  const std::vector<Token>& tokens_;
  Diagnostics& diagnostics_;
  std::uint32_t file_;
  std::size_t index_ = 0;
  std::size_t previousEnd_ = 0;
  int depth_ = 0;
  /// Depth of the expression tree most recently produced by parseExpression,
  /// parsePrefix, or parsePostfix. Left-associative operator chains are parsed in
  /// a loop, so the recursion guard alone does not bound the depth of the tree
  /// they build; this counter does.
  int expressionDepth_ = 0;
  /// Syntax nodes created for this file, against the per-file budget.
  std::size_t nodeCount_ = 0;
  /// Set once a resource limit (nesting or node budget) is reached; parsing then
  /// stops rather than attempting further recovery, which keeps memory and time
  /// bounded.
  bool depthLimitExceeded_ = false;
  /// Set after the first non-import declaration, so imports must come first.
  bool sawDeclaration_ = false;
};

}  // namespace gable

#endif  // GABLE_PARSER_H
