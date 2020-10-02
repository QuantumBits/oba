#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oba_compiler.h"
#include "oba_memory.h"
#include "oba_opcodes.h"
#include "oba_token.h"
#include "oba_vm.h"

typedef struct {
  ObaVM* vm;
  Token current;
  Token previous;

  // Whether the parser encountered an error.
  // Code is not executed if this is true.
  bool hasError;

  const char* tokenStart;
  const char* currentChar;
  const char* source;

  int currentLine;
} Parser;

typedef struct {
  Token token;
  int scopeDepth;
} Local;

struct sCompiler {
  Local locals[MAX_LOCALS];
  int localCount;
  int currentScope;
  Parser* parser;
};

void initCompiler(Compiler* compiler, Parser* parser) {
  compiler->parser = parser;
  compiler->parser->vm->compiler = compiler;
  compiler->localCount = 0;
  compiler->currentScope = 0;

  compiler->parser->vm->chunk = (Chunk*)reallocate(NULL, 0, sizeof(Chunk));
  initChunk(compiler->parser->vm->chunk);
  compiler->parser->vm->ip = compiler->parser->vm->chunk->code;
}

// Forward declarations because the grammar is recursive.
static void ignoreNewlines(Compiler*);
static void grouping(Compiler*, bool);
static void unaryOp(Compiler*, bool);
static void infixOp(Compiler*, bool);
static void identifier(Compiler*, bool);
static void literal(Compiler*, bool);
static void string(Compiler*, bool);

static void declaration(Compiler*);

// Bytecode -------------------------------------------------------------------

static void emitByte(Compiler* compiler, int byte) {
  writeChunk(compiler->parser->vm->chunk, byte);
}

static void emitOp(Compiler* compiler, OpCode code) {
  emitByte(compiler, code);
}

// Adds [value] the the Vm's constant pool.
// Returns the address of the new constant within the pool.
static int addConstant(Compiler* compiler, Value value) {
  writeValueArray(&compiler->parser->vm->chunk->constants, value);
  return compiler->parser->vm->chunk->constants.count - 1;
}

// Registers [value] as a constant value.
//
// Constants are OP_CONSTANT followed by a 16-bit argument which points to
// the constant's location in the constant pool.
static void emitConstant(Compiler* compiler, Value value) {
  // Register the constant in the VM's constant pool.
  int constant = addConstant(compiler, value);
  emitOp(compiler, OP_CONSTANT);
  emitByte(compiler, constant);
}

static void emitBool(Compiler* compiler, Value value) {
  AS_BOOL(value) ? emitOp(compiler, OP_TRUE) : emitOp(compiler, OP_FALSE);
}

static int declareGlobal(Compiler* compiler, Value name) {
  return addConstant(compiler, name);
}

static void defineGlobal(Compiler* compiler, int global) {
  emitOp(compiler, OP_DEFINE_GLOBAL);
  emitByte(compiler, global);
}

static void addLocal(Compiler* compiler) {
  Local local = compiler->locals[compiler->localCount++];
  local.scopeDepth = compiler->currentScope;
  local.token = compiler->parser->previous;
}

static int declareVariable(Compiler* compiler, Value name) {
  if (compiler->currentScope > 0) {
    addLocal(compiler);
    // Locals live on the stack, so we didn't add a constant. Return a dummy
    // value.
    return 0;
  }
  return addConstant(compiler, name);
}

static void defineVariable(Compiler* compiler, int variable) {
  // Local variables live on the stack, so we don't need to define anything.
  if (compiler->currentScope > 0)
    return;

  defineGlobal(compiler, variable);
}

static void getGlobal(Compiler* compiler, Value name) {
  int global = addConstant(compiler, name);
  emitOp(compiler, OP_GET_GLOBAL);
  emitByte(compiler, global);
}

static void getLocal(Compiler* compiler, Value name) {
  int global = addConstant(compiler, name);
  emitOp(compiler, OP_GET_LOCAL);
  emitByte(compiler, global);
}

// Finds a local variabled named [name] in the current scope.
// Returns a negative number if it is not found.
static int lookupLocal(Compiler* compiler, Value name) { return -1; }

static void getVariable(Compiler* compiler, Value name) {
  uint8_t getOp;
  int local = lookupLocal(compiler, name);
  if (local < 0) {
    getGlobal(compiler, name);
  } else {
    getLocal(compiler, name);
  }
}

// Grammar --------------------------------------------------------------------

// Parse precedence table.
// Greater value == greater precedence.
typedef enum {
  PREC_NONE,
  PREC_LOWEST,
  PREC_COND,    // < > <= >= != ==
  PREC_SUM,     // + -
  PREC_PRODUCT, // * /
} Precedence;

typedef void (*GrammarFn)(Compiler*, bool canAssign);

// Oba grammar rules.
//
// The Pratt parser tutorial at stuffwithstuff describes these as "Parselets".
// The difference between this implementation and parselets is that the prefix
// and infix parselets for the same token are stored on this one struct instead
// in separate tables. This means the same rule implements different operations
// that share the same lexeme.
//
// Additionally, our parser stores tokens internally, so GrammarFn does not
// accept the previous token as an argument, it accesses it using
// parser->previous.
typedef struct {
  GrammarFn prefix;
  GrammarFn infix;
  Precedence precedence;
  const char* name;
} GrammarRule;

// clang-format off

// Pratt parser rules.
//
// See: http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/
#define UNUSED                     { NULL, NULL, PREC_NONE, NULL }
#define PREFIX(fn)                 { fn, NULL, PREC_NONE, NULL }
#define INFIX_OPERATOR(prec, name) { NULL, infixOp, prec, name }

GrammarRule rules[] =  {
  /* TOK_NOT       */ PREFIX(unaryOp),
  /* TOK_ASSIGN    */ INFIX_OPERATOR(PREC_COND, "="),
  /* TOK_GT        */ INFIX_OPERATOR(PREC_COND, ">"),
  /* TOK_LT        */ INFIX_OPERATOR(PREC_COND, "<"),
  /* TOK_GTE       */ INFIX_OPERATOR(PREC_COND, ">="),
  /* TOK_LTE       */ INFIX_OPERATOR(PREC_COND, "<="),
  /* TOK_EQ        */ INFIX_OPERATOR(PREC_COND, "=="),
  /* TOK_NEQ       */ INFIX_OPERATOR(PREC_COND, "!="),
  /* TOK_LPAREN    */ PREFIX(grouping),  
  /* TOK_RPAREN    */ UNUSED, 
  /* TOK_LBRACK    */ UNUSED,  
  /* TOK_RBRACK    */ UNUSED, 
  /* TOK_PLUS      */ INFIX_OPERATOR(PREC_SUM, "+"),
  /* TOK_MINUS     */ INFIX_OPERATOR(PREC_SUM, "-"),
  /* TOK_MULTIPLY  */ INFIX_OPERATOR(PREC_PRODUCT, "*"),
  /* TOK_DIVIDE    */ INFIX_OPERATOR(PREC_PRODUCT, "/"),
  /* TOK_IDENT     */ PREFIX(identifier),
  /* TOK_NUMBER    */ PREFIX(literal),
  /* TOK_STRING    */ PREFIX(string),
  /* TOK_NEWLINE   */ UNUSED, 
  /* TOK_DEBUG     */ UNUSED,
  /* TOK_LET       */ UNUSED,
  /* TOK_TRUE      */ PREFIX(literal),
  /* TOK_FALSE     */ PREFIX(literal),
  /* TOK_ERROR     */ UNUSED,  
  /* TOK_EOF       */ UNUSED,
};

// Gets the [GrammarRule] associated with tokens of [type].
static GrammarRule* getRule(TokenType type) {
  return &rules[type];
}

typedef struct {
  const char* lexeme;
  size_t length;
  TokenType type;
} Keyword;

static Keyword keywords[] = {
    {"debug", 5, TOK_DEBUG},
    {"false", 5, TOK_FALSE},
    {"let",   3, TOK_LET},
    {"true",  4, TOK_TRUE},
    {NULL,    0, TOK_EOF}, // Sentinel to mark the end of the array.
};

// clang-format on

// Lexing ---------------------------------------------------------------------

static void printError(Compiler* compiler, int line, const char* label,
                       const char* format, va_list args) {
  char message[1024];
  int length = sprintf(message, "%s: ", label);
  length += vsprintf(message + length, format, args);
  // TODO(kendal): Ensure length < 1024
  printf("%s\n", message);
}

static void lexError(Compiler* compiler, const char* format, ...) {
  compiler->parser->hasError = true;

  va_list args;
  va_start(args, format);
  printError(compiler, compiler->parser->currentLine, "Error", format, args);
  va_end(args);
}

static void error(Compiler* compiler, const char* format, ...) {
  compiler->parser->hasError = true;

  // The lexer already reported this error.
  if (compiler->parser->previous.type == TOK_ERROR)
    return;

  va_list args;
  va_start(args, format);
  printError(compiler, compiler->parser->currentLine, "Error", format, args);
  va_end(args);
}

// Parsing --------------------------------------------------------------------

static char peekChar(Compiler* compiler) {
  return *compiler->parser->currentChar;
}

static char nextChar(Compiler* compiler) {
  char c = peekChar(compiler);
  compiler->parser->currentChar++;
  if (c == '\n')
    compiler->parser->currentLine++;
  return c;
}

static bool matchChar(Compiler* compiler, char c) {
  if (peekChar(compiler) != c)
    return false;
  nextChar(compiler);
  return true;
}

// Returns the type of the current token.
static TokenType peek(Compiler* compiler) {
  return compiler->parser->current.type;
}

static void makeToken(Compiler* compiler, TokenType type) {
  compiler->parser->current.type = type;
  compiler->parser->current.start = compiler->parser->tokenStart;
  compiler->parser->current.length =
      (int)(compiler->parser->currentChar - compiler->parser->tokenStart);
  compiler->parser->current.line = compiler->parser->currentLine;

  // Make line tokens appear on the line containing the "\n".
  if (type == TOK_NEWLINE)
    compiler->parser->current.line--;
}

static void makeNumber(Compiler* compiler) {
  double value = strtod(compiler->parser->tokenStart, NULL);
  compiler->parser->current.value = OBA_NUMBER(value);
  makeToken(compiler, TOK_NUMBER);
}

static void makeString(Compiler* compiler) { makeToken(compiler, TOK_STRING); }

static bool isName(char c) { return isalpha(c) || c == '_'; }

static bool isNumber(char c) { return isdigit(c); }

// Finishes lexing a string.
static void readString(Compiler* compiler) {
  // TODO(kendal): Handle strings with escaped quotes.
  while (peekChar(compiler) != '"') {
    nextChar(compiler);
  }
  nextChar(compiler);
  makeString(compiler);
}

// Finishes lexing an identifier.
static void readName(Compiler* compiler) {
  while (isName(peekChar(compiler)) || isdigit(peekChar(compiler))) {
    nextChar(compiler);
  }

  size_t length = compiler->parser->currentChar - compiler->parser->tokenStart;
  for (int i = 0; keywords[i].lexeme != NULL; i++) {
    if (length == keywords[i].length &&
        memcmp(compiler->parser->tokenStart, keywords[i].lexeme, length) == 0) {
      makeToken(compiler, keywords[i].type);
      return;
    }
  }
  makeToken(compiler, TOK_IDENT);
}

static void readNumber(Compiler* compiler) {
  while (isNumber(peekChar(compiler))) {
    nextChar(compiler);
  }
  makeNumber(compiler);
}

static void skipLineComment(Compiler* compiler) {
  // A comment goes until the end of the line.
  while (peekChar(compiler) != '\n' && peekChar(compiler) != '\0') {
    nextChar(compiler);
  }
}

// Lexes the next token and stores it in [parser.current].
static void nextToken(Compiler* compiler) {
  compiler->parser->previous = compiler->parser->current;

  if (compiler->parser->current.type == TOK_EOF)
    return;

#define IF_MATCH_NEXT(next, matched, unmatched)                                \
  do {                                                                         \
    if (matchChar(compiler, next))                                             \
      makeToken(compiler, matched);                                            \
    else                                                                       \
      makeToken(compiler, unmatched);                                          \
  } while (0)

  while (peekChar(compiler) != '\0') {
    compiler->parser->tokenStart = compiler->parser->currentChar;
    char c = nextChar(compiler);
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      break;
    case '\n':
      makeToken(compiler, TOK_NEWLINE);
      return;
    case '(':
      makeToken(compiler, TOK_LPAREN);
      return;
    case ')':
      makeToken(compiler, TOK_RPAREN);
      return;
    case '{':
      makeToken(compiler, TOK_LBRACK);
      return;
    case '}':
      makeToken(compiler, TOK_RBRACK);
      return;
    case '+':
      makeToken(compiler, TOK_PLUS);
      return;
    case '-':
      makeToken(compiler, TOK_MINUS);
      return;
    case '*':
      makeToken(compiler, TOK_MULTIPLY);
      return;
    case '!':
      IF_MATCH_NEXT('=', TOK_NEQ, TOK_NOT);
      return;
    case '>':
      IF_MATCH_NEXT('=', TOK_GTE, TOK_GT);
      return;
    case '<':
      IF_MATCH_NEXT('=', TOK_LTE, TOK_LT);
      return;
    case '=':
      IF_MATCH_NEXT('=', TOK_EQ, TOK_ASSIGN);
      return;
    case '/':
      if (matchChar(compiler, '/')) {
        skipLineComment(compiler);
        break;
      }

      makeToken(compiler, TOK_DIVIDE);
      return;
    case '"':
      readString(compiler);
      return;
    default:
      if (isName(c)) {
        readName(compiler);
        return;
      }
      if (isNumber(c)) {
        readNumber(compiler);
        return;
      }
      lexError(compiler, "Invalid character '%c'.", c);
      compiler->parser->current.type = TOK_ERROR;
      compiler->parser->current.length = 0;
      return;
    }
  }

#undef IF_MATCH_NEXT

  // No more source left.
  compiler->parser->tokenStart = compiler->parser->currentChar;
  makeToken(compiler, TOK_EOF);
}

// Returns true iff the next token has the [expected] Type.
static bool match(Compiler* compiler, TokenType expected) {
  if (peek(compiler) != expected)
    return false;
  nextToken(compiler);
  return true;
}

static bool matchLine(Compiler* compiler) {
  if (!match(compiler, TOK_NEWLINE))
    return false;
  while (match(compiler, TOK_NEWLINE))
    ;
  return true;
}

static void ignoreNewlines(Compiler* compiler) { matchLine(compiler); }

// Moves past the next token which must have the [expected] type.
// If the type is not as expected, this emits an error and attempts to continue
// parsing at the next token.
static void consume(Compiler* compiler, TokenType expected,
                    const char* errorMessage) {
  nextToken(compiler);
  if (compiler->parser->previous.type != expected) {
    error(compiler, errorMessage);
    if (compiler->parser->current.type == expected) {
      nextToken(compiler);
    }
  }
}

// AST ------------------------------------------------------------------------

static void parse(Compiler* compiler, int precedence) {
  nextToken(compiler);
  Token token = compiler->parser->previous;

  GrammarFn prefix = rules[token.type].prefix;
  if (prefix == NULL) {
    error(compiler, "Parse error %d", token.type);
    return;
  }

  bool canAssign = false;
  prefix(compiler, canAssign);

  while (precedence < rules[compiler->parser->current.type].precedence) {
    nextToken(compiler);
    GrammarFn infix = rules[compiler->parser->previous.type].infix;
    infix(compiler, canAssign);
  }
}

static void expression(Compiler* compiler) { parse(compiler, PREC_LOWEST); }

static void assignStmt(Compiler* compiler) {
  consume(compiler, TOK_IDENT, "Expected an identifier.");
  // Get the name, but don't declare it yet; A variable should not be in scope
  // in its own initializer.
  Value name = OBJ_VAL(copyString(compiler->parser->previous.start,
                                  compiler->parser->previous.length));

  // Compile the initializer.
  consume(compiler, TOK_ASSIGN, "Expected '='");
  expression(compiler);

  // Now define the variable.
  int variable = declareVariable(compiler, name);
  defineVariable(compiler, variable);
}

static void debugStmt(Compiler* compiler) {
  expression(compiler);
  emitOp(compiler, OP_DEBUG);
}

static void enterScope(Compiler* compiler) { compiler->currentScope++; }
static void exitScope(Compiler* compiler) { compiler->currentScope--; }

static void block(Compiler* compiler) {
  enterScope(compiler);

  ignoreNewlines(compiler);

  do {
    declaration(compiler);
    ignoreNewlines(compiler);
  } while (peek(compiler) != TOK_RBRACK && peek(compiler) != TOK_EOF);

  ignoreNewlines(compiler);
  consume(compiler, TOK_RBRACK, "Expected '}' at the end of block");
  exitScope(compiler);
}

static void statement(Compiler* compiler) {
  if (match(compiler, TOK_DEBUG)) {
    debugStmt(compiler);
  } else if (match(compiler, TOK_LBRACK)) {
    block(compiler);
  } else {
    expression(compiler);
  }
}

static void declaration(Compiler* compiler) {
  if (match(compiler, TOK_LET)) {
    assignStmt(compiler);
  } else {
    statement(compiler);
  }
}

// A parenthesized expression.
static void grouping(Compiler* compiler, bool canAssign) {
  expression(compiler);
  consume(compiler, TOK_RPAREN, "Expected ')' after expression.");
}

static void string(Compiler* compiler, bool canAssign) {
  // +1 and -2 to omit the leading and traling '"'.
  emitConstant(compiler,
               OBJ_VAL(copyString(compiler->parser->previous.start + 1,
                                  compiler->parser->previous.length - 2)));
}

static void identifier(Compiler* compiler, bool canAssign) {
  Value name = OBJ_VAL(copyString(compiler->parser->previous.start,
                                  compiler->parser->previous.length));
  getVariable(compiler, name);
}

static void literal(Compiler* compiler, bool canAssign) {
  switch (compiler->parser->previous.type) {
  case TOK_TRUE:
    emitBool(compiler, OBA_BOOL(true));
    break;
  case TOK_FALSE:
    emitBool(compiler, OBA_BOOL(false));
    break;
  case TOK_NUMBER:
    emitConstant(compiler, compiler->parser->previous.value);
    break;
  default:
    error(compiler, "Expected a boolean or number value.");
  }
}

static void unaryOp(Compiler* compiler, bool canAssign) {
  GrammarRule* rule = getRule(compiler->parser->previous.type);
  TokenType opType = compiler->parser->previous.type;

  ignoreNewlines(compiler);

  // Compile the right hand side (right-associative).
  parse(compiler, rule->precedence);

  switch (opType) {
  case TOK_NOT:
    emitOp(compiler, OP_NOT);
    break;
  default:
    error(compiler, "Invalid operator %s", rule->name);
  }
}

static void infixOp(Compiler* compiler, bool canAssign) {
  GrammarRule* rule = getRule(compiler->parser->previous.type);
  TokenType opType = compiler->parser->previous.type;

  ignoreNewlines(compiler);

  // Compile the right hand side (right-associative).
  parse(compiler, rule->precedence);

  switch (opType) {
  case TOK_PLUS:
    emitOp(compiler, OP_ADD);
    return;
  case TOK_MINUS:
    emitOp(compiler, OP_MINUS);
    return;
  case TOK_MULTIPLY:
    emitOp(compiler, OP_MULTIPLY);
    return;
  case TOK_DIVIDE:
    emitOp(compiler, OP_DIVIDE);
    return;
  case TOK_GT:
    emitOp(compiler, OP_GT);
    return;
  case TOK_LT:
    emitOp(compiler, OP_LT);
    return;
  case TOK_GTE:
    emitOp(compiler, OP_GTE);
    return;
  case TOK_LTE:
    emitOp(compiler, OP_LTE);
    return;
  case TOK_EQ:
    emitOp(compiler, OP_EQ);
    return;
  case TOK_NEQ:
    emitOp(compiler, OP_NEQ);
    return;
  case TOK_ASSIGN:
    emitOp(compiler, OP_ASSIGN);
    return;
  default:
    error(compiler, "Invalid operator %s", rule->name);
    return;
  }
}

// Compiling ------------------------------------------------------------------

bool obaCompile(ObaVM* vm, const char* source) {
  // Skip the UTF-8 BOM if there is one.
  if (strncmp(source, "\xEF\xBB\xBF", 3) == 0)
    source += 3;

  Parser parser;
  parser.vm = vm;
  parser.source = source;
  parser.tokenStart = source;
  parser.currentChar = source;
  parser.currentLine = 1;
  parser.current.type = TOK_ERROR;
  parser.current.start = source;
  parser.current.length = 0;
  parser.current.line = 0;
  parser.hasError = false;

  Compiler compiler;
  initCompiler(&compiler, &parser);

  nextToken(&compiler);
  ignoreNewlines(&compiler);

  while (!match(&compiler, TOK_EOF)) {
    declaration(&compiler);
    // If no newline, the file must end on this line.
    if (!matchLine(&compiler)) {
      consume(&compiler, TOK_EOF, "Expected end of file.");
      break;
    }
  }

  emitOp(&compiler, OP_EXIT);
  return parser.hasError;
}
