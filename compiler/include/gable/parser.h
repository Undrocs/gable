// Gable compiler -- recursive descent + Pratt parser.
#ifndef GABLE_PARSER_H
#define GABLE_PARSER_H

#include <vector>

#include "gable/ast.h"
#include "gable/diagnostics.h"
#include "gable/token.h"

namespace gable {

/// Parses a token sequence into a Program.
///
/// The parser reports syntax errors through the DiagnosticEngine and recovers
/// at statement and declaration boundaries so that one run can surface several
/// independent mistakes. Callers must check `diagnostics.hasErrors()` before
/// using the result; a program containing errors is not guaranteed to be
/// complete.
class Parser {
 public:
  Parser(const std::vector<Token> &tokens, DiagnosticEngine &diagnostics);

  ast::Program parseProgram();

 private:
  /// RAII guard that counts nesting depth so that hostile input cannot
  /// exhaust the process stack through deeply nested expressions or blocks.
  struct DepthGuard {
    explicit DepthGuard(Parser& owner);
    ~DepthGuard();
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
    Parser& parser;
  };

  bool depthExceeded(const char* context);

  // Declarations and statements.
  std::unique_ptr<ast::FunctionDecl> parseFunction();
  ast::TypeSyntax parseTypeAnnotation();
  std::vector<std::unique_ptr<ast::Stmt>> parseBlock();
  std::unique_ptr<ast::Stmt> parseStatement();
  std::unique_ptr<ast::Stmt> parseLetStatement(bool isMutable);
  std::unique_ptr<ast::Stmt> parseReturnStatement();
  std::unique_ptr<ast::Stmt> parseIfStatement();
  std::unique_ptr<ast::Stmt> parseWhileStatement();

  // Expressions (Pratt).
  ast::ExprPtr parseExpression(int minPrecedence);
  ast::ExprPtr parsePrefix();
  ast::ExprPtr parsePrimary();

  int precedenceOf(TokenKind kind) const;
  BinaryOp binaryOpFor(TokenKind kind) const;

  // Token helpers.
  const Token &current() const { return tokens_[index_]; }
  const Token &peek(std::size_t lookahead = 1) const;
  bool check(TokenKind kind) const { return current().kind == kind; }
  bool checkAt(std::size_t lookahead, TokenKind kind) const;
  const Token &advance();
  bool match(TokenKind kind);
  const Token &expect(TokenKind kind, const char *context);

  /// Reports an error and skips tokens until a plausible statement boundary.
  void synchronize();

  SourceSpan spanFrom(const Token &begin) const {
    return SourceSpan{begin.span.begin, previousEnd_};
  }

  const std::vector<Token> &tokens_;
  DiagnosticEngine &diagnostics_;
  std::size_t index_ = 0;
  std::size_t previousEnd_ = 0;
  int depth_ = 0;
  /// Set once the nesting limit is reached; parsing then stops rather than
  /// attempting further recovery, which keeps memory and time bounded.
  bool depthLimitExceeded_ = false;
};

}  // namespace gable

#endif  // GABLE_PARSER_H
