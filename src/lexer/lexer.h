#ifndef LEXER_H
#define LEXER_H
#include <stdio.h>

typedef enum 
{

    TK_SENSOR,
    TK_ATUADOR,
    TK_PORTA,
    TK_ANALOGICO,
    TK_DIGITAL,
    TK_SE,
    TK_ENTAO,
    TK_SENAO,
    TK_FIM_SE,
    TK_ENQUANTO,
    TK_FIM_ENQUANTO,
    TK_LER,
    TK_ESCREVER,

    TK_SOMA,
    TK_SUB,
    TK_MULT,
    TK_DIV,

    TK_MENOR,
    TK_MENOR_IGUAL,
    TK_MAIOR,
    TK_MAIOR_IGUAL,
    TK_IGUAL,
    TK_DIFERENTE,

    TK_ATRIB,

    TK_PONTO,
    TK_DOIS_PONTOS,
    TK_VIRGULA,
    TK_ABRE_PAR,
    TK_FECHA_PAR,

    TK_NUMERO,
    TK_IDENTIFICADOR,

    TK_EOF,
    TK_ERRO

} TokenType;

typedef struct 
{
    TokenType tipo;
    char lexema[128];
    int linha;
} Token;

Token getToken(FILE *arquivo);
const char *tokenToString(TokenType tipo);
void lexer_init();

#endif