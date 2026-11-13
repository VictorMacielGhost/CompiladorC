#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

static int compiler_read_int(int *value)
{
    char text[128];
    char *end;
    long parsed;
    int next;

    if (scanf("%127s", text) != 1)
        goto invalid_input;
    if (strlen(text) == sizeof(text) - 1) {
        next = getchar();
        if (next != EOF && !isspace((unsigned char)next)) {
            do { next = getchar(); }
            while (next != EOF && !isspace((unsigned char)next));
            goto invalid_input;
        }
        if (next != EOF)
            ungetc(next, stdin);
    }

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX)
        goto invalid_input;

    *value = (int)parsed;
    return 1;

invalid_input:
    fputs("Erro em execucao: entrada nao e um inteiro valido no intervalo de int.\n", stderr);
    return 0;
}

static int compiler_add_helper(int left, int right)
{
    if ((right > 0 && left > INT_MAX - right) ||
        (right < 0 && left < INT_MIN - right)) {
        fputs("Erro em execucao: overflow na soma de inteiros.\n", stderr);
        exit(EXIT_FAILURE);
    }
    return left + right;
}

static int compiler_subtract_helper(int left, int right)
{
    if ((right < 0 && left > INT_MAX + right) ||
        (right > 0 && left < INT_MIN + right)) {
        fputs("Erro em execucao: overflow na subtracao de inteiros.\n", stderr);
        exit(EXIT_FAILURE);
    }
    return left - right;
}

static int compiler_multiply_helper(int left, int right)
{
    if ((left > 0 && ((right > 0 && left > INT_MAX / right) ||
            (right < 0 && right < INT_MIN / left))) ||
        (left < 0 && ((right > 0 && left < INT_MIN / right) ||
            (right < 0 && left < INT_MAX / right)))) {
        fputs("Erro em execucao: overflow na multiplicacao de inteiros.\n", stderr);
        exit(EXIT_FAILURE);
    }
    return left * right;
}

static int compiler_div_helper(int dividend, int divisor)
{
    if (divisor == 0) {
        fputs("Erro em execucao: divisao por zero.\n", stderr);
        exit(EXIT_FAILURE);
    }
    if (dividend == INT_MIN && divisor == -1) {
        fputs("Erro em execucao: resultado da divisao fora do intervalo int.\n", stderr);
        exit(EXIT_FAILURE);
    }
    return dividend / divisor;
}

int domus_temperatura;
int domus_claridade;
int domus_cortina;
int domus_ventilador;
int domus_x;
int domus_w;
int domus_asd;

int main(void)
{
    if (!compiler_read_int(&domus_x)) return 1;
    domus_w = compiler_multiply_helper((compiler_add_helper(6, 8)), (compiler_subtract_helper(5, 7)));
    if ((((domus_temperatura < 30)))) {
        domus_cortina = 1;
        if (domus_cortina != 0 && domus_cortina != 1) {
            fprintf(stderr, "Valor invalido para o atuador digital cortina na linha 8.\n");
            return 1;
        }
        domus_ventilador = 80;
    } else {
        domus_cortina = 0;
        if (domus_cortina != 0 && domus_cortina != 1) {
            fprintf(stderr, "Valor invalido para o atuador digital cortina na linha 11.\n");
            return 1;
        }
        domus_ventilador = 0;
    }
    if (domus_claridade > 20) {
        domus_cortina = 0;
        if (domus_cortina != 0 && domus_cortina != 1) {
            fprintf(stderr, "Valor invalido para o atuador digital cortina na linha 14.\n");
            return 1;
        }
    }
    domus_asd = compiler_div_helper(8, domus_x);
    domus_asd = compiler_add_helper(domus_asd, 2);
    printf("%d\n", (compiler_multiply_helper(domus_asd, 2)));
    while (domus_asd > 1) {
        domus_asd = compiler_subtract_helper(domus_asd, 1);
    }
    return 0;
}
