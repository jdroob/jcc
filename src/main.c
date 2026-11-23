#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>

// ======
// common.h
size_t len;

// ======
// argparse.h

static struct option longOptions[] = {
	{ "lex", no_argument, NULL, 'l' },
	{ "parse", no_argument, NULL, 'p' },
	{ "codegen", no_argument, NULL, 'c'},
	{ "emit-asm", no_argument, NULL, 'S'},
	{ "retain-pp", no_argument, NULL, 'E'},
	{ 0, 0, 0, 0 }
};

typedef enum {
	JCC_LEX_ONLY,
	JCC_PARSE_ONLY,
	JCC_CODEGEN_ONLY,
	JCC_EMITASM_ONLY,
	JCC_FULL
} JccMode_e;

JccMode_e mode = JCC_FULL;
#define ONLY_EMITASM (mode == JCC_EMITASM_ONLY)
#define ONLY_LEX     (mode == JCC_LEX_ONLY)
#define ONLY_PARSE   (mode == JCC_PARSE_ONLY)
#define ONLY_CODEGEN (mode == JCC_CODEGEN_ONLY)
bool retainPreProcessedSource = false;

void usage(void) {
	printf("usage: jcc <source>.c [--lex] [--parse] [--codegen]\n");
}

// ======
// lexer.h

// --- types ---
typedef enum {
	TOKEN_TYPE_INT,
	TOKEN_TYPE_VOID,
	TOKEN_OPEN_PAREN,
	TOKEN_CLOSE_PAREN,
	TOKEN_OPEN_BRACE,
	TOKEN_CLOSE_BRACE,
	TOKEN_SEMICOLON,
	TOKEN_COMMA,
	TOKEN_RETURN,
	TOKEN_NUM,
	TOKEN_IDENTIFIER,
	TOKEN_PLUS,
	TOKEN_MINUS,
	TOKEN_STAR,
	TOKEN_SLASH,
	TOKEN_TERMINAL
} TokenType_e;

typedef struct {
	TokenType_e type;
	ssize_t len;
	int line;
	const char *start;
} Token_t;

typedef struct {
	ssize_t len;
	ssize_t cap;
	Token_t *tokens;
} TokenList_t;

typedef struct {
	int line;
	const char *start;
	const char *curr;
	TokenList_t *tokens;
} Scanner_t;

// --- scanner state ---
Scanner_t scanner;

static bool isFloat = false;

// --- token list ---
TokenList_t *createTokenList(void) {
	TokenList_t *list = (TokenList_t *)malloc(sizeof(TokenList_t));
	if (!list) {
		perror(": createTokenList::malloc");
		exit(EXIT_FAILURE);
	}
	list->len = -1;
	list->cap = -1;
	list->tokens = NULL;
	return list;
}

void deleteList(void) {
	free(scanner.tokens->tokens);
	free(scanner.tokens);
}

Token_t createToken(TokenType_e type) {
	Token_t tok;
	tok.type = type;
	tok.start = scanner.curr;
	return tok;
}

bool addToken(Token_t tok) {
	if (scanner.tokens->len >= scanner.tokens->cap) {
		ssize_t oldCap = scanner.tokens->cap;
		if (oldCap == -1) {
			scanner.tokens->cap = 8;
			scanner.tokens->len = 0;
		} else {
			scanner.tokens->cap *= 2;
		}
		Token_t *list = (Token_t *)realloc(scanner.tokens->tokens, (size_t)scanner.tokens->cap * sizeof(Token_t));
		if (!list) {
			perror(": addToken::realloc");
			// exit(EXIT_FAILURE);
			return false;
		}
		scanner.tokens->tokens = list;
	}
	scanner.tokens->tokens[scanner.tokens->len++] = tok;
	return true;
}

// --- scanner setup ---
void initScanner(Scanner_t *scanner) {
	scanner->start = NULL;
	scanner->curr = NULL;
	scanner->line = 1;
	scanner->tokens = createTokenList();
}

void setup(const char *src) {
	initScanner(&scanner);
	scanner.start = scanner.curr = src;
}

void teardown(void) {
	deleteList();
	scanner.start = scanner.curr = NULL;
}

// --- cursor movement ---
bool isAtEnd(void) {
	return *scanner.curr == '\0';
}

void skip(void) {
	scanner.start++;
}

void advance(void) {
	if (!isAtEnd()) {
		scanner.curr++;
	}
}

void bump(void) {
	skip();
	advance();
}

char peek(void) {
	if (!isAtEnd()) {
		return *(scanner.curr + 1);
	}
	return '\0';
}

bool check(char c) {
	if (*scanner.curr == c) {
		advance();
		return true;
	}
	return false;
}

// --- character classification ---
bool isWhitespace(void) {
	if (*scanner.curr == '\n') {
		scanner.line++;
		return true;
	}
	return *scanner.curr == ' '  ||
		   *scanner.curr == '\r' ||
		   *scanner.curr == '\t';
}

bool isNumeric(const char *c) {
	if (*c =='.') {
		if (!isFloat) {
			isFloat = true;
			return true;
		} else {
			fprintf(stderr, "Multiple '.' in float\n");
			exit(EXIT_FAILURE);
		}
	}
	return *c >= '0' && *c <= '9';
}

bool isAlpha(const char *c) {
	return (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z');
}

bool isAlphaNum(const char *c) {
	return (*c >= 'a' && *c <= 'z') ||
		   (*c >= 'A' && *c <= 'Z') ||
		   *c == '_' 				||
		   (*c >= '0' && *c <= '9');
}

// --- comments ---
bool isSLComment(void) {
	return *scanner.start == '/' && peek() == '/';
}

void skipComment(void) {
	if (isSLComment()) {
		while (!isAtEnd() && !check('\n')) {
			advance();
		}
		if (isAtEnd()) {
			return;
		}
		scanner.line++;
	}
}

// --- token construction ---
Token_t makeToken(TokenType_e type) {
	Token_t tok;
	tok.type = type;
	tok.start = scanner.start;
	tok.len = scanner.curr - scanner.start;
	tok.line = scanner.line;
	scanner.start = scanner.curr;
	return tok;
}

Token_t makeIdentifier(void) {
	while (isAlphaNum(scanner.curr)) {
		advance();
	}
	Token_t tok;
	tok.type = TOKEN_IDENTIFIER;
	tok.start = scanner.start;
	tok.len = scanner.curr - scanner.start;
	tok.line = scanner.line;
	scanner.start = scanner.curr;
	return tok;
}

Token_t checkKeyword(const char *suffix, const char *start, TokenType_e type) {
	while (isAlphaNum(scanner.curr)) {
		advance();
	}
	if (strncmp(suffix, start, (size_t)(scanner.curr - start)) == 0) {
		return makeToken(type);
	}
	return makeToken(TOKEN_IDENTIFIER);
}

// --- lexer entry points ---
Token_t nextToken(void) {
	/**
		'skip X' code is largely useless since we're operating on
		preprocessed code :)
	*/
	bool lookingAtJunk = false;
	do {
		// skip whitespace
		while (!isAtEnd() && isWhitespace()) {
			bump();
		}
		// skip comments
		if (!isAtEnd() && isSLComment()) {
			skipComment();
		}
		lookingAtJunk = !isAtEnd() && (isWhitespace() || isSLComment());
	} while (lookingAtJunk);
	if (isAtEnd()) return makeToken(TOKEN_TERMINAL);
	if (isNumeric(scanner.start)) {
		while (isNumeric(scanner.curr)) {
			advance();
		}
		isFloat = false;
		if (isAlpha(scanner.curr)) {
			while (!isAtEnd() && isAlpha(scanner.curr)) { advance(); }
			fprintf(stderr, "Invalid identifier %.*s\n", (int)(scanner.curr - scanner.start), scanner.start);
			exit(EXIT_FAILURE);
		}
		return makeToken(TOKEN_NUM);
	}
	switch(*scanner.start) {
		case 'i': {
			return checkKeyword("nt", scanner.curr + 1, TOKEN_TYPE_INT);
		}
		case 'r': {
			return checkKeyword("eturn", scanner.curr + 1, TOKEN_RETURN);
		}
		case 'v': {
			return checkKeyword("oid", scanner.curr + 1, TOKEN_TYPE_VOID);
		}
		case '(': {
			advance();
			return makeToken(TOKEN_OPEN_PAREN);
		}
		case ')': {
			advance();
			return makeToken(TOKEN_CLOSE_PAREN);
		}
		case '{': {
			advance();
			return makeToken(TOKEN_OPEN_BRACE);
		}
		case '}': {
			advance();
			return makeToken(TOKEN_CLOSE_BRACE);
		}
		case ';': {
			advance();
			return makeToken(TOKEN_SEMICOLON);
		}
		case ',': {
			advance();
			return makeToken(TOKEN_COMMA);
		}
		case '+': {
			advance();
			return makeToken(TOKEN_PLUS);
		}
		case '-': {
			advance();
			return makeToken(TOKEN_MINUS);
		}
		case '/': {
			advance();
			return makeToken(TOKEN_SLASH);
		}
		case '*': {
			advance();
			return makeToken(TOKEN_STAR);
		}
		default: {
			if (isAlphaNum(scanner.start)) {
				return makeIdentifier();
			} else {
				fprintf(stderr, "INVALID TOKEN: %s\n", scanner.start);
				exit(EXIT_FAILURE);
			}
		}
	}
}

void printToken(Token_t tok) {
	switch(tok.type) {
		case TOKEN_TYPE_INT: {
			printf("[line %d] TOKEN: TOKEN_TYPE_INT: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_TYPE_VOID: {
			printf("[line %d] TOKEN: TOKEN_TYPE_VOID: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_OPEN_PAREN: {
			printf("[line %d] TOKEN: TOKEN_OPEN_PAREN: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_CLOSE_PAREN: {
			printf("[line %d] TOKEN: TOKEN_CLOSE_PAREN: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_OPEN_BRACE: {
			printf("[line %d] TOKEN: TOKEN_OPEN_BRACE: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_CLOSE_BRACE: {
			printf("[line %d] TOKEN: TOKEN_CLOSE_BRACE: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_SEMICOLON: {
			printf("[line %d] TOKEN: TOKEN_SEMICOLON: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_COMMA: {
			printf("[line %d] TOKEN: TOKEN_COMMA: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_NUM: {
			printf("[line %d] TOKEN: TOKEN_NUM: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_RETURN: {
			printf("[line %d] TOKEN: TOKEN_RETURN: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_IDENTIFIER: {
			printf("[line %d] TOKEN: TOKEN_IDENTIFIER: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_PLUS:
		case TOKEN_MINUS:
		case TOKEN_SLASH:
		case TOKEN_STAR: {
			printf("[line %d] TOKEN: TOKEN_PLUS: %.*s\n", tok.line, (int)tok.len, tok.start);
			break;
		}
		case TOKEN_TERMINAL: {
			return;
		}
		default: {
			printf("[line %d] TOKEN: INVALID_TOKEN\n", tok.line);
			break;
		}
	}
}

void printTokens(void) {
	for (Token_t *it=scanner.tokens->tokens; 
	it!=&scanner.tokens->tokens[scanner.tokens->len]; 
	it++) {
		printToken(*it);
	}
}

bool lex(const char *src) {
	setup(src);
	while (!isAtEnd()) {
		addToken(nextToken());
	}
	printTokens();
	return true;
}

// ======
// preprocessor.h
void runPreprocessor(const char *src, char *pp) {
	pp[len] = '\0';
	strcpy(pp, src); pp[len - 1] = 'i';
	printf("Going to run: gcc -E -P %s -o %s\n", src, pp);
	char pp_cmd[2 * len + 16];
	sprintf(pp_cmd, "gcc -E -P %s -o %s", src, pp);
	printf("%s\n", pp_cmd);
	if (system(pp_cmd) < 0) {
		perror(": system");
		exit(EXIT_FAILURE);
	}
}

// ======
// compiler.h
void compile2(const char *pp, char *assembly);
void compile(const char *pp, char *assembly) {
	assembly[len] = '\0';
	strcpy(assembly, pp); assembly[len - 1] = 's';
	compile2(pp, assembly);
	char asm_cmd[2 * len + 16];
	sprintf(asm_cmd, "gcc %s -S -o %s", pp, assembly);
	printf("%s\n", asm_cmd);
	if (system(asm_cmd) < 0) {
		perror(": system");
		exit(EXIT_FAILURE);
	}
	if (!retainPreProcessedSource && remove(pp)) {
		perror(": remove");
		exit(EXIT_FAILURE);
	}
}

char *readSource(const char *pp) {
	FILE *fh = fopen(pp, "r");
	if (!fh) {
		char src[len + 1];
		strcpy(src, pp);
		src[len - 1] = 'c';
		fprintf(stderr, "Cannot open %s\n", src);
		exit(EXIT_FAILURE);
	}
	fseek(fh, 0, SEEK_END);
	size_t bytesRead = (size_t)ftell(fh);
	rewind(fh);
	char *src = (char *)malloc(bytesRead + 1);
	fread(src, bytesRead, 1, fh);
	fclose(fh);
	src[bytesRead] = '\0';
	printf("src\n%s\n", src);
	return src;
}

void compile2(const char *pp, char *assembly) {
	assembly[len] = '\0';
	strcpy(assembly, pp); assembly[len - 1] = 's';
	// the real compiler
	char *src = readSource(pp);
	bool lexrc = lex(src);
	free(src);
	teardown();
	if (ONLY_LEX) {
		lexrc ? exit(EXIT_SUCCESS) : exit(EXIT_FAILURE);
	}
}

// ======
// assembler.h
void assemble(const char *assembly, char *binary) {
	if (!ONLY_EMITASM) {
		binary[len - 2] = '\0';
		strncpy(binary, assembly, len - 2); 
		char bin_cmd[2 * len + 8];
		sprintf(bin_cmd, "gcc %s -o %s", assembly, binary);
		printf("%s\n", bin_cmd);
		if (system(bin_cmd) < 0) {
			perror(": system");
			exit(EXIT_FAILURE);
		}
		if (remove(assembly)) {
			perror(": remove");
			exit(EXIT_FAILURE);
		}
	}
}


// ======
// parser.h
/**
	DA GRAMMA:
	----------

		<program> ::= <function>* | <struct>* | <enum>* | <varDecl>* ;
		<function> ::= <type> <identifier> "(" <params> ")" "{" <statement> "}" ;
		<type> ::= "int" | "void" ;
		<identifer> ::= IDENTIFIER ;
		<params> ::= (<type> <identifier> ",")* (<type> <identifier>)? ;
		<statement> ::= returnStmt | ifStmt | blockStmt ;
		<blockStmt> ::= (<statement>)+ ;
		<returnStmt> ::= "return" <expr> ;
		<ifStmt> ::= "if" "(" <expr> ")" "{" <statement> "}" ["else" "{" <statement> "}"] ;
		<expr> ::= NUMBER | STRING | BINOP | UNOP ;

*/

typedef enum {
	INT,
	VOID
} Type_e;

typedef struct {
	Type_e type;
	Token_t identifier;
} Param_t;

typedef enum {
	NUMBER,
	STRING,
	BINOP,
	UNOP
} ExprType_e;

typedef struct {
	ExprType_e type;
} Expr_t;

typedef enum {
	FUNC_DECL,
	RETURN,
	IF,
	BLOCK
} StmtType_e;

typedef struct Stmt_t Stmt_t;
typedef struct {
	ssize_t cap;
	ssize_t len;
	struct Stmt_t **stmts;
} StmtList_t;

struct Stmt_t {
	StmtType_e type;
};

typedef struct {
	Stmt_t stmt;
	Expr_t expr;
} ReturnStmt_t;

typedef struct {
	Stmt_t stmt;
	StmtList_t stmts;
} BlockStmt_t;

typedef struct {
	Stmt_t stmt;
	Expr_t ifCond;
	BlockStmt_t thenStmt;
	BlockStmt_t elseStmt;
} IfStmt_t;

typedef struct {
	Stmt_t stmt;
	Type_e returnType;
	Param_t *params;
	StmtList_t body;
} FuncDecl_t;

typedef struct {
	const Token_t *start;
	const Token_t *curr;
	StmtList_t *stmts;
} Parser_t;

Parser_t parser;

StmtList_t *createStmtList(void) {
	StmtList_t *list = (StmtList_t *)malloc(sizeof(StmtList_t));
	if (!list) {
		perror(": createStmtList::malloc");
		exit(EXIT_FAILURE);
	}
	list->len = -1;
	list->cap = -1;
	list->stmts = NULL;
	return list;
}

bool addStmt(Stmt_t *stmt) {
	if (parser.stmts->len >= parser.stmts->cap) {
		ssize_t oldCap = parser.stmts->cap;
		if (oldCap == -1) {
			parser.stmts->cap = 8;
			parser.stmts->len = 0;
		} else {
			parser.stmts->cap *= 2;
		}
		Stmt_t **list = (Stmt_t **)realloc(parser.stmts->stmts, (size_t)parser.stmts->cap * sizeof(Stmt_t *));
		if (!list) {
			perror(": addStmt::realloc");
			// exit(EXIT_FAILURE);
			return false;
		}
		parser.stmts->stmts = list;
	}
	parser.stmts->stmts[parser.stmts->len++] = stmt;
	return true;
}

void initParser(void) {
	parser.start = parser.curr = scanner.tokens->tokens;
	parser.stmts = createStmtList();
}

FuncDecl_t *newFuncDecl(void) {
	FuncDecl_t *funcDecl = (FuncDecl_t *)malloc(sizeof(FuncDecl_t));
	funcDecl->stmt.type = FUNC_DECL;
	return funcDecl;
}
void function(void) {
	FuncDecl_t *funcDecl = newFuncDecl();
	// TODO: recursive descent logic here :)
	addStmt(&funcDecl->stmt);
}

bool parserCheck(TokenType_e type);
void top(void) {
	// <program> ::= <function>* ;
	if (parserCheck(TOKEN_TYPE_INT) || parserCheck(TOKEN_TYPE_VOID)) {
		function();
	}
	// TODO: other top-level productions need to be added
}

bool parserCheck(TokenType_e type) {
	if (parser.curr->type == type) {
		return true;
	}
	return false;
}

// ======
// driver
int main(int argc, char *argv[]) {
	/**
	 * This is the entry point to the compiler *driver*
	 * The compiler driver is an orchestrator of the compiler pipeline
	 * 1. preprocess: Accept input *.c file, pass it to preprocessor, emit *.i
	 * 2. compile: Pass *.i file to compiler, compile, emit *.s, delete *.i
	 * 3. assemble: Pass *.s to assembler, assemble & link, emit binary, delete *.s
	 */
	if (argc <= 1) {
		usage();
		exit(1);
	}

	int opt;
	while ((opt = getopt_long(argc, argv, "lpcSE", longOptions, NULL)) != -1) {
		switch (opt) {
			case 'l': {
				mode = JCC_LEX_ONLY;
				break;
			}
			case 'p': {
				mode = JCC_PARSE_ONLY;
				break;
			}
			case 'c': {
				mode = JCC_CODEGEN_ONLY;
				break;
			}
			case 'E': {
				retainPreProcessedSource = true;
				break;
			}
			case 'S': {
				mode = JCC_EMITASM_ONLY;
				break;
			}
		}
	}

	char *src = argv[optind]; len = strlen(src);
	if (strcmp(&src[len - 2], ".c")) {
		fprintf(stderr, "error: %s does not have expected '.c' extension\n", src);
		exit(1);
	}

	char pp[len + 1];
	char assembly[len + 1];
	char binary[len - 1];
	runPreprocessor(src, pp);
	compile(pp, assembly);
	assemble(assembly, binary);

	return 0;
}
