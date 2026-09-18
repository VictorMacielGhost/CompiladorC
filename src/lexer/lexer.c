#include "lexer.h"
#include <ctype.h>
#include <string.h>
#include <cli/cli.h>
#include <logger/logger.h>

void lexer_init()
{
    Token tk;
    FILE *arquivo = fopen(cli_get_input_file(), "r");

    if(!arquivo)
    {
        log_msg(LOG_LEVEL_ERROR, "Lexer could not open input file: %s", cli_get_input_file());
        return;
    }

    log_msg(LOG_LEVEL_INFO, "Lexer started");
    do
    {
        tk = getToken(arquivo);

    } while(tk.tipo != TK_EOF);

    log_msg(LOG_LEVEL_INFO, "Lexer finished");
    fclose(arquivo);
}

static int linhaAtual = 1;

TokenType palavraReservada(const char *lexema)
{
    TokenType tipo = TK_IDENTIFICADOR;

    if(strcmp(lexema,"sensor")==0) tipo = TK_SENSOR;
    else if(strcmp(lexema,"atuador")==0) tipo = TK_ATUADOR;
    else if(strcmp(lexema,"porta")==0) tipo = TK_PORTA;
    else if(strcmp(lexema,"analogico")==0) tipo = TK_ANALOGICO;
    else if(strcmp(lexema,"digital")==0) tipo = TK_DIGITAL;
    else if(strcmp(lexema,"se")==0) tipo = TK_SE;
    else if(strcmp(lexema,"entao")==0) tipo = TK_ENTAO;
    else if(strcmp(lexema,"senao")==0) tipo = TK_SENAO;
    else if(strcmp(lexema,"fim_se")==0) tipo = TK_FIM_SE;
    else if(strcmp(lexema,"enquanto")==0) tipo = TK_ENQUANTO;
    else if(strcmp(lexema,"fim_enquanto")==0) tipo = TK_FIM_ENQUANTO;
    else if(strcmp(lexema,"ler")==0) tipo = TK_LER;
    else if(strcmp(lexema,"escrever")==0) tipo = TK_ESCREVER;

    log_msg(LOG_LEVEL_DEBUG, "Lexeme '%s' classified as %s", lexema, tokenToString(tipo));
    return tipo;
}

Token criarToken(TokenType tipo, const char *lexema)
{
    Token t;

    t.tipo = tipo;
    t.linha = linhaAtual;

    strcpy(t.lexema, lexema);

    log_msg(
        LOG_LEVEL_INFO,
        "Lexer token: line=%d type=%s lexeme=[%s]",
        t.linha,
        tokenToString(t.tipo),
        t.lexema
    );

    return t;
}

Token getToken(FILE *arquivo)
{
    int c;

    while((c = fgetc(arquivo)) != EOF)
    {
        if(c == '\n')
        {
            linhaAtual++;
            log_msg(LOG_LEVEL_DEBUG, "Found newline at %d, jumping to line %d", (linhaAtual - 1), linhaAtual);
            continue;
        }

        if(isspace(c))
            continue;

        if(isalpha(c))
        {
            char buffer[128];
            int i = 0;

            do
            {
                buffer[i++] = c;
                c = fgetc(arquivo);
            }
            while(isalnum(c) || c == '_');

            buffer[i] = '\0';

            if(c != EOF)
                ungetc(c, arquivo);

            return criarToken(
                palavraReservada(buffer),
                buffer
            );
        }

        if(isdigit(c))
        {
            char buffer[128];
            int i = 0;

            do
            {
                buffer[i++] = c;
                c = fgetc(arquivo);
            }
            while(isdigit(c));

            buffer[i] = '\0';

            if(c != EOF)
                ungetc(c, arquivo);

            return criarToken(TK_NUMERO, buffer);
        }

        switch(c)
        {
            case '+':
                return criarToken(TK_SOMA, "+");

            case '-':
                return criarToken(TK_SUB, "-");

            case '*':
                return criarToken(TK_MULT, "*");

            case '.':
                return criarToken(TK_PONTO, ".");

            case ':':
                return criarToken(TK_DOIS_PONTOS, ":");

            case ',':
                return criarToken(TK_VIRGULA, ",");

            case '(':
                return criarToken(TK_ABRE_PAR, "(");

            case ')':
                return criarToken(TK_FECHA_PAR, ")");

            case '/':
            {
                int prox = fgetc(arquivo);

                if(prox == '*')
                {
                    int ant = 0;

                    log_msg(LOG_LEVEL_DEBUG, "Starting block comment at line %d", linhaAtual);

                    while((c = fgetc(arquivo)) != EOF)
                    {
                        if(c == '\n')
                            linhaAtual++;

                        if(ant == '*' && c == '/')
                            break;

                        ant = c;
                    }

                    if(c == EOF)
                        log_msg(LOG_LEVEL_WARNING, "Unterminated block comment at line %d", linhaAtual);
                    else
                        log_msg(LOG_LEVEL_DEBUG, "Finished block comment at line %d", linhaAtual);

                    return getToken(arquivo);
                }

                if(prox != EOF)
                    ungetc(prox, arquivo);

                return criarToken(TK_DIV, "/");
            }

            case '<':
            {
                int prox = fgetc(arquivo);

                if(prox == '=')
                    return criarToken(TK_MENOR_IGUAL, "<=");

                if(prox == '-')
                    return criarToken(TK_ATRIB, "<-");

                if(prox != EOF)
                    ungetc(prox, arquivo);

                return criarToken(TK_MENOR, "<");
            }

            case '>':
            {
                int prox = fgetc(arquivo);

                if(prox == '=')
                    return criarToken(TK_MAIOR_IGUAL, ">=");

                if(prox != EOF)
                    ungetc(prox, arquivo);

                return criarToken(TK_MAIOR, ">");
            }

            case '=':
            {
                int prox = fgetc(arquivo);

                if(prox == '=')
                    return criarToken(TK_IGUAL, "==");

                if(prox != EOF)
                    ungetc(prox, arquivo);

                return criarToken(TK_ERRO, "=");
            }

            case '!':
            {
                int prox = fgetc(arquivo);

                if(prox == '=')
                    return criarToken(TK_DIFERENTE, "!=");

                if(prox != EOF)
                    ungetc(prox, arquivo);

                return criarToken(TK_ERRO, "!");
            }
        }

        char erro[2];
        erro[0] = (char)c;
        erro[1] = '\0';

        log_msg(LOG_LEVEL_ERROR, "Invalid character '%s' at line %d", erro, linhaAtual);

        return criarToken(TK_ERRO, erro);
    }

    return criarToken(TK_EOF, "EOF");
}

const char *tokenToString(TokenType tipo)
{
    switch(tipo)
    {
        case TK_SENSOR: return "SENSOR";
        case TK_ATUADOR: return "ATUADOR";
        case TK_PORTA: return "PORTA";
        case TK_ANALOGICO: return "ANALOGICO";
        case TK_DIGITAL: return "DIGITAL";
        case TK_SE: return "SE";
        case TK_ENTAO: return "ENTAO";
        case TK_SENAO: return "SENAO";
        case TK_FIM_SE: return "FIM_SE";
        case TK_ENQUANTO: return "ENQUANTO";
        case TK_FIM_ENQUANTO: return "FIM_ENQUANTO";
        case TK_LER: return "LER";
        case TK_ESCREVER: return "ESCREVER";

        case TK_SOMA: return "SOMA";
        case TK_SUB: return "SUB";
        case TK_MULT: return "MULT";
        case TK_DIV: return "DIV";

        case TK_MENOR: return "MENOR";
        case TK_MENOR_IGUAL: return "MENOR_IGUAL";
        case TK_MAIOR: return "MAIOR";
        case TK_MAIOR_IGUAL: return "MAIOR_IGUAL";
        case TK_IGUAL: return "IGUAL";
        case TK_DIFERENTE: return "DIFERENTE";

        case TK_ATRIB: return "ATRIB";

        case TK_PONTO: return "PONTO";
        case TK_DOIS_PONTOS: return "DOIS_PONTOS";
        case TK_VIRGULA: return "VIRGULA";
        case TK_ABRE_PAR: return "ABRE_PAR";
        case TK_FECHA_PAR: return "FECHA_PAR";

        case TK_NUMERO: return "NUMERO";
        case TK_IDENTIFICADOR: return "IDENTIFICADOR";

        case TK_EOF: return "EOF";
        case TK_ERRO: return "ERRO";

        default: return "DESCONHECIDO";
    }
}