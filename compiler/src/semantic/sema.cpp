#include "gable/sema.h"

#include <algorithm>
#include <utility>

namespace gable {
namespace {

/// True if an integer literal with `magnitude` is representable in `type`.
/// `negate` indicates that the literal is the operand of a unary minus, which
/// makes one additional value representable for signed types (the minimum).
bool literalFits(ir::Type type, std::uint64_t magnitude, bool negate) {
  const unsigned bits = type.bitWidth();
  if (type.isSigned()) {
    const std::uint64_t limit =
        (bits >= 64) ? (1ull << 63) : (1ull << (bits - 1));
    return negate ? magnitude <= limit : magnitude <= limit - 1;
  }
  if (negate) {
    return false;
  }
  if (bits >= 64) {
    return true;
  }
  return magnitude <= ((1ull << bits) - 1);
}

/// Two's-complement bit pattern of the given signed magnitude, narrowed to the
/// destination type's width by the caller when it emits the value.
std::uint64_t literalBits(std::uint64_t magnitude, bool negate) {
  return negate ? (0ull - magnitude) : magnitude;
}

bool isPrintName(const std::string& name) { return name == "print" || name == "eprint"; }

/// The built-in function that a method name stands for. The receiver becomes its
/// first argument, so the two spellings cannot drift apart.
std::string_view methodToBuiltin(const std::string& method) {
  if (method == "trim") return "str_trim";
  if (method == "split") return "str_split";
  if (method == "slice") return "str_slice";
  if (method == "find") return "str_find";
  if (method == "join") return "str_join";
  return {};
}

/// Depth-first search over struct fields looking for a value-containment cycle.
bool containsItself(ir::Type type, const ir::Module& module, std::vector<bool>& onPath,
                    std::vector<bool>& done) {
  if (type.kind() == ir::TypeKind::Array) {
    return containsItself(module.types.element(type), module, onPath, done);
  }
  if (type.kind() != ir::TypeKind::Struct) {
    return false;
  }
  const std::size_t index = type.index();
  if (index >= module.structs.size()) {
    return false;
  }
  if (onPath[index]) {
    return true;
  }
  if (done[index]) {
    return false;
  }
  onPath[index] = true;
  for (const ir::StructField& field : module.structs[index]->fields) {
    if (containsItself(field.type, module, onPath, done)) {
      return true;
    }
  }
  onPath[index] = false;
  done[index] = true;
  return false;
}

}  // namespace

SemanticAnalyzer::SemanticAnalyzer(Diagnostics& diagnostics) : diagnostics_(diagnostics) {}

std::unique_ptr<ir::Module> SemanticAnalyzer::analyze(
    const ast::Program& prelude, const std::vector<const ast::Program*>& programs) {
  module_ = std::make_unique<ir::Module>();

  // Every declaration of every file is collected before any body is checked, so
  // functions and structs can be used in any order and across files.
  registerStructs(prelude, true);
  for (const ast::Program* program : programs) {
    registerStructs(*program, false);
  }
  resolveStructFields();
  registerBuiltins();
  registerFunctions(prelude);
  for (const ast::Program* program : programs) {
    registerFunctions(*program);
  }

  for (const ast::Program* program : programs) {
    for (const auto& declaration : program->functions) {
      const auto it = functions_.find(declaration->name);
      if (it == functions_.end() || it->second.declaration != declaration.get()) {
        continue;  // duplicate or rejected declaration
      }
      checkFunction(*declaration, it->second, *module_->functions[it->second.index]);
    }
  }

  checkEntryPoint();
  return std::move(module_);
}

void SemanticAnalyzer::registerStructs(const ast::Program& program, bool fromPrelude) {
  currentFile_ = 0;
  for (const auto& declaration : program.structs) {
    if (ir::Type::isScalarSourceName(declaration->name)) {
      diagnostics_.error(declaration->file, declaration->nameSpan,
                         "'" + declaration->name +
                             "' is a built-in type name and cannot be declared as a struct");
      continue;
    }
    const auto existing = structTypesByName_.find(declaration->name);
    if (existing != structTypesByName_.end()) {
      diagnostics_.error(declaration->file, declaration->nameSpan,
                         "duplicate declaration of struct '" + declaration->name + "'");
      const ir::StructInfo& previous = *module_->structs[existing->second.index()];
      diagnostics_.note(previous.file, previous.span, "previous declaration is here");
      continue;
    }

    auto info = std::make_unique<ir::StructInfo>();
    info->name = declaration->name;
    info->span = declaration->nameSpan;
    info->file = declaration->file;
    info->fromPrelude = fromPrelude;
    for (const ast::FieldDecl& field : declaration->fields) {
      info->fields.push_back(ir::StructField{field.name, ir::Type::error(), field.nameSpan});
    }

    const ir::Type type = ir::Type::structType(static_cast<std::uint32_t>(module_->structs.size()));
    ir::StructInfo* raw = info.get();
    structTypesByName_.emplace(declaration->name, type);
    module_->structs.push_back(std::move(info));
    pendingStructFields_.emplace_back(raw, declaration.get());
  }
}

void SemanticAnalyzer::resolveStructFields() {
  for (const auto& pending : pendingStructFields_) {
    ir::StructInfo& info = *pending.first;
    const ast::StructDecl& declaration = *pending.second;
    currentFile_ = declaration.file;

    for (std::size_t i = 0; i < declaration.fields.size(); ++i) {
      const ast::FieldDecl& field = declaration.fields[i];
      const ir::Type type = resolveType(field.type);
      info.fields[i].type = type;

      for (std::size_t j = 0; j < i; ++j) {
        if (declaration.fields[j].name == field.name) {
          diagnostics_.error(declaration.file, field.nameSpan,
                             "duplicate field '" + field.name + "' in struct '" + info.name + "'");
        }
      }
      if (type.kind() == ir::TypeKind::Void) {
        diagnostics_.error(declaration.file, field.type.span,
                           "field '" + field.name + "' cannot have type 'void'");
      }
    }
  }
  pendingStructFields_.clear();

  // A struct that contains itself by value would have infinite size, so the
  // cycle is rejected before layout is ever computed.
  std::vector<bool> onPath(module_->structs.size(), false);
  std::vector<bool> done(module_->structs.size(), false);
  for (std::size_t i = 0; i < module_->structs.size(); ++i) {
    std::fill(onPath.begin(), onPath.end(), false);
    if (containsItself(ir::Type::structType(static_cast<std::uint32_t>(i)), *module_, onPath,
                       done)) {
      const ir::StructInfo& info = *module_->structs[i];
      diagnostics_.error(info.file, info.span,
                         "struct '" + info.name + "' contains itself by value, which cannot be "
                         "given a size");
      diagnostics_.note(info.file, info.span,
                        "store a count and read the values from a file instead of nesting a "
                        "struct inside itself");
    }
  }
}

void SemanticAnalyzer::registerFunctions(const ast::Program& program) {
  for (const auto& declaration : program.functions) {
    currentFile_ = declaration->file;

    if (isPrintName(declaration->name) || findBuiltin(declaration->name) != nullptr) {
      diagnostics_.error(declaration->file, declaration->nameSpan,
                         "the name '" + declaration->name +
                             "' is reserved for a built-in function");
      continue;
    }

    const ir::Type returnType =
        declaration->hasReturnType ? resolveType(declaration->returnType) : ir::Type::voidType();

    std::vector<ir::Type> parameterTypes;
    bool signatureValid = true;
    for (const ast::Parameter& parameter : declaration->parameters) {
      const ir::Type type = resolveType(parameter.type);
      if (type.kind() == ir::TypeKind::Void) {
        diagnostics_.error(declaration->file, parameter.type.span,
                           "parameter '" + parameter.name + "' cannot have type 'void'");
        signatureValid = false;
      }
      parameterTypes.push_back(type);
    }
    if (!signatureValid) {
      continue;
    }

    const auto existing = functions_.find(declaration->name);
    if (existing != functions_.end()) {
      diagnostics_.error(declaration->file, declaration->nameSpan,
                         "duplicate declaration of function '" + declaration->name + "'");
      diagnostics_.note(existing->second.declaration->file,
                        existing->second.declaration->nameSpan,
                        "previous declaration of '" + declaration->name + "' is here");
      continue;
    }

    auto function = std::make_unique<ir::Function>();
    function->name = declaration->name;
    function->span = declaration->nameSpan;
    function->file = declaration->file;
    function->returnType = returnType;
    function->isEntryPoint = declaration->name == "main";

    FunctionInfo info;
    info.declaration = declaration.get();
    info.returnType = returnType;
    info.parameterTypes = std::move(parameterTypes);
    info.index = static_cast<std::uint32_t>(module_->functions.size());

    module_->functions.push_back(std::move(function));
    functions_.emplace(declaration->name, std::move(info));
  }
}

void SemanticAnalyzer::registerBuiltins() {
  const auto preludeStruct = [&](std::string_view name) {
    const auto it = structTypesByName_.find(std::string(name));
    return it == structTypesByName_.end() ? ir::Type::error() : it->second;
  };

  const ir::Type str = ir::Type::str();
  // Counts, sizes, and indices are `int` everywhere, so a length from `.len`, an
  // argument index, and a slice bound can be used together without conversions.
  const ir::Type intType = ir::Type::intType();

  builtins_ = {
      {"arg_count", ir::Builtin::ArgCount, intType, {}},
      {"arg", ir::Builtin::Arg, str, {intType}},
      {"fs_read_text", ir::Builtin::FsReadText, preludeStruct("FileReadResult"), {str}},
      {"fs_write_text",
       ir::Builtin::FsWriteText,
       preludeStruct("FileWriteResult"),
       {str, str}},
      {"fs_write_text_atomic",
       ir::Builtin::FsWriteTextAtomic,
       preludeStruct("FileWriteResult"),
       {str, str}},
      {"fs_lock",
       ir::Builtin::FsLock,
       preludeStruct("FileWriteResult"),
       {str}},
      {"fs_unlock",
       ir::Builtin::FsUnlock,
       preludeStruct("FileWriteResult"),
       {str}},
      {"str_of_i64", ir::Builtin::StrOfI64, str, {intType}},
      {"str_of_bool", ir::Builtin::StrOfBool, str, {ir::Type::boolean()}},
      {"parse_i64", ir::Builtin::ParseI64, preludeStruct("ParseResult"), {str}},
      {"str_find", ir::Builtin::Find, preludeStruct("FindResult"), {str, str}},
      {"str_slice", ir::Builtin::Slice, preludeStruct("SliceResult"), {str, intType, intType}},
      {"str_trim", ir::Builtin::Trim, str, {str}},
      // Splitting and joining are what makes a list of strings useful: reading
      // input into fields, and building output without repeated concatenation.
      {"str_split", ir::Builtin::Split, module_->types.list(str), {str, str}},
      {"str_join", ir::Builtin::Join, str, {module_->types.list(str), str}},
  };
}

const SemanticAnalyzer::BuiltinInfo* SemanticAnalyzer::findBuiltin(std::string_view name) const {
  for (const BuiltinInfo& builtin : builtins_) {
    if (builtin.name == name) {
      return &builtin;
    }
  }
  return nullptr;
}

void SemanticAnalyzer::checkEntryPoint() {
  const auto it = functions_.find("main");
  if (it == functions_.end()) {
    diagnostics_.error(0, SourceSpan{},
                       "no 'main' function found; an executable requires 'fn main()' or "
                       "'fn main() -> int'");
    return;
  }
  const ast::FunctionDecl& declaration = *it->second.declaration;
  if (!declaration.parameters.empty()) {
    diagnostics_.error(declaration.file, declaration.nameSpan,
                       "'main' must not take parameters; use arg_count() and arg(i) to read the "
                       "command line");
  }
  if (it->second.returnType != ir::Type::intType() &&
      it->second.returnType != ir::Type::voidType()) {
    diagnostics_.error(declaration.file,
                       declaration.hasReturnType ? declaration.returnType.span
                                                 : declaration.nameSpan,
                       "'main' must return 'int' (the exit status) or nothing at all, found '" +
                           ir::typeName(it->second.returnType, *module_) + "'");
    diagnostics_.note(declaration.file, declaration.nameSpan,
                      "write 'fn main()' to exit with status 0, or 'fn main() -> int' to "
                      "return a status");
  }
}

void SemanticAnalyzer::checkFunction(const ast::FunctionDecl& declaration,
                                    const FunctionInfo& info, ir::Function& out) {
  currentFunction_ = &out;
  currentFunctionName_ = declaration.name;
  currentReturnType_ = info.returnType;
  currentFile_ = declaration.file;
  loopDepth_ = 0;

  pushScope();

  for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
    const ast::Parameter& parameter = declaration.parameters[i];
    const ir::Type type = info.parameterTypes[i];

    if (lookup(parameter.name) != nullptr) {
      diagnostics_.error(declaration.file, parameter.span,
                         "duplicate parameter name '" + parameter.name + "'");
      continue;
    }
    if (isPrintName(parameter.name) || findBuiltin(parameter.name) != nullptr) {
      diagnostics_.error(declaration.file, parameter.span,
                         "the parameter name '" + parameter.name + "' is reserved for a built-in "
                         "function");
      continue;
    }

    const auto slot = static_cast<std::uint32_t>(out.locals.size());
    out.locals.push_back(ir::LocalVariable{parameter.name, type, parameter.span, false, true});
    out.parameterCount = static_cast<std::uint32_t>(out.locals.size());
    scopes_.back().emplace(
        parameter.name,
        Binding{slot, type, false, true, parameter.span, declaration.file});
  }

  out.body = checkStatements(declaration.body);

  if (currentReturnType_ != ir::Type::voidType() &&
      currentReturnType_.kind() != ir::TypeKind::Error && !alwaysReturns(out.body)) {
    diagnostics_.error(declaration.file, declaration.nameSpan,
                       "function '" + declaration.name +
                           "' can reach the end of its body without returning a value of type '" +
                           ir::typeName(currentReturnType_, *module_) + "'");
  }

  popScope();
  currentFunction_ = nullptr;
  currentFunctionName_.clear();
  currentReturnType_ = ir::Type::voidType();
}

std::vector<ir::StmtPtr> SemanticAnalyzer::checkStatements(
    const std::vector<ast::StmtPtr>& statements) {
  std::vector<ir::StmtPtr> lowered;
  lowered.reserve(statements.size());
  for (const auto& statement : statements) {
    ir::StmtPtr result = checkStmt(*statement);
    if (result) {
      lowered.push_back(std::move(result));
    }
  }
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkStmt(const ast::Stmt& stmt) {
  switch (stmt.kind) {
    case ast::Stmt::Kind::Let:
      return checkLet(stmt);
    case ast::Stmt::Kind::Assign:
      return checkAssign(stmt);
    case ast::Stmt::Kind::ExprStmt: {
      auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Expr, stmt.span);
      lowered->value = checkExpr(*stmt.value, std::nullopt);
      return lowered;
    }
    case ast::Stmt::Kind::Return:
      return checkReturn(stmt);
    case ast::Stmt::Kind::If:
      return checkIf(stmt);
    case ast::Stmt::Kind::While:
      return checkWhile(stmt);
    case ast::Stmt::Kind::For:
      return checkFor(stmt);
    case ast::Stmt::Kind::Break:
    case ast::Stmt::Kind::Continue:
      return checkBreakOrContinue(stmt);
    case ast::Stmt::Kind::Block:
      return checkBlock(stmt);
  }
  return nullptr;
}

ir::StmtPtr SemanticAnalyzer::checkLet(const ast::Stmt& stmt) {
  if (isPrintName(stmt.name) || findBuiltin(stmt.name) != nullptr) {
    diagnostics_.error(currentFile_, stmt.nameSpan,
                       "the name '" + stmt.name + "' is reserved for a built-in function");
    return nullptr;
  }
  if (scopes_.back().count(stmt.name) != 0) {
    diagnostics_.error(currentFile_, stmt.nameSpan,
                       "duplicate declaration of '" + stmt.name + "' in the same scope");
  }
  if (structTypesByName_.count(stmt.name) != 0) {
    diagnostics_.error(currentFile_, stmt.nameSpan,
                       "'" + stmt.name +
                           "' is a struct name; a variable needs a different name");
    return nullptr;
  }
  if (functions_.count(stmt.name) != 0) {
    diagnostics_.error(currentFile_, stmt.nameSpan,
                       "'" + stmt.name +
                           "' is a function name and cannot be redeclared as a variable");
    return nullptr;
  }

  ir::Type type = ir::Type::error();
  std::optional<ir::Type> expected;

  if (stmt.hasTypeAnnotation) {
    type = resolveType(stmt.typeAnnotation);
    if (type.kind() == ir::TypeKind::Void) {
      diagnostics_.error(currentFile_, stmt.typeAnnotation.span,
                         "a variable cannot have type 'void'");
      type = ir::Type::error();
    } else if (type.kind() != ir::TypeKind::Error) {
      expected = type;
    }
  }

  ir::ExprPtr initializer;
  if (stmt.value) {
    initializer = checkExpr(*stmt.value, expected);
    if (expected) {
      if (initializer->type.kind() != ir::TypeKind::Error &&
          initializer->type != *expected) {
        reportTypeMismatch(stmt.value->span, "initializer", *expected, initializer->type);
      }
    } else if (initializer->type.kind() == ir::TypeKind::Void) {
      diagnostics_.error(currentFile_, stmt.value->span,
                         "cannot initialize '" + stmt.name + "' with a value of type 'void'");
      return nullptr;
    } else {
      type = initializer->type;
    }
  } else if (!stmt.hasTypeAnnotation) {
    diagnostics_.error(currentFile_, stmt.nameSpan,
                       "binding '" + stmt.name +
                           "' needs a type annotation when it has no initializer");
    return nullptr;
  }

  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Let, stmt.span);
  lowered->slot = static_cast<std::uint32_t>(currentFunction_->locals.size());
  lowered->type = type;
  lowered->isMutable = stmt.isMutable;
  lowered->name = stmt.name;
  lowered->value = std::move(initializer);

  currentFunction_->locals.push_back(
      ir::LocalVariable{stmt.name, type, stmt.nameSpan, stmt.isMutable, false});
  // The binding becomes visible only after its initializer, so `let x = x;`
  // refers to an outer binding or reports an undefined name.
  scopes_.back().emplace(
      stmt.name, Binding{lowered->slot, type, stmt.isMutable, false, stmt.nameSpan, currentFile_});
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkAssign(const ast::Stmt& stmt) {
  ir::ExprPtr target = checkLvalue(*stmt.target);
  if (!target) {
    return nullptr;
  }

  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Assign, stmt.span);
  lowered->type = target->type;
  lowered->name = target->kind == ir::Expr::Kind::Local ? nameOfSlot(target->slot) : "";
  lowered->target = std::move(target);

  const bool known = lowered->type.kind() != ir::TypeKind::Error;
  lowered->value = checkExpr(
      *stmt.value, known ? std::optional<ir::Type>(lowered->type) : std::nullopt);
  if (known && lowered->value->type.kind() != ir::TypeKind::Error &&
      lowered->value->type != lowered->type) {
    reportTypeMismatch(stmt.value->span, "assignment", lowered->type, lowered->value->type);
  }
  return lowered;
}

std::string SemanticAnalyzer::nameOfSlot(std::uint32_t slot) const {
  if (currentFunction_ != nullptr && slot < currentFunction_->locals.size()) {
    return currentFunction_->locals[slot].name;
  }
  return {};
}

ir::ExprPtr SemanticAnalyzer::checkLvalue(const ast::Expr& expr) {
  switch (expr.kind) {
    case ast::Expr::Kind::Name: {
      Binding* binding = lookup(expr.name);
      if (binding == nullptr) {
        diagnostics_.error(currentFile_, expr.span, "undefined variable '" + expr.name + "'");
        return std::make_unique<ir::Expr>(ir::Expr::Kind::Local, ir::Type::error(), expr.span);
      }
      if (!binding->isMutable) {
        if (binding->isParameter) {
          diagnostics_.error(currentFile_, expr.span,
                             "cannot assign to parameter '" + expr.name +
                                 "'; parameters are immutable");
          diagnostics_.note(binding->file, binding->span, "parameter declared here");
          diagnostics_.note(currentFile_, expr.span,
                            "copy the parameter into a 'var' binding and modify that instead");
        } else {
          diagnostics_.error(currentFile_, expr.span,
                             "cannot assign to immutable binding '" + expr.name +
                                 "'; declare it with 'var' to make it mutable");
          diagnostics_.note(binding->file, binding->span, "binding declared here");
        }
      }
      auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Local, binding->type, expr.span);
      lowered->slot = binding->slot;
      return lowered;
    }
    case ast::Expr::Kind::FieldAccess: {
      ir::ExprPtr base = checkLvalue(*expr.lhs);
      if (!base) {
        return nullptr;
      }
      const ir::StructInfo* info = structOf(base->type);
      if (info == nullptr) {
        diagnostics_.error(currentFile_, expr.fieldSpan,
                           "'" + expr.fieldName + "' cannot be assigned: '" +
                               ir::typeName(base->type, *module_) +
                               "' has no assignable fields");
        return base;
      }
      const int index = fieldIndex(*info, expr.fieldName);
      if (index < 0) {
        reportUnknownField(expr.fieldSpan, *info, expr.fieldName);
        return base;
      }
      const ir::Type fieldType = info->fields[static_cast<std::size_t>(index)].type;
      auto access = std::make_unique<ir::Expr>(ir::Expr::Kind::FieldAccess, fieldType, expr.span);
      access->fieldIndex = static_cast<std::uint32_t>(index);
      access->name = expr.fieldName;
      access->lhs = std::move(base);
      return access;
    }
    case ast::Expr::Kind::Index: {
      // A list element lives in the list's shared storage, so assigning to it
      // changes the list rather than the binding that names the list. It is
      // therefore allowed through a `let` binding and through a parameter, for
      // the same reason `list_push` accepts a list parameter.
      if (expr.lhs) {
        ir::ExprPtr listBase = checkExpr(*expr.lhs, std::nullopt);
        if (listBase->type.kind() == ir::TypeKind::List) {
          return checkIndex(expr);
        }
      }
      ir::ExprPtr base = checkLvalue(*expr.lhs);
      if (!base) {
        return nullptr;
      }
      if (!base->type.isIterable()) {
        diagnostics_.error(currentFile_, expr.span,
                           "cannot index a value of type '" +
                               ir::typeName(base->type, *module_) +
                               "'; only arrays and lists can be indexed");
        return base;
      }
      ir::ExprPtr index = checkExpr(*expr.rhs, std::nullopt);
      if (index->type.kind() != ir::TypeKind::Error && !index->type.isInteger()) {
        diagnostics_.error(currentFile_, expr.rhs->span,
                           "index must be an integer, found '" +
                               ir::typeName(index->type, *module_) + "'");
      }
      const ir::Type elementType = base->type.kind() == ir::TypeKind::List
                                       ? module_->types.listElement(base->type)
                                       : module_->types.element(base->type);
      auto access = std::make_unique<ir::Expr>(ir::Expr::Kind::Index, elementType, expr.span);
      access->lhs = std::move(base);
      access->rhs = std::move(index);
      return access;
    }
    default: {
      checkExpr(expr, std::nullopt);
      diagnostics_.error(currentFile_, expr.span,
                         "invalid assignment target; expected a variable, a field, or an array "
                         "element");
      return std::make_unique<ir::Expr>(ir::Expr::Kind::Local, ir::Type::error(), expr.span);
    }
  }
}

ir::StmtPtr SemanticAnalyzer::checkReturn(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Return, stmt.span);
  const bool returnsValue = stmt.value != nullptr;

  if (currentReturnType_ == ir::Type::voidType()) {
    if (returnsValue) {
      diagnostics_.error(currentFile_, stmt.span,
                         "function '" + currentFunctionName_ +
                             "' does not return a value; remove the returned expression");
      checkExpr(*stmt.value, std::nullopt);
    }
    return lowered;
  }

  if (!returnsValue) {
    diagnostics_.error(currentFile_, stmt.span,
                       "function '" + currentFunctionName_ +
                           "' must return a value of type '" +
                           ir::typeName(currentReturnType_, *module_) + "'");
    return lowered;
  }

  lowered->value = checkExpr(*stmt.value, currentReturnType_);
  if (lowered->value->type.kind() != ir::TypeKind::Error &&
      lowered->value->type != currentReturnType_) {
    reportTypeMismatch(stmt.value->span, "return value", currentReturnType_,
                       lowered->value->type);
  }
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkIf(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::If, stmt.span);

  lowered->value = checkExpr(*stmt.value, std::nullopt);
  const ir::Type conditionType = lowered->value->type;
  if (conditionType.kind() != ir::TypeKind::Error && conditionType != ir::Type::boolean()) {
    diagnostics_.error(currentFile_, stmt.value->span,
                       "the condition of an 'if' statement must have type 'bool', found '" +
                           ir::typeName(conditionType, *module_) + "'");
    diagnostics_.note(currentFile_, stmt.value->span,
                      "compare explicitly, for example 'x != 0'");
  }

  pushScope();
  lowered->body = checkStatements(stmt.body);
  popScope();

  if (stmt.hasElse) {
    lowered->hasElse = true;
    pushScope();
    lowered->elseBody = checkStatements(stmt.elseBody);
    popScope();
  }
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkWhile(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::While, stmt.span);

  lowered->value = checkExpr(*stmt.value, std::nullopt);
  const ir::Type conditionType = lowered->value->type;
  if (conditionType.kind() != ir::TypeKind::Error && conditionType != ir::Type::boolean()) {
    diagnostics_.error(currentFile_, stmt.value->span,
                       "the condition of a 'while' statement must have type 'bool', found '" +
                           ir::typeName(conditionType, *module_) + "'");
    diagnostics_.note(currentFile_, stmt.value->span,
                      "compare explicitly, for example 'x < 10'");
  }

  pushScope();
  ++loopDepth_;
  lowered->body = checkStatements(stmt.body);
  --loopDepth_;
  popScope();
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkFor(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(
      stmt.isRangeLoop ? ir::Stmt::Kind::ForRange : ir::Stmt::Kind::ForEach, stmt.span);

  pushScope();
  ir::Type variableType = ir::Type::error();

  if (stmt.isRangeLoop) {
    const std::optional<LiteralInfo> startLiteral = asIntegerLiteral(*stmt.value);
    const std::optional<LiteralInfo> endLiteral = asIntegerLiteral(*stmt.rangeEnd);

    if (startLiteral && !endLiteral) {
      lowered->limit = checkExpr(*stmt.rangeEnd, std::nullopt);
      variableType = lowered->limit->type;
      if (variableType.kind() != ir::TypeKind::Error && !variableType.isInteger()) {
        diagnostics_.error(currentFile_, stmt.rangeEnd->span,
                           "a range bound must be an integer, found '" +
                               ir::typeName(variableType, *module_) + "'");
        variableType = ir::Type::error();
      }
      if (variableType.isInteger()) {
        lowered->value = literalAs(*stmt.value, *startLiteral, variableType);
      } else {
        lowered->value = checkExpr(*stmt.value, std::nullopt);
      }
    } else {
      lowered->value = checkExpr(*stmt.value, std::nullopt);
      variableType = lowered->value->type;
      if (variableType.kind() != ir::TypeKind::Error && !variableType.isInteger()) {
        diagnostics_.error(currentFile_, stmt.value->span,
                           "a range bound must be an integer, found '" +
                               ir::typeName(variableType, *module_) + "'");
        variableType = ir::Type::error();
      }
      lowered->limit = conformTo(*stmt.rangeEnd, variableType, "range bound");
    }
    if (!lowered->value || !lowered->limit) {
      popScope();
      return nullptr;
    }
  } else {
    ir::ExprPtr iterable = checkExpr(*stmt.value, std::nullopt);
    if (!iterable) {
      popScope();
      return nullptr;
    }
    if (!iterable->type.isIterable()) {
      diagnostics_.error(currentFile_, stmt.value->span,
                         "a 'for' loop iterates over an array, a list, or a range such as 0..n, "
                         "found '" +
                             ir::typeName(iterable->type, *module_) + "'");
      variableType = ir::Type::error();
    } else if (iterable->type.kind() == ir::TypeKind::List) {
      variableType = module_->types.listElement(iterable->type);
    } else {
      variableType = module_->types.element(iterable->type);
    }
    lowered->value = std::move(iterable);
  }

  const auto slot = static_cast<std::uint32_t>(currentFunction_->locals.size());
  lowered->slot = slot;
  lowered->name = stmt.loopVariable;
  lowered->type = variableType;
  currentFunction_->locals.push_back(ir::LocalVariable{stmt.loopVariable, variableType,
                                                      stmt.loopVariableSpan, false, false});
  // The loop variable is a copy and is immutable: assigning to it would not
  // affect the array or the iteration.
  scopes_.back().emplace(
      stmt.loopVariable,
      Binding{slot, variableType, false, false, stmt.loopVariableSpan, currentFile_});

  ++loopDepth_;
  lowered->body = checkStatements(stmt.body);
  --loopDepth_;
  popScope();
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkBreakOrContinue(const ast::Stmt& stmt) {
  const bool isBreak = stmt.kind == ast::Stmt::Kind::Break;
  if (loopDepth_ == 0) {
    diagnostics_.error(currentFile_, stmt.span,
                       std::string("'") + (isBreak ? "break" : "continue") +
                           "' is only allowed inside a loop");
    return nullptr;
  }
  return std::make_unique<ir::Stmt>(isBreak ? ir::Stmt::Kind::Break : ir::Stmt::Kind::Continue,
                                    stmt.span);
}

ir::StmtPtr SemanticAnalyzer::checkBlock(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Block, stmt.span);
  pushScope();
  lowered->body = checkStatements(stmt.body);
  popScope();
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkExpr(const ast::Expr& expr, std::optional<ir::Type> expected) {
  switch (expr.kind) {
    case ast::Expr::Kind::IntegerLiteral:
      return checkIntegerLiteral(expr, expected);
    case ast::Expr::Kind::BoolLiteral: {
      auto lowered =
          std::make_unique<ir::Expr>(ir::Expr::Kind::BoolLiteral, ir::Type::boolean(), expr.span);
      lowered->boolValue = expr.boolValue;
      return lowered;
    }
    case ast::Expr::Kind::StringLiteral: {
      auto lowered =
          std::make_unique<ir::Expr>(ir::Expr::Kind::StringLiteral, ir::Type::str(), expr.span);
      lowered->stringValue = expr.stringValue;
      return lowered;
    }
    case ast::Expr::Kind::InterpolatedString:
      return checkInterpolatedString(expr);
    case ast::Expr::Kind::Name: {
      const Binding* binding = lookup(expr.name);
      if (binding == nullptr) {
        if (functions_.count(expr.name) != 0) {
          diagnostics_.error(currentFile_, expr.span,
                             "function '" + expr.name +
                                 "' cannot be used as a value; call it with '()'");
        } else if (structTypesByName_.count(expr.name) != 0) {
          diagnostics_.error(currentFile_, expr.span,
                             "struct '" + expr.name + "' cannot be used as a value; construct it "
                             "with '" + expr.name + "(...)'");
        } else if (findBuiltin(expr.name) != nullptr || isPrintName(expr.name)) {
          diagnostics_.error(currentFile_, expr.span,
                             "built-in function '" + expr.name +
                                 "' cannot be used as a value; call it with '()'");
        } else {
          diagnostics_.error(currentFile_, expr.span, "undefined variable '" + expr.name + "'");
        }
        return std::make_unique<ir::Expr>(ir::Expr::Kind::Local, ir::Type::error(), expr.span);
      }
      auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Local, binding->type, expr.span);
      lowered->slot = binding->slot;
      lowered->name = expr.name;
      return lowered;
    }
    case ast::Expr::Kind::Unary:
      return checkUnary(expr, expected);
    case ast::Expr::Kind::Binary:
      return checkBinary(expr, expected);
    case ast::Expr::Kind::Call:
      return checkCall(expr);
    case ast::Expr::Kind::MethodCall:
      return checkMethodCall(expr);
    case ast::Expr::Kind::FieldAccess:
      return checkFieldAccess(expr);
    case ast::Expr::Kind::Index:
      return checkIndex(expr);
    case ast::Expr::Kind::Cast:
      return checkCast(expr);
    case ast::Expr::Kind::ArrayLiteral:
      return checkArrayLiteral(expr, expected);
    case ast::Expr::Kind::Range:
      diagnostics_.error(currentFile_, expr.span,
                         "a range is only valid in the header of a 'for' statement");
      return std::make_unique<ir::Expr>(ir::Expr::Kind::Local, ir::Type::error(), expr.span);
  }
  return std::make_unique<ir::Expr>(ir::Expr::Kind::Local, ir::Type::error(), expr.span);
}

ir::ExprPtr SemanticAnalyzer::conformTo(const ast::Expr& expr, ir::Type expected,
                                        const char* context) {
  ir::ExprPtr lowered = checkExpr(expr, expected);
  if (expected.kind() != ir::TypeKind::Error && lowered->type.kind() != ir::TypeKind::Error &&
      lowered->type != expected) {
    reportTypeMismatch(expr.span, context, expected, lowered->type);
  }
  return lowered;
}

std::optional<SemanticAnalyzer::LiteralInfo> SemanticAnalyzer::asIntegerLiteral(
    const ast::Expr& expr) {
  if (expr.kind == ast::Expr::Kind::IntegerLiteral) {
    return LiteralInfo{expr.integerValue, false, expr.unsignedLiteral};
  }
  if (expr.kind == ast::Expr::Kind::Unary && expr.unaryOp == UnaryOp::Negate && expr.lhs &&
      expr.lhs->kind == ast::Expr::Kind::IntegerLiteral) {
    return LiteralInfo{expr.lhs->integerValue, true, expr.lhs->unsignedLiteral};
  }
  return std::nullopt;
}

ir::ExprPtr SemanticAnalyzer::literalAs(const ast::Expr& node, const LiteralInfo& literal,
                                        ir::Type type) {
  if (literal.hasUnsignedSuffix) {
    diagnostics_.error(currentFile_, node.span,
                       "the 'u' integer suffix is not supported; annotate the binding or "
                       "parameter with an unsigned type instead");
    return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(),
                                      node.span);
  }
  if (literal.negate && type.isInteger() && !type.isSigned()) {
    diagnostics_.error(currentFile_, node.span,
                       "a negative literal cannot have the unsigned type '" +
                           ir::typeName(type, *module_) + "'");
    return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(),
                                      node.span);
  }
  if (!literalFits(type, literal.magnitude, literal.negate)) {
    diagnostics_.error(currentFile_, node.span,
                       "integer literal " + std::string(literal.negate ? "-" : "") +
                           std::to_string(literal.magnitude) + " does not fit in type '" +
                           ir::typeName(type, *module_) + "'");
    return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(),
                                      node.span);
  }
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, type, node.span);
  lowered->integerValue = literalBits(literal.magnitude, literal.negate);
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkIntegerLiteral(const ast::Expr& expr,
                                                  std::optional<ir::Type> expected) {
  const std::optional<LiteralInfo> literal = asIntegerLiteral(expr);
  if (!literal) {
    return checkUnary(expr, expected);
  }

  ir::Type type = ir::Type::intType();
  if (expected) {
    if (expected->kind() != ir::TypeKind::Error && expected->isInteger()) {
      type = *expected;
    }
  }
  return literalAs(expr, *literal, type);
}

ir::ExprPtr SemanticAnalyzer::checkUnary(const ast::Expr& expr, std::optional<ir::Type> expected) {
  if (expr.unaryOp == UnaryOp::LogicalNot) {
    auto lowered =
        std::make_unique<ir::Expr>(ir::Expr::Kind::Unary, ir::Type::boolean(), expr.span);
    lowered->unaryOp = UnaryOp::LogicalNot;
    lowered->lhs = conformTo(*expr.lhs, ir::Type::boolean(), "operand of '!'");
    return lowered;
  }

  // A negated literal is folded so that the minimum value of a signed type is
  // representable and range checked against the expected type.
  if (const std::optional<LiteralInfo> literal = asIntegerLiteral(expr)) {
    const ir::Type type = (expected && expected->isInteger()) ? *expected : ir::Type::intType();
    return literalAs(expr, *literal, type);
  }

  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Unary, ir::Type::error(), expr.span);
  lowered->unaryOp = UnaryOp::Negate;
  lowered->lhs = checkExpr(*expr.lhs, std::nullopt);
  if (lowered->lhs->type.kind() == ir::TypeKind::Error) {
    return lowered;
  }
  if (!lowered->lhs->type.isInteger()) {
    diagnostics_.error(currentFile_, expr.lhs->span,
                       "operator '-' requires an integer operand, found '" +
                           ir::typeName(lowered->lhs->type, *module_) + "'");
    return lowered;
  }
  if (!lowered->lhs->type.isSigned()) {
    diagnostics_.error(currentFile_, expr.span,
                       "operator '-' cannot be applied to the unsigned type '" +
                           ir::typeName(lowered->lhs->type, *module_) + "'");
    return lowered;
  }
  lowered->type = lowered->lhs->type;
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkBinary(const ast::Expr& expr, std::optional<ir::Type> expected) {
  const BinaryOp op = expr.binaryOp;
  const bool isComparison = isComparisonOperator(op);

  auto failed = [&]() {
    return std::make_unique<ir::Expr>(ir::Expr::Kind::Binary, ir::Type::error(), expr.span);
  };

  if (op == BinaryOp::LogicalAnd || op == BinaryOp::LogicalOr) {
    auto lowered =
        std::make_unique<ir::Expr>(ir::Expr::Kind::Binary, ir::Type::boolean(), expr.span);
    lowered->binaryOp = op;
    lowered->lhs = conformTo(*expr.lhs, ir::Type::boolean(), "left operand of logical operator");
    lowered->rhs = conformTo(*expr.rhs, ir::Type::boolean(), "right operand of logical operator");
    return lowered;
  }

  const std::optional<LiteralInfo> lhsLiteral = asIntegerLiteral(*expr.lhs);
  const std::optional<LiteralInfo> rhsLiteral = asIntegerLiteral(*expr.rhs);

  ir::ExprPtr lhs;
  ir::ExprPtr rhs;
  ir::Type operandType = ir::Type::error();

  if (lhsLiteral && rhsLiteral) {
    const ir::Type type = (expected && expected->isInteger()) ? *expected : ir::Type::intType();
    lhs = literalAs(*expr.lhs, *lhsLiteral, type);
    rhs = literalAs(*expr.rhs, *rhsLiteral, type);
    operandType = type;
  } else if (lhsLiteral && !rhsLiteral) {
    rhs = checkExpr(*expr.rhs, std::nullopt);
    if (rhs->type.kind() == ir::TypeKind::Error) {
      return failed();
    }
    if (!rhs->type.isInteger() && rhs->type.kind() != ir::TypeKind::Str) {
      diagnostics_.error(currentFile_, expr.span,
                         std::string("operator '") + binaryOpSpelling(op) +
                             "' cannot be applied to '" + ir::typeName(rhs->type, *module_) + "'");
      return failed();
    }
    if (rhs->type.isInteger()) {
      lhs = literalAs(*expr.lhs, *lhsLiteral, rhs->type);
      operandType = rhs->type;
    } else {
      lhs = checkExpr(*expr.lhs, std::nullopt);
      operandType = lhs->type;
    }
  } else if (!lhsLiteral && rhsLiteral) {
    lhs = checkExpr(*expr.lhs, std::nullopt);
    if (lhs->type.kind() == ir::TypeKind::Error) {
      return failed();
    }
    if (!lhs->type.isInteger() && lhs->type.kind() != ir::TypeKind::Str) {
      diagnostics_.error(currentFile_, expr.span,
                         std::string("operator '") + binaryOpSpelling(op) +
                             "' cannot be applied to '" + ir::typeName(lhs->type, *module_) + "'");
      return failed();
    }
    if (lhs->type.isInteger()) {
      rhs = literalAs(*expr.rhs, *rhsLiteral, lhs->type);
      operandType = lhs->type;
    } else {
      rhs = checkExpr(*expr.rhs, std::nullopt);
      operandType = rhs->type;
    }
  } else {
    lhs = checkExpr(*expr.lhs, std::nullopt);
    rhs = checkExpr(*expr.rhs, std::nullopt);
    if (lhs->type.kind() == ir::TypeKind::Error || rhs->type.kind() == ir::TypeKind::Error) {
      return failed();
    }
    if (lhs->type != rhs->type) {
      diagnostics_.error(currentFile_, expr.span,
                         std::string("operands of '") + binaryOpSpelling(op) +
                             "' have different types: '" + ir::typeName(lhs->type, *module_) +
                             "' and '" + ir::typeName(rhs->type, *module_) +
                             "'; Gable never converts between types implicitly");
      diagnostics_.note(currentFile_, expr.span, "use 'as' to convert one operand explicitly");
      return failed();
    }
    operandType = lhs->type;
  }

  if (lhs->type.kind() == ir::TypeKind::Error || operandType.kind() == ir::TypeKind::Error) {
    return failed();
  }

  ir::Type result = operandType;
  if (operandType == ir::Type::str()) {
    if (op == BinaryOp::Add) {
      result = ir::Type::str();
    } else if (op == BinaryOp::Equal || op == BinaryOp::NotEqual) {
      result = ir::Type::boolean();
    } else {
      diagnostics_.error(currentFile_, expr.span,
                         std::string("operator '") + binaryOpSpelling(op) +
                             "' cannot be applied to values of type 'str'");
      diagnostics_.note(currentFile_, expr.span,
                        "'+' concatenates strings and '=='/'!=' compare them; use str_find or "
                        "str_slice for anything else");
      return failed();
    }
  } else if (operandType == ir::Type::boolean()) {
    if (op != BinaryOp::Equal && op != BinaryOp::NotEqual) {
      diagnostics_.error(currentFile_, expr.span,
                         std::string("operator '") + binaryOpSpelling(op) +
                             "' cannot be applied to values of type 'bool'");
      return failed();
    }
  } else if (!operandType.isInteger()) {
    diagnostics_.error(currentFile_, expr.span,
                       std::string("operator '") + binaryOpSpelling(op) +
                           "' cannot be applied to values of type '" +
                           ir::typeName(operandType, *module_) + "'");
    diagnostics_.note(currentFile_, expr.span,
                      "struct and array values are read field by field or element by element");
    return failed();
  } else if (isComparison) {
    result = ir::Type::boolean();
  }

  if ((op == BinaryOp::Divide || op == BinaryOp::Remainder) &&
      rhs->kind == ir::Expr::Kind::IntegerLiteral && rhs->integerValue == 0) {
    diagnostics_.error(currentFile_, expr.rhs->span,
                       std::string(op == BinaryOp::Divide ? "division" : "remainder") +
                           " by zero");
    return failed();
  }

  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Binary, result, expr.span);
  lowered->binaryOp = op;
  lowered->lhs = std::move(lhs);
  lowered->rhs = std::move(rhs);
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkListPush(const ast::Expr& expr) {
  auto lowered =
      std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::voidType(), expr.span);
  lowered->builtin = ir::Builtin::ListPush;
  lowered->name = "list_push";

  if (expr.arguments.size() != 2) {
    diagnostics_.error(currentFile_, expr.span,
                       "list_push expects 2 arguments, the list and the value to add, found " +
                           std::to_string(expr.arguments.size()));
    for (const auto& argument : expr.arguments) {
      checkExpr(*argument, std::nullopt);
    }
    return lowered;
  }

  ir::ExprPtr list = checkExpr(*expr.arguments[0], std::nullopt);
  if (list->type.kind() == ir::TypeKind::Error) {
    lowered->arguments.push_back(std::move(list));
    for (std::size_t i = 1; i < expr.arguments.size(); ++i) {
      checkExpr(*expr.arguments[i], std::nullopt);
    }
    return lowered;
  }
  if (list->type.kind() != ir::TypeKind::List) {
    diagnostics_.error(currentFile_, expr.arguments[0]->span,
                       "list_push needs a list, found '" +
                           ir::typeName(list->type, *module_) + "'");
    diagnostics_.note(currentFile_, expr.arguments[0]->span,
                      "a growable list is written with an element type and no length, "
                      "for example 'let names: [str] = [];'");
    lowered->arguments.push_back(std::move(list));
    checkExpr(*expr.arguments[1], std::nullopt);
    return lowered;
  }

  const ir::Type elementType = module_->types.listElement(list->type);
  lowered->arguments.push_back(std::move(list));
  lowered->arguments.push_back(conformTo(*expr.arguments[1], elementType, "list element"));
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkPrintCall(const ast::Expr& expr, ir::Builtin id) {
  const char* name = id == ir::Builtin::Print ? "print" : "eprint";
  auto lowered =
      std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::voidType(), expr.span);
  lowered->builtin = id;
  lowered->name = name;

  if (expr.arguments.size() != 1) {
    diagnostics_.error(currentFile_, expr.span,
                       std::string(name) + " expects exactly 1 argument, found " +
                           std::to_string(expr.arguments.size()));
    for (const auto& argument : expr.arguments) {
      checkExpr(*argument, std::nullopt);
    }
    return lowered;
  }

  ir::ExprPtr argument = checkExpr(*expr.arguments[0], std::nullopt);
  const ir::Type argumentType = argument->type;
  if (argumentType.kind() != ir::TypeKind::Error && !argumentType.isScalar()) {
    diagnostics_.error(currentFile_, expr.arguments[0]->span,
                       std::string(name) + " cannot print a value of type '" +
                           ir::typeName(argumentType, *module_) + "'");
    if (argumentType.kind() == ir::TypeKind::List) {
      diagnostics_.note(currentFile_, expr.arguments[0]->span,
                        "print each element instead, for example with 'for item in values { "
                        "print(item); }'");
    } else {
      diagnostics_.note(currentFile_, expr.arguments[0]->span,
                        "print the fields of a struct individually, for example 'point.x'");
    }
  }
  lowered->arguments.push_back(std::move(argument));
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkCall(const ast::Expr& expr) {
  if (expr.name == "print") {
    return checkPrintCall(expr, ir::Builtin::Print);
  }
  if (expr.name == "eprint") {
    return checkPrintCall(expr, ir::Builtin::Eprint);
  }
  if (expr.name == "list_push") {
    // The second argument's type depends on the first, so this builtin cannot be
    // described by the fixed signature table.
    return checkListPush(expr);
  }
  if (const BuiltinInfo* builtin = findBuiltin(expr.name)) {
    return checkBuiltinCall(expr, *builtin);
  }
  if (const ir::StructInfo* info = findStruct(expr.name)) {
    return checkStructInit(expr, *info);
  }

  const auto it = functions_.find(expr.name);
  if (it == functions_.end()) {
    if (lookup(expr.name) != nullptr) {
      diagnostics_.error(currentFile_, expr.span,
                         "'" + expr.name + "' is a variable, not a function; Gable has no "
                         "function values");
    } else {
      diagnostics_.error(currentFile_, expr.span, "unknown function '" + expr.name + "'");
    }
    for (const auto& argument : expr.arguments) {
      checkExpr(*argument, std::nullopt);
    }
    return std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::error(), expr.span);
  }

  const FunctionInfo& callee = it->second;
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Call, callee.returnType, expr.span);
  lowered->calleeIndex = callee.index;
  lowered->name = expr.name;

  if (expr.arguments.size() != callee.parameterTypes.size()) {
    diagnostics_.error(currentFile_, expr.span,
                       "function '" + expr.name + "' expects " +
                           std::to_string(callee.parameterTypes.size()) + " argument(s), found " +
                           std::to_string(expr.arguments.size()));
  }

  const std::size_t checked = std::min(expr.arguments.size(), callee.parameterTypes.size());
  for (std::size_t i = 0; i < checked; ++i) {
    ir::ExprPtr argument = checkExpr(*expr.arguments[i], callee.parameterTypes[i]);
    if (callee.parameterTypes[i].kind() != ir::TypeKind::Error &&
        argument->type.kind() != ir::TypeKind::Error &&
        argument->type != callee.parameterTypes[i]) {
      diagnostics_.error(currentFile_, expr.arguments[i]->span,
                         "argument " + std::to_string(i + 1) + " of '" + expr.name +
                             "' has type '" + ir::typeName(argument->type, *module_) +
                             "', expected '" + ir::typeName(callee.parameterTypes[i], *module_) +
                             "'");
    }
    lowered->arguments.push_back(std::move(argument));
  }
  for (std::size_t i = checked; i < expr.arguments.size(); ++i) {
    checkExpr(*expr.arguments[i], std::nullopt);
  }

  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkBuiltinCall(const ast::Expr& expr, const BuiltinInfo& builtin) {
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Call, builtin.returnType, expr.span);
  lowered->builtin = builtin.id;
  lowered->name = std::string(builtin.name);

  if (expr.arguments.size() != builtin.parameterTypes.size()) {
    diagnostics_.error(currentFile_, expr.span,
                       "function '" + lowered->name + "' expects " +
                           std::to_string(builtin.parameterTypes.size()) + " argument(s), found " +
                           std::to_string(expr.arguments.size()));
  }

  const std::size_t checked = std::min(expr.arguments.size(), builtin.parameterTypes.size());
  for (std::size_t i = 0; i < checked; ++i) {
    ir::ExprPtr argument = checkExpr(*expr.arguments[i], builtin.parameterTypes[i]);
    if (builtin.parameterTypes[i].kind() != ir::TypeKind::Error &&
        argument->type.kind() != ir::TypeKind::Error &&
        argument->type != builtin.parameterTypes[i]) {
      diagnostics_.error(currentFile_, expr.arguments[i]->span,
                         "argument " + std::to_string(i + 1) + " of '" + lowered->name +
                             "' has type '" + ir::typeName(argument->type, *module_) +
                             "', expected '" +
                             ir::typeName(builtin.parameterTypes[i], *module_) + "'");
    }
    lowered->arguments.push_back(std::move(argument));
  }
  for (std::size_t i = checked; i < expr.arguments.size(); ++i) {
    checkExpr(*expr.arguments[i], std::nullopt);
  }

  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkStructInit(const ast::Expr& expr, const ir::StructInfo& info) {
  const auto found = structTypesByName_.find(info.name);
  const ir::Type structType =
      found == structTypesByName_.end() ? ir::Type::error() : found->second;

  auto lowered =
      std::make_unique<ir::Expr>(ir::Expr::Kind::StructInit, structType, expr.span);
  lowered->name = info.name;

  if (expr.arguments.size() != info.fields.size()) {
    diagnostics_.error(currentFile_, expr.span,
                       "struct '" + info.name + "' has " + std::to_string(info.fields.size()) +
                           " field(s) but " + std::to_string(expr.arguments.size()) +
                           " value(s) were given");
    if (!info.fields.empty()) {
      diagnostics_.note(currentFile_, expr.span,
                        "fields are given in declaration order: " + fieldListForHint(info));
    }
  }

  const std::size_t checked = std::min(expr.arguments.size(), info.fields.size());
  for (std::size_t i = 0; i < checked; ++i) {
    ir::ExprPtr value = checkExpr(*expr.arguments[i], info.fields[i].type);
    if (info.fields[i].type.kind() != ir::TypeKind::Error &&
        value->type.kind() != ir::TypeKind::Error && value->type != info.fields[i].type) {
      diagnostics_.error(currentFile_, expr.arguments[i]->span,
                         "field '" + info.fields[i].name + "' of struct '" + info.name +
                             "' has type '" + ir::typeName(info.fields[i].type, *module_) +
                             "', found '" + ir::typeName(value->type, *module_) + "'");
    }
    lowered->arguments.push_back(std::move(value));
  }
  for (std::size_t i = checked; i < expr.arguments.size(); ++i) {
    checkExpr(*expr.arguments[i], std::nullopt);
  }

  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkFieldAccess(const ast::Expr& expr) {
  ir::ExprPtr base = checkExpr(*expr.lhs, std::nullopt);
  if (base->type.kind() == ir::TypeKind::Error) {
    return base;
  }

  // `.len` is defined for strings (bytes), arrays, and lists (element count).
  if (expr.fieldName == "len") {
    if (base->type.kind() != ir::TypeKind::Str && !base->type.isIterable()) {
      diagnostics_.error(currentFile_, expr.fieldSpan,
                         "'len' is only defined for strings, arrays, and lists, not for '" +
                             ir::typeName(base->type, *module_) + "'");
      return base;
    }
    auto lowered =
        std::make_unique<ir::Expr>(ir::Expr::Kind::Length, ir::Type::i64(), expr.span);
    lowered->lhs = std::move(base);
    return lowered;
  }

  const ir::StructInfo* info = structOf(base->type);
  if (info == nullptr) {
    diagnostics_.error(currentFile_, expr.fieldSpan,
                       "type '" + ir::typeName(base->type, *module_) + "' has no field '" +
                           expr.fieldName + "'");
    return base;
  }
  const int index = fieldIndex(*info, expr.fieldName);
  if (index < 0) {
    reportUnknownField(expr.fieldSpan, *info, expr.fieldName);
    return base;
  }
  const ir::Type fieldType = info->fields[static_cast<std::size_t>(index)].type;
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::FieldAccess, fieldType, expr.span);
  lowered->fieldIndex = static_cast<std::uint32_t>(index);
  lowered->name = expr.fieldName;
  lowered->lhs = std::move(base);
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkIndex(const ast::Expr& expr) {
  ir::ExprPtr base = checkExpr(*expr.lhs, std::nullopt);
  if (base->type.kind() == ir::TypeKind::Error) {
    return base;
  }
  if (!base->type.isIterable()) {
    diagnostics_.error(currentFile_, expr.span,
                       "cannot index a value of type '" + ir::typeName(base->type, *module_) +
                           "'; only arrays and lists can be indexed");
    if (base->type.kind() == ir::TypeKind::Str) {
      diagnostics_.note(currentFile_, expr.span,
                        "use str_slice(text, start, length) to take part of a string");
    }
    return base;
  }
  ir::ExprPtr index = checkExpr(*expr.rhs, std::nullopt);
  if (index->type.kind() != ir::TypeKind::Error && !index->type.isInteger()) {
    diagnostics_.error(currentFile_, expr.rhs->span,
                       "index must be an integer, found '" + ir::typeName(index->type, *module_) +
                           "'");
  }
  const ir::Type elementType = base->type.kind() == ir::TypeKind::List
                                   ? module_->types.listElement(base->type)
                                   : module_->types.element(base->type);
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Index, elementType, expr.span);
  lowered->lhs = std::move(base);
  lowered->rhs = std::move(index);
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkCast(const ast::Expr& expr) {
  const ir::Type target = resolveType(expr.targetType);
  ir::ExprPtr operand = checkExpr(*expr.lhs, std::nullopt);
  if (operand->type.kind() == ir::TypeKind::Error || target.kind() == ir::TypeKind::Error) {
    return std::make_unique<ir::Expr>(ir::Expr::Kind::Cast, ir::Type::error(), expr.span);
  }
  if (!operand->type.isInteger() || !target.isInteger()) {
    diagnostics_.error(currentFile_, expr.span,
                       "'as' converts between integer types; '" +
                           ir::typeName(operand->type, *module_) + "' to '" +
                           ir::typeName(target, *module_) + "' is not an integer conversion");
    if (target == ir::Type::str() || operand->type == ir::Type::str()) {
      diagnostics_.note(currentFile_, expr.span,
                        "use str_of_i64 or parse_i64 to convert between text and numbers");
    }
    return std::make_unique<ir::Expr>(ir::Expr::Kind::Cast, ir::Type::error(), expr.span);
  }
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Cast, target, expr.span);
  lowered->lhs = std::move(operand);
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkArrayLiteral(const ast::Expr& expr,
                                               std::optional<ir::Type> expected) {
  if (expected && expected->kind() == ir::TypeKind::List) {
    // `[1, 2, 3]` where a list is expected: the elements decide nothing about
    // the type, so the surrounding context supplies it.
    const ir::Type elementType = module_->types.listElement(*expected);
    auto list = std::make_unique<ir::Expr>(ir::Expr::Kind::ArrayLiteral, *expected, expr.span);
    for (const auto& element : expr.arguments) {
      list->arguments.push_back(conformTo(*element, elementType, "list element"));
    }
    return list;
  }
  if (!expected || expected->kind() != ir::TypeKind::Array) {
    diagnostics_.error(currentFile_, expr.span,
                       "cannot infer the type of an array literal from its elements");
    diagnostics_.note(currentFile_, expr.span,
                      "annotate the binding, for example 'let values: [i32; 3] = [1, 2, 3];' "
                      "for a fixed array, or 'let totals: [i32] = [0];' for a growable list");
    for (const auto& element : expr.arguments) {
      checkExpr(*element, std::nullopt);
    }
    return std::make_unique<ir::Expr>(ir::Expr::Kind::ArrayLiteral, ir::Type::error(),
                                      expr.span);
  }

  const ir::Type arrayType = *expected;
  const ir::Type elementType = module_->types.element(arrayType);
  const std::uint64_t length = module_->types.length(arrayType);
  if (expr.arguments.size() != length) {
    diagnostics_.error(currentFile_, expr.span,
                       "array type '" + ir::typeName(arrayType, *module_) + "' needs " +
                           std::to_string(length) + " element(s), found " +
                           std::to_string(expr.arguments.size()));
  }

  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::ArrayLiteral, arrayType, expr.span);
  for (const auto& element : expr.arguments) {
    lowered->arguments.push_back(conformTo(*element, elementType, "array element"));
  }
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkMethodCall(const ast::Expr& expr) {
  ir::ExprPtr receiver = checkExpr(*expr.lhs, std::nullopt);
  const ir::Type receiverType = receiver->type;

  auto failed = [&]() {
    return std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::error(), expr.span);
  };

  // `values.push(x)` is the list operation written with the list first, which is
  // how every other list and string operation is written as well.
  if (expr.name == "push") {
    if (receiverType.kind() == ir::TypeKind::Error) {
      for (const auto& argument : expr.arguments) {
        checkExpr(*argument, std::nullopt);
      }
      return failed();
    }
    if (receiverType.kind() != ir::TypeKind::List) {
      reportUnknownMethod(expr, receiverType);
      for (const auto& argument : expr.arguments) {
        checkExpr(*argument, std::nullopt);
      }
      return failed();
    }
    if (expr.arguments.size() != 1) {
      diagnostics_.error(currentFile_, expr.span,
                         "'push' adds one value, found " +
                             std::to_string(expr.arguments.size()) + " arguments");
      for (const auto& argument : expr.arguments) {
        checkExpr(*argument, std::nullopt);
      }
      return failed();
    }
    auto lowered =
        std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::voidType(), expr.span);
    lowered->builtin = ir::Builtin::ListPush;
    lowered->name = "list_push";
    lowered->arguments.push_back(std::move(receiver));
    const ir::Type elementType = module_->types.listElement(receiverType);
    lowered->arguments.push_back(conformTo(*expr.arguments[0], elementType, "list element"));
    return lowered;
  }

  // Every other method is a built-in whose first parameter is the receiver.
  const std::string_view freeFunction = methodToBuiltin(expr.name);
  if (freeFunction.empty()) {
    reportUnknownMethod(expr, receiverType);
    for (const auto& argument : expr.arguments) {
      checkExpr(*argument, std::nullopt);
    }
    return failed();
  }
  const BuiltinInfo* builtin = findBuiltin(freeFunction);
  if (builtin == nullptr || builtin->parameterTypes.empty()) {
    return failed();
  }
  if (receiverType.kind() == ir::TypeKind::Error) {
    for (const auto& argument : expr.arguments) {
      checkExpr(*argument, std::nullopt);
    }
    return failed();
  }
  if (receiverType != builtin->parameterTypes[0]) {
    diagnostics_.error(currentFile_, expr.fieldSpan,
                       "'" + expr.name + "' needs a value of type '" +
                           ir::typeName(builtin->parameterTypes[0], *module_) + "', found '" +
                           ir::typeName(receiverType, *module_) + "'");
    noteAvailableMethods(expr.fieldSpan, receiverType);
    for (const auto& argument : expr.arguments) {
      checkExpr(*argument, std::nullopt);
    }
    return failed();
  }

  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Call, builtin->returnType, expr.span);
  lowered->builtin = builtin->id;
  lowered->name = std::string(builtin->name);
  lowered->arguments.push_back(std::move(receiver));

  const std::size_t expected = builtin->parameterTypes.size() - 1;
  if (expr.arguments.size() != expected) {
    diagnostics_.error(currentFile_, expr.span,
                       "'" + expr.name + "' expects " + std::to_string(expected) +
                           " argument(s) besides the value it is called on, found " +
                           std::to_string(expr.arguments.size()));
  }
  const std::size_t checked = std::min(expr.arguments.size(), expected);
  for (std::size_t i = 0; i < checked; ++i) {
    lowered->arguments.push_back(
        conformTo(*expr.arguments[i], builtin->parameterTypes[i + 1], "argument"));
  }
  for (std::size_t i = checked; i < expr.arguments.size(); ++i) {
    checkExpr(*expr.arguments[i], std::nullopt);
  }
  return lowered;
}

void SemanticAnalyzer::reportUnknownMethod(const ast::Expr& expr, ir::Type receiverType) {
  diagnostics_.error(currentFile_, expr.fieldSpan,
                     "'" + ir::typeName(receiverType, *module_) + "' has no method '" +
                         expr.name + "'");
  noteAvailableMethods(expr.fieldSpan, receiverType);
}

void SemanticAnalyzer::noteAvailableMethods(SourceSpan span, ir::Type receiverType) {
  if (receiverType.kind() == ir::TypeKind::Str) {
    diagnostics_.note(currentFile_, span,
                      "a str supports '.len', '.trim()', '.split(separator)', '.slice(start, "
                      "length)', and '.find(needle)'");
    return;
  }
  if (receiverType.kind() == ir::TypeKind::List) {
    diagnostics_.note(currentFile_, span,
                      "a list supports '.len', '.push(value)', indexing, and 'for item in list'");
    return;
  }
  if (receiverType.kind() == ir::TypeKind::Array) {
    diagnostics_.note(currentFile_, span,
                      "an array supports '.len', indexing, and 'for item in array'; use a list "
                      "('[T]') when it has to grow");
  }
}

ir::ExprPtr SemanticAnalyzer::checkInterpolatedString(const ast::Expr& expr) {
  auto stringLiteral = [](std::string text, SourceSpan span) {
    auto node = std::make_unique<ir::Expr>(ir::Expr::Kind::StringLiteral, ir::Type::str(), span);
    node->stringValue = std::move(text);
    return node;
  };

  ir::ExprPtr result;
  auto append = [&](ir::ExprPtr piece) {
    if (!result) {
      result = std::move(piece);
      return;
    }
    auto concat =
        std::make_unique<ir::Expr>(ir::Expr::Kind::Binary, ir::Type::str(), expr.span);
    concat->binaryOp = BinaryOp::Add;
    concat->lhs = std::move(result);
    concat->rhs = std::move(piece);
    result = std::move(concat);
  };

  const std::size_t count = expr.arguments.size();
  for (std::size_t i = 0; i < count; ++i) {
    if (i < expr.parts.size() && !expr.parts[i].empty()) {
      append(stringLiteral(expr.parts[i], expr.span));
    }

    const ast::Expr& argument = *expr.arguments[i];
    ir::ExprPtr value = checkExpr(argument, std::nullopt);
    if (value->type.kind() == ir::TypeKind::Error) {
      continue;
    }
    if (value->type == ir::Type::str()) {
      append(std::move(value));
      continue;
    }
    if (value->type.isInteger() || value->type == ir::Type::boolean()) {
      const bool isInteger = value->type.isInteger();
      auto converted = std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::str(),
                                                  argument.span);
      converted->builtin = isInteger ? ir::Builtin::StrOfI64 : ir::Builtin::StrOfBool;
      converted->name = isInteger ? "str_of_i64" : "str_of_bool";
      converted->arguments.push_back(std::move(value));
      append(std::move(converted));
      continue;
    }
    diagnostics_.error(currentFile_, argument.span,
                       "cannot put a value of type '" +
                           ir::typeName(value->type, *module_) + "' into a string literal");
    diagnostics_.note(currentFile_, argument.span,
                      "a string literal interpolates str, integers, and bool; convert the value "
                      "first, for example with str_of_i64");
  }
  if (!expr.parts.empty() && !expr.parts.back().empty()) {
    append(stringLiteral(expr.parts.back(), expr.span));
  }
  if (!result) {
    return stringLiteral(std::string(), expr.span);
  }
  return result;
}

void SemanticAnalyzer::pushScope() { scopes_.emplace_back(); }

void SemanticAnalyzer::popScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

SemanticAnalyzer::Binding* SemanticAnalyzer::lookup(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    const auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

ir::Type SemanticAnalyzer::resolveType(const ast::TypeSyntax& syntax) {
  if (syntax.kind == ast::TypeSyntax::Kind::List) {
    if (!syntax.element) {
      return ir::Type::error();
    }
    const ir::Type element = resolveType(*syntax.element);
    if (element.kind() == ir::TypeKind::Error) {
      return ir::Type::error();
    }
    if (element.kind() == ir::TypeKind::Void) {
      diagnostics_.error(currentFile_, syntax.span, "a list element cannot have type 'void'");
      return ir::Type::error();
    }
    return module_->types.list(element);
  }

  if (syntax.kind == ast::TypeSyntax::Kind::Array) {
    if (!syntax.element) {
      return ir::Type::error();
    }
    const ir::Type element = resolveType(*syntax.element);
    if (element.kind() == ir::TypeKind::Error) {
      return ir::Type::error();
    }
    if (element.kind() == ir::TypeKind::Void) {
      diagnostics_.error(currentFile_, syntax.span, "an array element cannot have type 'void'");
      return ir::Type::error();
    }
    if (syntax.arrayLength == 0) {
      diagnostics_.error(currentFile_, syntax.lengthSpan,
                         "an array must have at least one element");
      return ir::Type::error();
    }
    if (syntax.arrayLength > 1000000) {
      diagnostics_.error(currentFile_, syntax.lengthSpan,
                         "array length " + std::to_string(syntax.arrayLength) +
                             " exceeds the supported maximum of 1000000");
      return ir::Type::error();
    }
    return module_->types.array(element, syntax.arrayLength);
  }

  if (ir::Type::isScalarSourceName(syntax.name)) {
    return ir::Type::fromSourceName(syntax.name);
  }
  if (const ir::StructInfo* info = findStruct(syntax.name)) {
    return structTypesByName_.at(info->name);
  }
  diagnostics_.error(currentFile_, syntax.span, "unknown type '" + syntax.name + "'");
  return ir::Type::error();
}

const ir::StructInfo* SemanticAnalyzer::findStruct(const std::string& name) const {
  const auto it = structTypesByName_.find(name);
  if (it == structTypesByName_.end()) {
    return nullptr;
  }
  return module_->structs[it->second.index()].get();
}

const ir::StructInfo* SemanticAnalyzer::structOf(ir::Type type) const {
  if (type.kind() != ir::TypeKind::Struct || type.index() >= module_->structs.size()) {
    return nullptr;
  }
  return module_->structs[type.index()].get();
}

int SemanticAnalyzer::fieldIndex(const ir::StructInfo& info, const std::string& name) const {
  for (std::size_t i = 0; i < info.fields.size(); ++i) {
    if (info.fields[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void SemanticAnalyzer::reportUnknownField(SourceSpan span, const ir::StructInfo& info,
                                          const std::string& name) {
  diagnostics_.error(currentFile_, span,
                     "struct '" + info.name + "' has no field '" + name + "'");
  if (!info.fields.empty()) {
    diagnostics_.note(currentFile_, span, "available fields: " + fieldListForHint(info));
  }
}

std::string SemanticAnalyzer::fieldListForHint(const ir::StructInfo& info) const {
  std::string list;
  for (std::size_t i = 0; i < info.fields.size(); ++i) {
    if (i != 0) {
      list += ", ";
    }
    list += info.fields[i].name + ": " + ir::typeName(info.fields[i].type, *module_);
  }
  return list;
}

void SemanticAnalyzer::reportTypeMismatch(SourceSpan span, const std::string& context,
                                          ir::Type expected, ir::Type actual) {
  diagnostics_.error(currentFile_, span,
                     "type mismatch in " + context + ": expected '" +
                         ir::typeName(expected, *module_) + "', found '" +
                         ir::typeName(actual, *module_) + "'");
}

bool SemanticAnalyzer::alwaysReturns(const std::vector<ir::StmtPtr>& statements) const {
  for (const auto& statement : statements) {
    if (stmtAlwaysReturns(*statement)) {
      return true;
    }
  }
  return false;
}

bool SemanticAnalyzer::stmtAlwaysReturns(const ir::Stmt& stmt) const {
  switch (stmt.kind) {
    case ir::Stmt::Kind::Return:
      return true;
    case ir::Stmt::Kind::Block:
      return alwaysReturns(stmt.body);
    case ir::Stmt::Kind::If:
      return stmt.hasElse && alwaysReturns(stmt.body) && alwaysReturns(stmt.elseBody);
    default:
      return false;
  }
}

}  // namespace gable
