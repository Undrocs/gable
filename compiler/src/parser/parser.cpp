#include "gable/parser.h"

#include <algorithm>

#include <string>
#include <utility>

namespace gable {
namespace {

bool isTypeKeyword(TokenKind kind) {
  switch (kind) {
    case TokenKind::KwI8:
    case TokenKind::KwI16:
    case TokenKind::KwI32:
    case TokenKind::KwI64:
    case TokenKind::KwU8:
    case TokenKind::KwU16:
    case TokenKind::KwU32:
    case TokenKind::KwU64:
    case TokenKind::KwBool:
    case TokenKind::KwStr:
    case TokenKind::KwVoid:
      return true;
    default:
      return false;
  }
}

/// How many tokens to skip when a declaration cannot be parsed.
constexpr std::size_t kMaxSynchronizeSteps = 4096;

/// Maximum nesting depth for expressions, blocks, and types. Well beyond anything
/// readable, low enough to keep the recursive-descent parser's stack bounded.
constexpr int kMaxNestingDepth = 200;

/// Maximum number of syntax nodes in one source file. A file is at most 64 MiB,
/// which already bounds this, but a million tiny expressions is both useless and
/// expensive to analyse: the budget turns it into a diagnostic instead of a
/// process that runs out of memory.
constexpr std::size_t kMaxSyntaxNodes = 2'000'000;

}  // namespace

Parser::Parser(const std::vector<Token>& tokens, Diagnostics& diagnostics, std::uint32_t fileIndex)
    : tokens_(tokens), diagnostics_(diagnostics), file_(fileIndex) {}

Parser::DepthGuard::DepthGuard(Parser& owner) : parser(owner) { ++parser.depth_; }

Parser::DepthGuard::~DepthGuard() { --parser.depth_; }

bool Parser::depthExceeded(const char* context) {
  if (depth_ <= kMaxNestingDepth) {
    return false;
  }
  return limitReached(current().span, std::string(context) + " is nested too deeply (limit " +
                                            std::to_string(kMaxNestingDepth) + " levels)");
}

bool Parser::limitReached(SourceSpan span, std::string message) {
  if (!depthLimitExceeded_) {
    depthLimitExceeded_ = true;
    error(span, std::move(message));
  }
  return true;
}

bool Parser::consumeNodeBudget() {
  ++nodeCount_;
  if (nodeCount_ <= kMaxSyntaxNodes) {
    return true;
  }
  return limitReached(current().span,
                      "this file contains more than " + std::to_string(kMaxSyntaxNodes) +
                          " syntax nodes; split it into smaller files");
}

void Parser::error(SourceSpan span, std::string message) {
  diagnostics_.error(file_, span, std::move(message));
}

void Parser::note(SourceSpan span, std::string message) {
  diagnostics_.note(file_, span, std::move(message));
}

const Token& Parser::peek(std::size_t lookahead) const {
  const std::size_t index = index_ + lookahead;
  return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

bool Parser::checkAt(std::size_t lookahead, TokenKind kind) const {
  return peek(lookahead).kind == kind;
}

const Token& Parser::advance() {
  const Token& token = tokens_[index_];
  previousEnd_ = token.span.end;
  if (token.kind != TokenKind::EndOfFile) {
    ++index_;
  }
  return token;
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) {
    return false;
  }
  advance();
  return true;
}

const Token& Parser::expect(TokenKind kind, const char* context) {
  if (check(kind)) {
    return advance();
  }
  const Token& found = current();
  error(found.span, std::string("expected ") + tokenKindName(kind) + " " + context + ", found " +
                        tokenKindName(found.kind));
  return found;
}

ast::Program Parser::parseProgram() {
  ast::Program program;
  while (!check(TokenKind::EndOfFile) && !depthLimitExceeded_) {
    switch (current().kind) {
      case TokenKind::KwImport:
        parseImport(program);
        continue;
      case TokenKind::KwFn: {
        sawDeclaration_ = true;
        auto function = parseFunction();
        if (function) {
          program.functions.push_back(std::move(function));
        }
        continue;
      }
      case TokenKind::KwStruct: {
        sawDeclaration_ = true;
        auto declaration = parseStruct();
        if (declaration) {
          program.structs.push_back(std::move(declaration));
        }
        continue;
      }
      default: {
        const Token& unexpected = current();
        error(unexpected.span, std::string("expected 'fn', 'struct', or 'import', found ") +
                                   tokenKindName(unexpected.kind));
        advance();
        synchronize();
        sawDeclaration_ = true;
        continue;
      }
    }
  }
  return program;
}

void Parser::parseImport(ast::Program& program) {
  const Token& keyword = advance();  // 'import'
  if (sawDeclaration_) {
    error(keyword.span, "imports must appear before any function or struct declaration");
    note(keyword.span, "move this import to the top of the file");
  }
  if (!check(TokenKind::StringLiteral)) {
    error(current().span, std::string("expected a quoted file path after 'import', found ") +
                              tokenKindName(current().kind));
    synchronize();
    return;
  }
  const Token& path = advance();
  if (path.stringValue.empty()) {
    error(path.span, "import path must not be empty");
  }
  expect(TokenKind::Semicolon, "after import path");
  ast::ImportDecl declaration;
  declaration.path = path.stringValue;
  declaration.span = SourceSpan{keyword.span.begin, previousEnd_};
  program.imports.push_back(std::move(declaration));
}

void Parser::synchronize() {
  std::size_t steps = 0;
  while (!check(TokenKind::EndOfFile) && steps < kMaxSynchronizeSteps) {
    ++steps;
    if (match(TokenKind::Semicolon)) {
      return;
    }
    if (check(TokenKind::RBrace)) {
      return;
    }
    if (check(TokenKind::KwFn) || check(TokenKind::KwStruct) || check(TokenKind::KwImport) ||
        check(TokenKind::KwLet) || check(TokenKind::KwVar) || check(TokenKind::KwReturn) ||
        check(TokenKind::KwIf) || check(TokenKind::KwWhile) || check(TokenKind::KwFor) ||
        check(TokenKind::KwBreak) || check(TokenKind::KwContinue)) {
      return;
    }
    advance();
  }
}

std::unique_ptr<ast::FunctionDecl> Parser::parseFunction() {
  const Token& fnToken = advance();  // 'fn'
  auto function = std::make_unique<ast::FunctionDecl>();
  function->span = fnToken.span;
  function->file = file_;

  if (!check(TokenKind::Identifier)) {
    error(current().span, std::string("expected a function name, found ") +
                              tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  const Token& nameToken = advance();
  function->name = nameToken.text;
  function->nameSpan = nameToken.span;

  if (!check(TokenKind::LParen)) {
    error(current().span, "expected '(' after function name '" + function->name + "', found " +
                              tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  advance();

  if (!check(TokenKind::RParen)) {
    for (;;) {
      if (!check(TokenKind::Identifier)) {
        error(current().span, "expected a parameter name, found " +
                                  std::string(tokenKindName(current().kind)));
        synchronize();
        return nullptr;
      }
      ast::Parameter parameter;
      const Token& parameterName = advance();
      parameter.name = parameterName.text;
      parameter.span = parameterName.span;

      if (!check(TokenKind::Colon)) {
        error(current().span,
              "expected ':' and a type after parameter name '" + parameter.name + "'");
        synchronize();
        return nullptr;
      }
      advance();
      parameter.type = parseTypeAnnotation();
      if (!parameter.type.isValid()) {
        synchronize();
        return nullptr;
      }
      function->parameters.push_back(std::move(parameter));

      if (match(TokenKind::Comma)) {
        continue;
      }
      break;
    }
  }
  if (!check(TokenKind::RParen)) {
    error(current().span,
          "expected ')' to close the parameter list of '" + function->name + "'");
    synchronize();
    return nullptr;
  }
  advance();

  if (match(TokenKind::Arrow)) {
    function->returnType = parseTypeAnnotation();
    function->hasReturnType = function->returnType.isValid();
    if (!function->hasReturnType) {
      synchronize();
      return nullptr;
    }
  }

  if (!check(TokenKind::LBrace)) {
    error(current().span,
          "expected '{' to begin the body of function '" + function->name + "'");
    synchronize();
    return nullptr;
  }
  function->body = parseBlock();
  return function;
}

std::unique_ptr<ast::StructDecl> Parser::parseStruct() {
  const Token& keyword = advance();  // 'struct'
  auto declaration = std::make_unique<ast::StructDecl>();
  declaration->span = keyword.span;
  declaration->file = file_;

  if (!check(TokenKind::Identifier)) {
    error(current().span, std::string("expected a struct name, found ") +
                              tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  const Token& name = advance();
  declaration->name = name.text;
  declaration->nameSpan = name.span;

  if (!check(TokenKind::LBrace)) {
    error(current().span, "expected '{' to begin the fields of struct '" + declaration->name + "'");
    synchronize();
    return nullptr;
  }
  advance();

  while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile) && !depthLimitExceeded_) {
    if (!check(TokenKind::Identifier)) {
      error(current().span, std::string("expected a field name, found ") +
                                tokenKindName(current().kind));
      advance();
      synchronize();
      continue;
    }
    ast::FieldDecl field;
    const Token& fieldName = advance();
    field.name = fieldName.text;
    field.nameSpan = fieldName.span;
    if (!check(TokenKind::Colon)) {
      error(current().span, "expected ':' and a type after field name '" + field.name + "'");
      synchronize();
      continue;
    }
    advance();
    field.type = parseTypeAnnotation();
    if (!field.type.isValid()) {
      synchronize();
      continue;
    }
    declaration->fields.push_back(std::move(field));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }

  if (check(TokenKind::RBrace)) {
    advance();
  } else if (!depthLimitExceeded_) {
    error(current().span, "expected '}' to close the fields of struct '" + declaration->name + "'");
  }
  if (declaration->fields.empty()) {
    error(declaration->nameSpan, "struct '" + declaration->name + "' must declare at least one field");
  }
  return declaration;
}

ast::TypeSyntax Parser::parseTypeAnnotation() {
  DepthGuard guard(*this);
  if (depthExceeded("type")) {
    return ast::TypeSyntax{};
  }
  ast::TypeSyntax type;

  if (check(TokenKind::LBracket)) {
    const Token& open = advance();
    type.kind = ast::TypeSyntax::Kind::Array;
    type.span = open.span;
    type.element = std::make_unique<ast::TypeSyntax>(parseTypeAnnotation());
    if (!type.element->isValid()) {
      return ast::TypeSyntax{};
    }
    if (!check(TokenKind::Semicolon)) {
      error(current().span, std::string("expected ';' and a length in an array type, found ") +
                                tokenKindName(current().kind));
      return ast::TypeSyntax{};
    }
    advance();
    if (!check(TokenKind::IntegerLiteral)) {
      error(current().span, std::string("expected an array length, found ") +
                                tokenKindName(current().kind));
      return ast::TypeSyntax{};
    }
    const Token& length = advance();
    if (length.integerValue == 0) {
      error(length.span, "array length must be greater than zero");
      return ast::TypeSyntax{};
    }
    type.arrayLength = length.integerValue;
    type.lengthSpan = length.span;
    if (!check(TokenKind::RBracket)) {
      error(current().span, std::string("expected ']' to close an array type, found ") +
                                tokenKindName(current().kind));
      return ast::TypeSyntax{};
    }
    const Token& close = advance();
    type.span = SourceSpan{open.span.begin, close.span.end};
    return type;
  }

  // Identifiers are accepted here on purpose: semantic analysis reports them as
  // unknown types, which is a more useful diagnostic than a syntax error.
  if (!isTypeKeyword(current().kind) && !check(TokenKind::Identifier)) {
    error(current().span,
          std::string("expected a type name, found ") + tokenKindName(current().kind));
    return ast::TypeSyntax{};
  }
  const Token& token = advance();
  type.kind = ast::TypeSyntax::Kind::Named;
  type.name = token.text;
  type.span = token.span;
  return type;
}

std::vector<std::unique_ptr<ast::Stmt>> Parser::parseBlock() {
  DepthGuard guard(*this);
  if (depthExceeded("block")) {
    return {};
  }
  expect(TokenKind::LBrace, "to begin a block");
  std::vector<std::unique_ptr<ast::Stmt>> statements;
  while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile) && !depthLimitExceeded_) {
    auto statement = parseStatement();
    if (statement) {
      statements.push_back(std::move(statement));
    }
  }
  if (check(TokenKind::RBrace)) {
    advance();
  } else if (!depthLimitExceeded_) {
    error(current().span, "expected '}' to close the block");
  }
  return statements;
}

std::unique_ptr<ast::Stmt> Parser::parseStatement() {
  if (depthLimitExceeded_) {
    return nullptr;
  }
  // A statement can contain another statement without passing through a block,
  // as `else if` chains do, so statement nesting needs its own guard: otherwise
  // the parser, the analyser, and the destructor all recurse once per link.
  DepthGuard guard(*this);
  if (depthExceeded("statement")) {
    return nullptr;
  }
  if (!consumeNodeBudget()) {
    return nullptr;
  }
  switch (current().kind) {
    case TokenKind::KwLet:
      return parseLetStatement(false);
    case TokenKind::KwVar:
      return parseLetStatement(true);
    case TokenKind::KwReturn:
      return parseReturnStatement();
    case TokenKind::KwIf:
      return parseIfStatement();
    case TokenKind::KwWhile:
      return parseWhileStatement();
    case TokenKind::KwFor:
      return parseForStatement();
    case TokenKind::KwBreak: {
      const Token& token = advance();
      auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Break, token.span);
      expect(TokenKind::Semicolon, "after 'break'");
      statement->span = spanFrom(token);
      return statement;
    }
    case TokenKind::KwContinue: {
      const Token& token = advance();
      auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Continue, token.span);
      expect(TokenKind::Semicolon, "after 'continue'");
      statement->span = spanFrom(token);
      return statement;
    }
    case TokenKind::LBrace: {
      const Token& begin = current();
      auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Block, begin.span);
      statement->body = parseBlock();
      statement->span = spanFrom(begin);
      return statement;
    }
    case TokenKind::Semicolon: {
      // An empty statement is accepted and has no effect.
      const Token& token = advance();
      return std::make_unique<ast::Stmt>(ast::Stmt::Kind::Block, token.span);
    }
    case TokenKind::KwFn:
      error(current().span, "nested function declarations are not supported by Gable");
      synchronize();
      advance();
      return nullptr;
    case TokenKind::KwStruct:
      error(current().span, "struct declarations are only allowed at the top level of a file");
      synchronize();
      advance();
      return nullptr;
    case TokenKind::KwImport:
      error(current().span, "imports must appear before any function or struct declaration");
      synchronize();
      advance();
      return nullptr;
    default:
      break;
  }

  const Token& begin = current();
  ast::ExprPtr expression = parseExpression(0);
  if (!expression) {
    synchronize();
    return nullptr;
  }

  if (check(TokenKind::Assign)) {
    advance();
    auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Assign, begin.span);
    statement->target = std::move(expression);
    statement->value = parseExpression(0);
    if (!statement->value) {
      synchronize();
      return nullptr;
    }
    expect(TokenKind::Semicolon, "after assignment");
    statement->span = spanFrom(begin);
    return statement;
  }

  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::ExprStmt, begin.span);
  statement->value = std::move(expression);
  expect(TokenKind::Semicolon, "after expression");
  statement->span = spanFrom(begin);
  return statement;
}

std::unique_ptr<ast::Stmt> Parser::parseLetStatement(bool isMutable) {
  const Token& begin = advance();  // 'let' or 'var'
  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Let, begin.span);
  statement->isMutable = isMutable;

  if (!check(TokenKind::Identifier)) {
    error(current().span, std::string("expected a variable name after '") +
                              (isMutable ? "var" : "let") + "', found " +
                              tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  const Token& nameToken = advance();
  statement->name = nameToken.text;
  statement->nameSpan = nameToken.span;

  if (match(TokenKind::Colon)) {
    statement->typeAnnotation = parseTypeAnnotation();
    if (!statement->typeAnnotation.isValid()) {
      synchronize();
      return nullptr;
    }
    statement->hasTypeAnnotation = true;
  }

  if (!check(TokenKind::Assign)) {
    error(current().span, std::string("binding '") + statement->name +
                              "' must be initialized; expected '=' and an initial value");
    synchronize();
    return nullptr;
  }
  advance();
  statement->value = parseExpression(0);
  if (!statement->value) {
    synchronize();
    return nullptr;
  }
  expect(TokenKind::Semicolon, "after variable declaration");
  statement->span = spanFrom(begin);
  return statement;
}

std::unique_ptr<ast::Stmt> Parser::parseReturnStatement() {
  const Token& begin = advance();  // 'return'
  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Return, begin.span);
  if (!check(TokenKind::Semicolon)) {
    statement->value = parseExpression(0);
    if (!statement->value) {
      synchronize();
      return nullptr;
    }
  }
  expect(TokenKind::Semicolon, "after return statement");
  statement->span = spanFrom(begin);
  return statement;
}

std::unique_ptr<ast::Stmt> Parser::parseIfStatement() {
  // `else if` recurses through this function rather than through parseStatement,
  // so the chain needs a guard of its own as well.
  DepthGuard guard(*this);
  if (depthExceeded("if/else chain")) {
    return nullptr;
  }
  const Token& begin = advance();  // 'if'
  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::If, begin.span);

  statement->value = parseExpression(0);
  if (!statement->value) {
    synchronize();
    return nullptr;
  }

  if (!check(TokenKind::LBrace)) {
    error(current().span,
          "expected '{' after the condition of an 'if' statement; Gable requires braced bodies");
    synchronize();
    return nullptr;
  }
  statement->body = parseBlock();

  if (match(TokenKind::KwElse)) {
    statement->hasElse = true;
    if (check(TokenKind::KwIf)) {
      auto nested = parseIfStatement();
      if (!nested) {
        return nullptr;
      }
      statement->elseBody.push_back(std::move(nested));
    } else if (check(TokenKind::LBrace)) {
      statement->elseBody = parseBlock();
    } else {
      error(current().span,
            "expected '{' or 'if' after 'else'; Gable requires braced bodies");
      synchronize();
      return nullptr;
    }
  }

  statement->span = spanFrom(begin);
  return statement;
}

std::unique_ptr<ast::Stmt> Parser::parseWhileStatement() {
  const Token& begin = advance();  // 'while'
  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::While, begin.span);

  statement->value = parseExpression(0);
  if (!statement->value) {
    synchronize();
    return nullptr;
  }

  if (!check(TokenKind::LBrace)) {
    error(current().span,
          "expected '{' after the condition of a 'while' statement; Gable requires braced bodies");
    synchronize();
    return nullptr;
  }
  statement->body = parseBlock();
  statement->span = spanFrom(begin);
  return statement;
}

std::unique_ptr<ast::Stmt> Parser::parseForStatement() {
  const Token& begin = advance();  // 'for'
  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::For, begin.span);

  if (!check(TokenKind::Identifier)) {
    error(current().span, std::string("expected a loop variable name after 'for', found ") +
                              tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  const Token& variable = advance();
  statement->loopVariable = variable.text;
  statement->loopVariableSpan = variable.span;

  if (!check(TokenKind::KwIn)) {
    error(current().span, std::string("expected 'in' after the loop variable, found ") +
                              tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  advance();

  statement->value = parseExpression(0);
  if (!statement->value) {
    synchronize();
    return nullptr;
  }
  if (statement->value->kind == ast::Expr::Kind::Range) {
    statement->isRangeLoop = true;
    statement->rangeEnd = std::move(statement->value->rhs);
    statement->value = std::move(statement->value->lhs);
  }

  if (!check(TokenKind::LBrace)) {
    error(current().span,
          "expected '{' after the header of a 'for' statement; Gable requires braced bodies");
    synchronize();
    return nullptr;
  }
  statement->body = parseBlock();
  statement->span = spanFrom(begin);
  return statement;
}

int Parser::precedenceOf(TokenKind kind) const {
  switch (kind) {
    case TokenKind::PipePipe: return 1;
    case TokenKind::AmpAmp: return 2;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual: return 3;
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual: return 4;
    case TokenKind::Plus:
    case TokenKind::Minus: return 5;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent: return 6;
    default: return 0;
  }
}

BinaryOp Parser::binaryOpFor(TokenKind kind) const {
  switch (kind) {
    case TokenKind::Plus: return BinaryOp::Add;
    case TokenKind::Minus: return BinaryOp::Subtract;
    case TokenKind::Star: return BinaryOp::Multiply;
    case TokenKind::Slash: return BinaryOp::Divide;
    case TokenKind::Percent: return BinaryOp::Remainder;
    case TokenKind::EqualEqual: return BinaryOp::Equal;
    case TokenKind::BangEqual: return BinaryOp::NotEqual;
    case TokenKind::Less: return BinaryOp::Less;
    case TokenKind::LessEqual: return BinaryOp::LessEqual;
    case TokenKind::Greater: return BinaryOp::Greater;
    case TokenKind::GreaterEqual: return BinaryOp::GreaterEqual;
    case TokenKind::AmpAmp: return BinaryOp::LogicalAnd;
    case TokenKind::PipePipe: return BinaryOp::LogicalOr;
    default: return BinaryOp::Add;
  }
}

ast::ExprPtr Parser::parseExpression(int minPrecedence) {
  DepthGuard guard(*this);
  if (depthExceeded("expression") || !consumeNodeBudget()) {
    return nullptr;
  }
  ast::ExprPtr left = parsePrefix();
  if (!left) {
    return nullptr;
  }
  // `expressionDepth_` now holds the depth of `left`. Each operator parsed
  // below wraps it in one more node, so the depth grows with every iteration of
  // the loop, not with the recursion of the parser.
  int treeDepth = expressionDepth_;

  for (;;) {
    const TokenKind kind = current().kind;
    if (kind == TokenKind::DotDot && minPrecedence <= 1) {
      // A range is only meaningful in a `for` header; it is parsed here so that
      // the error for `0..10` elsewhere points at the range.
      const Token& opToken = advance();
      ast::ExprPtr right = parseExpression(2);
      if (!right) {
        return nullptr;
      }
      if (!growTreeDepth(std::max(treeDepth, expressionDepth_) + 1, opToken.span)) {
        return nullptr;
      }
      treeDepth = std::max(treeDepth, expressionDepth_) + 1;
      auto range = std::make_unique<ast::Expr>(ast::Expr::Kind::Range, opToken.span);
      range->span = SourceSpan{left->span.begin, right->span.end};
      range->lhs = std::move(left);
      range->rhs = std::move(right);
      left = std::move(range);
      continue;
    }
    const int precedence = precedenceOf(kind);
    if (precedence == 0 || precedence < minPrecedence) {
      break;
    }
    const Token& opToken = advance();
    ast::ExprPtr right = parseExpression(precedence + 1);
    if (!right) {
      return nullptr;
    }
    if (!growTreeDepth(std::max(treeDepth, expressionDepth_) + 1, opToken.span)) {
      return nullptr;
    }
    treeDepth = std::max(treeDepth, expressionDepth_) + 1;
    auto binary = std::make_unique<ast::Expr>(ast::Expr::Kind::Binary, opToken.span);
    binary->binaryOp = binaryOpFor(kind);
    binary->span = SourceSpan{left->span.begin, right->span.end};
    binary->lhs = std::move(left);
    binary->rhs = std::move(right);
    left = std::move(binary);
  }
  expressionDepth_ = treeDepth;
  return left;
}

bool Parser::growTreeDepth(int depth, SourceSpan span) {
  if (depth <= kMaxNestingDepth) {
    return true;
  }
  return limitReached(span, "expression is too large (limit " +
                                std::to_string(kMaxNestingDepth) +
                                " levels of operators and operands); split it into "
                                "smaller statements");
}

ast::ExprPtr Parser::parsePrefix() {
  const Token& token = current();
  switch (token.kind) {
    case TokenKind::Minus:
    case TokenKind::Bang: {
      advance();
      ast::ExprPtr operand = parseExpression(7);
      if (!operand) {
        return nullptr;
      }
      if (!growTreeDepth(expressionDepth_ + 1, token.span)) {
        return nullptr;
      }
      expressionDepth_ += 1;
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::Unary, token.span);
      expr->unaryOp = token.kind == TokenKind::Minus ? UnaryOp::Negate : UnaryOp::LogicalNot;
      expr->span = SourceSpan{token.span.begin, operand->span.end};
      expr->lhs = std::move(operand);
      return expr;
    }
    case TokenKind::Amp:
    case TokenKind::Pipe:
      error(token.span, std::string("operator '") + token.text +
                            "' is reserved for a future Gable release and is not supported");
      advance();
      synchronize();
      return nullptr;
    default:
      return parsePrimary();
  }
}

ast::ExprPtr Parser::parsePrimary() {
  const Token& token = current();
  switch (token.kind) {
    case TokenKind::IntegerLiteral: {
      advance();
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::IntegerLiteral, token.span);
      expr->integerValue = token.integerValue;
      expr->unsignedLiteral = token.unsignedLiteral;
      expressionDepth_ = 1;
      return parsePostfix(std::move(expr));
    }
    case TokenKind::StringLiteral: {
      advance();
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::StringLiteral, token.span);
      expr->stringValue = token.stringValue;
      expressionDepth_ = 1;
      return parsePostfix(std::move(expr));
    }
    case TokenKind::KwTrue:
    case TokenKind::KwFalse: {
      advance();
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::BoolLiteral, token.span);
      expr->boolValue = token.kind == TokenKind::KwTrue;
      expressionDepth_ = 1;
      return parsePostfix(std::move(expr));
    }
    case TokenKind::Identifier: {
      advance();
      if (!check(TokenKind::LParen)) {
        auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::Name, token.span);
        expr->name = token.text;
        expressionDepth_ = 1;
        return parsePostfix(std::move(expr));
      }
      advance();  // '('
      auto call = std::make_unique<ast::Expr>(ast::Expr::Kind::Call, token.span);
      call->name = token.text;
      int argumentDepth = 0;
      if (!check(TokenKind::RParen)) {
        for (;;) {
          ast::ExprPtr argument = parseExpression(0);
          if (!argument) {
            return nullptr;
          }
          argumentDepth = std::max(argumentDepth, expressionDepth_);
          call->arguments.push_back(std::move(argument));
          if (match(TokenKind::Comma)) {
            continue;
          }
          break;
        }
      }
      if (!check(TokenKind::RParen)) {
        error(current().span, "expected ')' to close the argument list of '" + token.text + "'");
        return nullptr;
      }
      const Token& close = advance();
      call->span = SourceSpan{token.span.begin, close.span.end};
      if (!growTreeDepth(argumentDepth + 1, token.span)) {
        return nullptr;
      }
      expressionDepth_ = argumentDepth + 1;
      return parsePostfix(std::move(call));
    }
    case TokenKind::LBracket: {
      advance();
      auto literal = std::make_unique<ast::Expr>(ast::Expr::Kind::ArrayLiteral, token.span);
      int elementDepth = 0;
      if (!check(TokenKind::RBracket)) {
        for (;;) {
          ast::ExprPtr element = parseExpression(0);
          if (!element) {
            return nullptr;
          }
          elementDepth = std::max(elementDepth, expressionDepth_);
          literal->arguments.push_back(std::move(element));
          if (match(TokenKind::Comma)) {
            if (check(TokenKind::RBracket)) {
              break;  // trailing comma
            }
            continue;
          }
          break;
        }
      }
      if (!check(TokenKind::RBracket)) {
        error(current().span, "expected ']' to close the array literal");
        return nullptr;
      }
      const Token& close = advance();
      literal->span = SourceSpan{token.span.begin, close.span.end};
      if (!growTreeDepth(elementDepth + 1, token.span)) {
        return nullptr;
      }
      expressionDepth_ = elementDepth + 1;
      return parsePostfix(std::move(literal));
    }
    case TokenKind::LParen: {
      advance();
      ast::ExprPtr inner = parseExpression(0);
      if (!inner) {
        return nullptr;
      }
      if (!check(TokenKind::RParen)) {
        error(current().span, "expected ')' to close the parenthesized expression");
        return nullptr;
      }
      advance();
      return parsePostfix(std::move(inner));
    }
    case TokenKind::EndOfFile:
      error(token.span, "expected an expression, found end of file");
      return nullptr;
    default:
      error(token.span,
            std::string("expected an expression, found ") + tokenKindName(token.kind));
      return nullptr;
  }
}

ast::ExprPtr Parser::parsePostfix(ast::ExprPtr base) {
  // `expressionDepth_` describes `base` on entry and is updated as each postfix
  // operator is applied, so a long `a.b[i].c` chain is bounded as well.
  int baseDepth = expressionDepth_;
  for (;;) {
    if (check(TokenKind::Dot)) {
      advance();
      if (!check(TokenKind::Identifier)) {
        error(current().span, std::string("expected a field name after '.', found ") +
                                  tokenKindName(current().kind));
        return nullptr;
      }
      const Token& field = advance();
      if (!growTreeDepth(baseDepth + 1, field.span)) {
        return nullptr;
      }
      baseDepth += 1;
      expressionDepth_ = baseDepth;
      auto access = std::make_unique<ast::Expr>(ast::Expr::Kind::FieldAccess, field.span);
      access->span = SourceSpan{base->span.begin, field.span.end};
      access->fieldName = field.text;
      access->fieldSpan = field.span;
      access->lhs = std::move(base);
      base = std::move(access);
      continue;
    }
    if (check(TokenKind::LBracket)) {
      const Token& open = advance();
      ast::ExprPtr index = parseExpression(0);
      if (!index) {
        return nullptr;
      }
      if (!check(TokenKind::RBracket)) {
        error(current().span, "expected ']' after the index expression");
        return nullptr;
      }
      const Token& close = advance();
      if (!growTreeDepth(std::max(baseDepth, expressionDepth_) + 1, open.span)) {
        return nullptr;
      }
      baseDepth = std::max(baseDepth, expressionDepth_) + 1;
      expressionDepth_ = baseDepth;
      auto access = std::make_unique<ast::Expr>(ast::Expr::Kind::Index, open.span);
      access->span = SourceSpan{base->span.begin, close.span.end};
      access->lhs = std::move(base);
      access->rhs = std::move(index);
      base = std::move(access);
      continue;
    }
    if (check(TokenKind::KwAs)) {
      const Token& keyword = advance();
      auto cast = std::make_unique<ast::Expr>(ast::Expr::Kind::Cast, keyword.span);
      cast->targetType = parseTypeAnnotation();
      if (!cast->targetType.isValid()) {
        return nullptr;
      }
      if (!growTreeDepth(baseDepth + 1, keyword.span)) {
        return nullptr;
      }
      baseDepth += 1;
      expressionDepth_ = baseDepth;
      cast->span = SourceSpan{base->span.begin, cast->targetType.span.end};
      cast->lhs = std::move(base);
      base = std::move(cast);
      continue;
    }
    return base;
  }
}

}  // namespace gable
