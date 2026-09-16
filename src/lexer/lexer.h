#ifndef LEXER_H
#define LEXER_H

typedef struct
{
    int line;
    int column;
    FILE *file;
    char *input;
} Lexer;

typedef enum
{
    TOKEN_ID,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_OPERATOR,
    TOKEN_KEYWORD,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_EOF,
    TOKEN_UNKNOWN
} TokenType;

typedef struct
{
    TokenType type;
    char *value;
    int line;
    int column;
} Token;

void lexer_init(Lexer *lexer);
void lexer_run(Lexer *lexer);

#endif