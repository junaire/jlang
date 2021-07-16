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
