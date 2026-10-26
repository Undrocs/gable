#include "gable/parser.h"

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

/// Maximum nesting depth for expressions and blocks. Well beyond anything
/// readable, low enough to keep the recursive-descent parser's stack bounded.
constexpr int kMaxNestingDepth = 200;

}  // namespace

Parser::Parser(const std::vector<Token>& tokens, DiagnosticEngine& diagnostics)
    : tokens_(tokens), diagnostics_(diagnostics) {}

Parser::DepthGuard::DepthGuard(Parser& owner) : parser(owner) { ++parser.depth_; }

Parser::DepthGuard::~DepthGuard() { --parser.depth_; }

bool Parser::depthExceeded(const char* context) {
  if (depth_ <= kMaxNestingDepth) {
    return false;
  }
  if (!depthLimitExceeded_) {
    depthLimitExceeded_ = true;
    diagnostics_.error(current().span, std::string(context) + " is nested too deeply (limit " +
                                           std::to_string(kMaxNestingDepth) + " levels)");
  }
  return true;
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
  diagnostics_.error(found.span, std::string("expected ") + tokenKindName(kind) + " " + context +
                                     ", found " + tokenKindName(found.kind));
  return found;
}

ast::Program Parser::parseProgram() {
  ast::Program program;
  while (!check(TokenKind::EndOfFile) && !depthLimitExceeded_) {
    if (check(TokenKind::KwFn)) {
      auto function = parseFunction();
      if (function) {
        program.functions.push_back(std::move(function));
      }
      continue;
    }
    const Token& unexpected = current();
    diagnostics_.error(unexpected.span,
                       std::string("expected a function declaration ('fn'), found ") +
                           tokenKindName(unexpected.kind));
    advance();
    synchronize();
  }
  return program;
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
    if (check(TokenKind::KwFn) || check(TokenKind::KwLet) || check(TokenKind::KwVar) ||
        check(TokenKind::KwReturn) || check(TokenKind::KwIf) || check(TokenKind::KwWhile)) {
      return;
    }
    advance();
  }
}

std::unique_ptr<ast::FunctionDecl> Parser::parseFunction() {
  const Token& fnToken = advance();  // 'fn'
  auto function = std::make_unique<ast::FunctionDecl>();
  function->span = fnToken.span;

  if (!check(TokenKind::Identifier)) {
    diagnostics_.error(current().span, std::string("expected a function name, found ") +
                                           tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  const Token& nameToken = advance();
  function->name = nameToken.text;
  function->nameSpan = nameToken.span;

  if (!check(TokenKind::LParen)) {
    diagnostics_.error(current().span,
                       std::string("expected '(' after function name '") + function->name +
                           "', found " + tokenKindName(current().kind));
    synchronize();
    return nullptr;
  }
  advance();

  if (!check(TokenKind::RParen)) {
    for (;;) {
      if (!check(TokenKind::Identifier)) {
        diagnostics_.error(current().span,
                           "expected a parameter name, found " + std::string(tokenKindName(current().kind)));
        synchronize();
        return nullptr;
      }
      ast::Parameter parameter;
      const Token& parameterName = advance();
      parameter.name = parameterName.text;
      parameter.span = parameterName.span;

      if (!check(TokenKind::Colon)) {
        diagnostics_.error(current().span, "expected ':' and a type after parameter name '" +
                                               parameter.name + "'");
        synchronize();
        return nullptr;
      }
      advance();
      parameter.type = parseTypeAnnotation();
      if (parameter.type.name.empty()) {
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
    diagnostics_.error(current().span, "expected ')' to close the parameter list of '" +
                                           function->name + "'");
    synchronize();
    return nullptr;
  }
  advance();

  if (match(TokenKind::Arrow)) {
    function->returnType = parseTypeAnnotation();
    function->hasReturnType = !function->returnType.name.empty();
    if (!function->hasReturnType) {
      synchronize();
      return nullptr;
    }
  }

  if (!check(TokenKind::LBrace)) {
    diagnostics_.error(current().span, "expected '{' to begin the body of function '" +
                                           function->name + "'");
    synchronize();
    return nullptr;
  }
  function->body = parseBlock();
  return function;
}

ast::TypeSyntax Parser::parseTypeAnnotation() {
  ast::TypeSyntax type;
  // Identifiers are accepted here on purpose: semantic analysis reports them as
  // unknown types, which is a more useful diagnostic than a syntax error.
  if (!isTypeKeyword(current().kind) && !check(TokenKind::Identifier)) {
    diagnostics_.error(current().span,
                       std::string("expected a type name, found ") + tokenKindName(current().kind));
    return type;
  }
  const Token& token = advance();
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
    diagnostics_.error(current().span, "expected '}' to close the block");
  }
  return statements;
}

std::unique_ptr<ast::Stmt> Parser::parseStatement() {
  if (depthLimitExceeded_) {
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
      diagnostics_.error(current().span,
                         "nested function declarations are not supported by Gable v0.1");
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
    if (expression->kind != ast::Expr::Kind::Name) {
      diagnostics_.error(expression->span, "invalid assignment target; expected a variable name");
      synchronize();
      return nullptr;
    }
    auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Assign, begin.span);
    statement->name = expression->name;
    statement->nameSpan = expression->span;
    statement->expression = parseExpression(0);
    statement->span = spanFrom(begin);
    if (!statement->expression) {
      synchronize();
      return nullptr;
    }
    expect(TokenKind::Semicolon, "after assignment");
    statement->span = spanFrom(begin);
    return statement;
  }

  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::ExprStmt, begin.span);
  statement->expression = std::move(expression);
  expect(TokenKind::Semicolon, "after expression");
  statement->span = spanFrom(begin);
  return statement;
}

std::unique_ptr<ast::Stmt> Parser::parseLetStatement(bool isMutable) {
  const Token& begin = advance();  // 'let' or 'var'
  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::Let, begin.span);
  statement->isMutable = isMutable;

  if (!check(TokenKind::Identifier)) {
    diagnostics_.error(current().span, std::string("expected a variable name after '") +
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
    if (statement->typeAnnotation.name.empty()) {
      synchronize();
      return nullptr;
    }
    statement->hasTypeAnnotation = true;
  }

  if (!check(TokenKind::Assign)) {
    diagnostics_.error(current().span,
                       std::string("binding '") + statement->name +
                           "' must be initialized; expected '=' and an initial value");
    synchronize();
    return nullptr;
  }
  advance();
  statement->expression = parseExpression(0);
  if (!statement->expression) {
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
    statement->expression = parseExpression(0);
    if (!statement->expression) {
      synchronize();
      return nullptr;
    }
  }
  expect(TokenKind::Semicolon, "after return statement");
  statement->span = spanFrom(begin);
  return statement;
}

std::unique_ptr<ast::Stmt> Parser::parseIfStatement() {
  const Token& begin = advance();  // 'if'
  auto statement = std::make_unique<ast::Stmt>(ast::Stmt::Kind::If, begin.span);

  statement->expression = parseExpression(0);
  if (!statement->expression) {
    synchronize();
    return nullptr;
  }

  if (!check(TokenKind::LBrace)) {
    diagnostics_.error(current().span,
                       "expected '{' after the condition of an 'if' statement; Gable requires "
                       "braced bodies");
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
      diagnostics_.error(current().span,
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

  statement->expression = parseExpression(0);
  if (!statement->expression) {
    synchronize();
    return nullptr;
  }

  if (!check(TokenKind::LBrace)) {
    diagnostics_.error(current().span,
                       "expected '{' after the condition of a 'while' statement; Gable requires "
                       "braced bodies");
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
  if (depthExceeded("expression")) {
    return nullptr;
  }
  ast::ExprPtr left = parsePrefix();
  if (!left) {
    return nullptr;
  }

  for (;;) {
    const TokenKind kind = current().kind;
    const int precedence = precedenceOf(kind);
    if (precedence == 0 || precedence < minPrecedence) {
      break;
    }
    const Token& opToken = advance();
    ast::ExprPtr right = parseExpression(precedence + 1);
    if (!right) {
      return nullptr;
    }
    auto binary = std::make_unique<ast::Expr>(ast::Expr::Kind::Binary, opToken.span);
    binary->binaryOp = binaryOpFor(kind);
    binary->span = SourceSpan{left->span.begin, right->span.end};
    binary->lhs = std::move(left);
    binary->rhs = std::move(right);
    left = std::move(binary);
  }
  return left;
}

ast::ExprPtr Parser::parsePrefix() {
  const Token& token = current();
  switch (token.kind) {
    case TokenKind::Minus: {
      advance();
      ast::ExprPtr operand = parseExpression(7);
      if (!operand) {
        return nullptr;
      }
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::Unary, token.span);
      expr->unaryOp = UnaryOp::Negate;
      expr->span = SourceSpan{token.span.begin, operand->span.end};
      expr->lhs = std::move(operand);
      return expr;
    }
    case TokenKind::Bang: {
      advance();
      ast::ExprPtr operand = parseExpression(7);
      if (!operand) {
        return nullptr;
      }
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::Unary, token.span);
      expr->unaryOp = UnaryOp::LogicalNot;
      expr->span = SourceSpan{token.span.begin, operand->span.end};
      expr->lhs = std::move(operand);
      return expr;
    }
    case TokenKind::Amp:
    case TokenKind::Pipe:
      diagnostics_.error(token.span, std::string("operator '") + token.text +
                                         "' is reserved for a future Gable release and is not "
                                         "supported in v0.1");
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
      return expr;
    }
    case TokenKind::StringLiteral: {
      advance();
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::StringLiteral, token.span);
      expr->stringValue = token.stringValue;
      return expr;
    }
    case TokenKind::KwTrue:
    case TokenKind::KwFalse: {
      advance();
      auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::BoolLiteral, token.span);
      expr->boolValue = token.kind == TokenKind::KwTrue;
      return expr;
    }
    case TokenKind::Identifier: {
      advance();
      if (!check(TokenKind::LParen)) {
        auto expr = std::make_unique<ast::Expr>(ast::Expr::Kind::Name, token.span);
        expr->name = token.text;
        return expr;
      }
      advance();  // '('
      auto call = std::make_unique<ast::Expr>(ast::Expr::Kind::Call, token.span);
      call->name = token.text;
      if (!check(TokenKind::RParen)) {
        for (;;) {
          ast::ExprPtr argument = parseExpression(0);
          if (!argument) {
            return nullptr;
          }
          call->arguments.push_back(std::move(argument));
          if (match(TokenKind::Comma)) {
            continue;
          }
          break;
        }
      }
      if (!check(TokenKind::RParen)) {
        diagnostics_.error(current().span,
                           "expected ')' to close the argument list of '" + token.text + "'");
        return nullptr;
      }
      const Token& close = advance();
      call->span = SourceSpan{token.span.begin, close.span.end};
      return call;
    }
    case TokenKind::LParen: {
      advance();
      ast::ExprPtr inner = parseExpression(0);
      if (!inner) {
        return nullptr;
      }
      if (!check(TokenKind::RParen)) {
        diagnostics_.error(current().span, "expected ')' to close the parenthesized expression");
        return nullptr;
      }
      advance();
      return inner;
    }
    case TokenKind::EndOfFile:
      diagnostics_.error(token.span, "expected an expression, found end of file");
      return nullptr;
    default:
      diagnostics_.error(token.span,
                         std::string("expected an expression, found ") + tokenKindName(token.kind));
      return nullptr;
  }
}

}  // namespace gable
