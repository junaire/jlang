#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum Token {
	tok_eof = -1,

	tok_def = -2,
	tok_extern = -3,

	tok_identifier = -4,
	tok_number = -5,
};

static std::string IndentifierStr;
static double NumVal;

static int gettok() {
	static int LastChar = ' ';

	// deal with spaces
	while(std::isspace(LastChar)) {
		LastChar = getchar();
	}

	//deal with alpha
	if(std::isalpha(LastChar)) {
		IndentifierStr = LastChar;

		while(std::isalnum(LastChar = getchar())) {
			IndentifierStr += LastChar;
		}

		if (IndentifierStr == "def")
			return tok_def;
		if (IndentifierStr == "extern")
			return tok_extern;
	}

	//deal with numbers
	if (std::isdigit((LastChar) || LastChar == '.')) {
		std::string NumStr;

		do {
			NumStr += LastChar;
			LastChar = getchar();
		}while (std::isdigit((LastChar) || LastChar == '.'));

		NumVal = std::strtod(NumStr.c_str(), nullptr);
		return tok_number;
	}

	//deal with comments
	if (LastChar == '#') {
		do {
			LastChar = getchar();
		}while(LastChar != EOF && LastChar != '\n' && LastChar != '\n');

		if (LastChar != EOF)
			return gettok();
	}

	//deal with end of file
	if (LastChar == EOF)
		return tok_eof;


	int ThisChar = LastChar;
	LastChar = getchar();

	return ThisChar;
}

class ExprAST {
	public:
		virtual ~ExprAST() = 0;
};

class NumberExprAST : public ExprAST {
	public:
		NumberExprAST(double v) : Val(v) {}
	private:
		double Val;
};

class VariableExprAST : public ExprAST {
	public:
		VariableExprAST(const std::string& str) : Name(str) {}
	private:
		std::string Name;
};

class BinaryExprAST : public ExprAST {
	public:
		BinaryExprAST(char op, 
				std::unique_ptr<ExprAST> lhs, 
				std::unique_ptr<ExprAST> rhs) :
			Op(op), LHS(std::move(lhs)), RHS(std::move(rhs)) {}
	private:
		char Op;
		std::unique_ptr<ExprAST> LHS, RHS;
};


class CallExprAST : public ExprAST {
	public:
		CallExprAST(const std::string& callee, 
				std::vector<std::unique_ptr<ExprAST>> args) :
			Callee(callee), Args(std::move(args)) {}
	private:
		std::string Callee;
		std::vector<std::unique_ptr<ExprAST>> Args;
};

class PrototypeAST {
	public:
		PrototypeAST(const std::string& name, std::vector<std::string> args) :
			Name(name), Args(args) {}
		const std::string& getName() const { return this->Name; }
	private:
		std::string Name;
		std::vector<std::string> Args;
};

class FunctionAST {
	public:
		FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) :
			Proto(std::move(proto)), Body(std::move(body)) {}
	private:
		std::unique_ptr<PrototypeAST> Proto;
		std::unique_ptr<ExprAST> Body;
};

