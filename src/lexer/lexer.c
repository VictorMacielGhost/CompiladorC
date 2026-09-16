#include "lexer.h"

void lexer_init(Lexer *lexer)
{
    lexer->line = 1;
    lexer->column = 1;
}

void lexer_run(Lexer *lexer)
{
    lexer->line = 1;
    lexer->column = 1;
}