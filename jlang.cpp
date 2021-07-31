#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
using namespace llvm;
enum Token {
  tok_eof = -1,

  tok_def = -2,
  tok_extern = -3,

  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr;
static double NumVal;

static int gettok() {
  static int LastChar = ' ';

  // deal with spaces
  while (std::isspace(LastChar)) {
    LastChar = getchar();
  }

  // deal with alpha
  if (std::isalpha(LastChar)) {
    IdentifierStr = LastChar;

    while (std::isalnum(LastChar = getchar())) {
      IdentifierStr += LastChar;
    }

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    return tok_identifier;
  }

  // deal with numbers
  if (std::isdigit((LastChar) || LastChar == '.')) {
    std::string NumStr;

    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (std::isdigit((LastChar) || LastChar == '.'));

    NumVal = std::strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  // deal with comments
  if (LastChar == '#') {
    do {
      LastChar = getchar();
    } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  // deal with end of file
  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;
  LastChar = getchar();

  return ThisChar;
}

// namespace {

class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
public:
  NumberExprAST(double v) : Val(v) {}
  Value *codegen() override;

private:
  double Val;
};

class VariableExprAST : public ExprAST {
public:
  VariableExprAST(const std::string &str) : Name(str) {}
  Value *codegen() override;

private:
  std::string Name;
};

class BinaryExprAST : public ExprAST {
public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> lhs,
                std::unique_ptr<ExprAST> rhs)
      : Op(op), LHS(std::move(lhs)), RHS(std::move(rhs)) {}
  Value *codegen() override;

private:
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;
};

class CallExprAST : public ExprAST {
public:
  CallExprAST(const std::string &callee,
              std::vector<std::unique_ptr<ExprAST>> args)
      : Callee(callee), Args(std::move(args)) {}
  Value *codegen() override;

private:
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;
};

class PrototypeAST {
public:
  PrototypeAST(const std::string &name, std::vector<std::string> args)
      : Name(name), Args(std::move(args)) {}
  const std::string &getName() const { return this->Name; }
  Function *codegen();

private:
  std::string Name;
  std::vector<std::string> Args;
};

class FunctionAST {
public:
  FunctionAST(std::unique_ptr<PrototypeAST> proto,
              std::unique_ptr<ExprAST> body)
      : Proto(std::move(proto)), Body(std::move(body)) {}
  Function *codegen();
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;
};

//}// namespace

static int CurTok;
static int getNextTok() { return CurTok = gettok(); }
std::map<char, int> BinopPrecedence;

static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

std::unique_ptr<ExprAST> LogError(const char *Str) {
  fmt::print("Log Error: {}\n", Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextTok();
  return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextTok();
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != ')')
    return LogError("Expected ')'");
  getNextTok();
  return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  getNextTok();

  if (CurTok != '(')
    return std::make_unique<VariableExprAST>(IdName);

  getNextTok();

  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return LogError("Expected ')' or ',' in the argument list");
      getNextTok();
    }
  }
  getNextTok(); // eat )
  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    LogError("Unkown token while parsing!");
  case tok_number:
    return ParseNumberExpr();
  case tok_identifier:
    return ParseIdentifierExpr();
  case '(':
    return ParseParenExpr();
  }
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;
    getNextTok();

    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(LHS));
      if (!RHS)
        return nullptr;
    }
    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected fucntion name in prototype");
  std::string Fname = IdentifierStr;
  getNextTok();

  if (CurTok != '(')
    return LogErrorP("Expected '(' name in prototype");

  std::vector<std::string> ArgNames;

  while (getNextTok() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
  }
  if (CurTok != ')')
    return LogErrorP("Expected ')' name in prototype");

  getNextTok();

  return std::make_unique<PrototypeAST>(Fname, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextTok();
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextTok();
  return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}
static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      fmt::print("Parsed a function definition.\n");
      FnIR->print(errs());
      std::printf("\n");
    }
  } else {
    getNextTok();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      fmt::print("Parsed an extern\n");
      FnIR->print(errs());
      std::printf("\n");
    }
  } else {
    getNextTok();
  }
}
static void HandleTopLevelExpression() {
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      fmt::print("Parsed a top-level expr\n");
      FnIR->print(errs());
      std::printf("\n");
    }
  } else {
    getNextTok();
  }
}

static void MainLoop() {
  while (true) {
    fmt::print("Jlang>");
    switch (CurTok) {
    case tok_eof:
      return;
    case ';':
      getNextTok();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

// In the LLVM IR, numeric constants are represented with the ConstantFP class,
// which holds the numeric value in an APFloat internally
Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  Value *V = NamedValues[Name];
  if (!V)
    LogErrorV("Unkown variable name!");
  return V;
}

Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();

  if (!L || !R) {
    return nullptr;
  }

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator!");
  }
}

Value *CallExprAST::codegen() {
  Function *CalleeF = TheModule->getFunction(Callee);
  if (!CalleeF) {
    return LogErrorV("Unkown function referenced!");
  }

  if (CalleeF->arg_size() != Args.size()) {
    return LogErrorV("Incorrect argument number!");
  }

  std::vector<Value *> ArgsV;
  for (auto &i : Args) {
    ArgsV.push_back(i->codegen());
    if (!ArgsV.back())
      return nullptr;
  }
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::codegen() {
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);
  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  auto Idx = 0;
  for (auto &Arg : F->args()) {
    Arg.setName(Args[Idx++]);
  }

  return F;
}

Function *FunctionAST::codegen() {
  Function *TheFunction = TheModule->getFunction(Proto->getName());

  if (!TheFunction)
    TheFunction = Proto->codegen();

  if (!TheFunction)
    return nullptr;

  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  NamedValues.clear();

  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  if (Value *RetVal = Body->codegen()) {
    Builder->CreateRet(RetVal);
    verifyFunction(*TheFunction);

    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}

static void InitializeModule() {
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("Jun's JIT", *TheContext);

  Builder = std::make_unique<IRBuilder<>>(*TheContext);
}
int main() {

  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40; // highest.

  fmt::print("Jlang>");
  getNextTok();
  InitializeModule();

  MainLoop();
  TheModule->print(errs(), nullptr);
  return 0;
}
