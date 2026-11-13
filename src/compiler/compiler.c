
#include "compiler.h"
#include <lexer/lexer.h>
#include <logger/logger.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
    int failed;
} StringBuilder;

typedef struct
{
    Token *items;
    size_t count;
    size_t capacity;
} TokenStream;

typedef enum
{
    SYMBOL_VARIABLE,
    SYMBOL_SENSOR,
    SYMBOL_ACTUATOR
} SymbolKind;

typedef enum
{
    DEVICE_INTEGER,
    DEVICE_DIGITAL
} DeviceType;

typedef struct
{
    char name[128];
    SymbolKind kind;
    DeviceType device_type;
    long port;
    int line;
} Symbol;

typedef struct
{
    TokenStream stream;
    size_t position;
    Symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    StringBuilder body;
    int indent;
    int uses_addition;
    int uses_subtraction;
    int uses_multiplication;
    int uses_division;
    int uses_input;
    int failed;
} Compiler;

typedef struct
{
    const Compiler *compiler;
    size_t position;
    size_t end;
    int constant;
    int valid;
} ConstantEvaluator;

static const char *symbol_kind_name(SymbolKind kind);
static const char *device_type_name(const Symbol *symbol);

static int builder_reserve(StringBuilder *builder, size_t additional)
{
    size_t max_size = (size_t)-1;
    size_t required;
    size_t capacity;
    char *data;

    if(builder->length == max_size ||
        additional > max_size - builder->length - 1)
        return 0;

    required = builder->length + additional + 1;
    if(required <= builder->capacity)
        return 1;

    capacity = builder->capacity ? builder->capacity : 128;
    while(capacity < required)
    {
        if(capacity > max_size / 2)
        {
            capacity = required;
            break;
        }
        capacity *= 2;
    }

    data = realloc(builder->data, capacity);
    if(!data)
        return 0;

    builder->data = data;
    builder->capacity = capacity;
    return 1;
}

static int builder_append_n(StringBuilder *builder, const char *text, size_t length)
{
    if(builder->failed || !builder_reserve(builder, length))
    {
        builder->failed = 1;
        return 0;
    }

    memcpy(builder->data + builder->length, text, length);
    builder->length += length;
    builder->data[builder->length] = '\0';
    return 1;
}

static int builder_append(StringBuilder *builder, const char *text)
{
    return builder_append_n(builder, text, strlen(text));
}

static int builder_appendf(StringBuilder *builder, const char *format, ...)
{
    va_list args;
    va_list size_args;
    int length;

    va_start(args, format);
    if(builder->failed)
    {
        va_end(args);
        return 0;
    }
    va_copy(size_args, args);
    length = vsnprintf(NULL, 0, format, size_args);
    va_end(size_args);

    if(length < 0 || !builder_reserve(builder, (size_t)length))
    {
        builder->failed = 1;
        va_end(args);
        return 0;
    }

    vsnprintf(builder->data + builder->length,
        builder->capacity - builder->length, format, args);
    builder->length += (size_t)length;
    va_end(args);
    return 1;
}

static void report_error(const char *kind, int line, const char *format, ...)
{
    const char *module = strcmp(kind, "lexico") == 0 ? "lexer" : "compiler";
    char detail[512];
    va_list args;

    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);

    fprintf(stderr, "Erro [%s/%s] na linha %d: %s\n",
        module, kind, line, detail);
    log_msg(LOG_LEVEL_ERROR, "Erro [%s/%s] na linha %d: %s",
        module, kind, line, detail);
}

static int token_stream_push(TokenStream *stream, Token token)
{
    Token *items;
    size_t capacity;

    if(stream->count == stream->capacity)
    {
        capacity = stream->capacity ? stream->capacity * 2 : 128;
        if(capacity < stream->capacity ||
            capacity > (size_t)-1 / sizeof(*items))
            return 0;
        items = realloc(stream->items, capacity * sizeof(*items));
        if(!items)
            return 0;
        stream->items = items;
        stream->capacity = capacity;
    }

    stream->items[stream->count++] = token;
    return 1;
}

static int tokenize(FILE *source, TokenStream *stream)
{
    Token token;

    log_msg(LOG_LEVEL_DEBUG, "Lexer: starting tokenization");
    do
    {
        token = getToken(source);
        if(token.tipo == TK_ERRO)
        {
            report_error("lexico", token.linha, "token invalido '%s'", token.lexema);
            return 0;
        }

        if(!token_stream_push(stream, token))
        {
            report_error("interno", token.linha, "memoria insuficiente ao armazenar tokens");
            return 0;
        }
    } while(token.tipo != TK_EOF);

    log_msg(LOG_LEVEL_DEBUG, "Lexer: tokenization completed with %zu tokens",
        stream->count);
    return 1;
}

static Token *current_token(Compiler *compiler)
{
    return &compiler->stream.items[compiler->position];
}

static void advance(Compiler *compiler)
{
    if(compiler->position + 1 < compiler->stream.count)
        compiler->position++;
}

static int parser_error(Compiler *compiler, const char *kind,
    const Token *token, const char *format, ...)
{
    char detail[512];
    va_list args;

    if(compiler->failed)
        return 0;

    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);
    report_error(kind, token->linha, "%s", detail);
    compiler->failed = 1;
    return 0;
}

static int expect(Compiler *compiler, TokenType type, const char *expected)
{
    Token *token = current_token(compiler);

    if(token->tipo != type)
        return parser_error(compiler, "sintatico", token,
            "esperado %s, encontrado %s ('%s')",
            expected, tokenToString(token->tipo), token->lexema);

    advance(compiler);
    return 1;
}

static Symbol *find_symbol(Compiler *compiler, const char *name)
{
    size_t i;

    log_msg(LOG_LEVEL_DEBUG, "Semantic: looking up identifier '%s'", name);
    for(i = 0; i < compiler->symbol_count; i++)
    {
        if(strcmp(compiler->symbols[i].name, name) == 0)
        {
            log_msg(LOG_LEVEL_DEBUG,
                "Semantic: identifier '%s' found as %s (symbol %zu)",
                name, symbol_kind_name(compiler->symbols[i].kind), i);
            return &compiler->symbols[i];
        }
    }

    log_msg(LOG_LEVEL_DEBUG,
        "Semantic: identifier '%s' is not in the symbol table", name);
    return NULL;
}

static Symbol *add_symbol(Compiler *compiler, const char *name,
    SymbolKind kind, DeviceType device_type, long port, int line)
{
    size_t i;
    Symbol *symbol;
    Token error_location = *current_token(compiler);
    error_location.linha = line;

    log_msg(LOG_LEVEL_DEBUG,
        "Semantic: declaring %s '%s' at line %d (type=%s, port=%ld)",
        kind == SYMBOL_VARIABLE ? "variable" : symbol_kind_name(kind),
        name, line, kind == SYMBOL_VARIABLE ? "integer" :
        (device_type == DEVICE_DIGITAL ? "digital" : "analog"), port);

    if(find_symbol(compiler, name))
    {
        parser_error(compiler, "semantico", &error_location,
            "identificador '%s' ja declarado", name);
        return NULL;
    }

    if(kind != SYMBOL_VARIABLE)
    {
        for(i = 0; i < compiler->symbol_count; i++)
        {
            if(compiler->symbols[i].kind != SYMBOL_VARIABLE &&
                compiler->symbols[i].port == port)
            {
                log_msg(LOG_LEVEL_DEBUG,
                    "Semantic: port %ld is already assigned to '%s'",
                    port, compiler->symbols[i].name);
                parser_error(compiler, "semantico", &error_location,
                    "porta %ld ja utilizada pelo identificador '%s'", port,
                    compiler->symbols[i].name);
                return NULL;
            }
        }
    }

    if(compiler->symbol_count == compiler->symbol_capacity)
    {
        parser_error(compiler, "interno", current_token(compiler),
            "capacidade da tabela de simbolos excedida");
        return NULL;
    }

    symbol = &compiler->symbols[compiler->symbol_count++];
    strcpy(symbol->name, name);
    symbol->kind = kind;
    symbol->device_type = device_type;
    symbol->port = port;
    symbol->line = line;
    log_msg(LOG_LEVEL_DEBUG,
        "Semantic: registered %s '%s' (type=%s, port=%ld, line=%d)",
        symbol_kind_name(kind), name, device_type_name(symbol), port, line);
    return symbol;
}

static Symbol *resolve_read(Compiler *compiler, const Token *token)
{
    Symbol *symbol = find_symbol(compiler, token->lexema);

    log_msg(LOG_LEVEL_DEBUG,
        "Semantic: validating read access to '%s' at line %d",
        token->lexema, token->linha);
    if(!symbol)
    {
        parser_error(compiler, "semantico", token,
            "variavel '%s' utilizada antes de ser definida", token->lexema);
        return NULL;
    }

    if(symbol->kind == SYMBOL_ACTUATOR)
    {
        parser_error(compiler, "semantico", token,
            "atuador '%s' nao pode ser lido", token->lexema);
        return NULL;
    }

    log_msg(LOG_LEVEL_DEBUG,
        "Semantic: read access to '%s' allowed (%s)", token->lexema,
        symbol_kind_name(symbol->kind));
    return symbol;
}

static int parse_expression(Compiler *compiler, StringBuilder *output);
static int parse_block(Compiler *compiler, int stop_at_else, TokenType stop_token);
static int parse_command(Compiler *compiler);

static int parse_factor(Compiler *compiler, StringBuilder *output)
{
    Token token = *current_token(compiler);
    Symbol *symbol;
    char *end;
    long value;

    if(token.tipo == TK_NUMERO)
    {
        errno = 0;
        value = strtol(token.lexema, &end, 10);
        if(errno == ERANGE || *end != '\0' || value > INT_MAX)
            return parser_error(compiler, "semantico", &token,
                "numero inteiro fora do intervalo permitido");

        if(!builder_append(output, token.lexema))
            return parser_error(compiler, "interno", &token,
                "memoria insuficiente ao gerar expressao");
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: numeric factor '%s' accepted at line %d",
            token.lexema, token.linha);
        advance(compiler);
        return 1;
    }

    if(token.tipo == TK_IDENTIFICADOR)
    {
        symbol = resolve_read(compiler, &token);
        if(!symbol)
        {
            compiler->failed = 1;
            return 0;
        }

        if(!builder_appendf(output, "domus_%s", symbol->name))
            return parser_error(compiler, "interno", &token,
                "memoria insuficiente ao gerar expressao");
        log_msg(LOG_LEVEL_DEBUG,
            "Codegen: identifier factor '%s' translated to 'domus_%s'",
            token.lexema, symbol->name);
        advance(compiler);
        return 1;
    }

    if(token.tipo == TK_ABRE_PAR)
    {
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: opening parenthesized arithmetic expression at line %d",
            token.linha);
        if(!builder_append(output, "("))
            return parser_error(compiler, "interno", &token,
                "memoria insuficiente ao gerar expressao");
        advance(compiler);
        if(!parse_expression(compiler, output))
            return 0;
        if(!expect(compiler, TK_FECHA_PAR, "')'"))
            return 0;
        if(!builder_append(output, ")"))
            return parser_error(compiler, "interno", &token,
                "memoria insuficiente ao gerar expressao");
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: closed parenthesized arithmetic expression at line %d",
            token.linha);
        return 1;
    }

    return parser_error(compiler, "sintatico", &token,
        "esperada expressao, encontrado %s ('%s')",
        tokenToString(token.tipo), token.lexema);
}

static int parse_term(Compiler *compiler, StringBuilder *output)
{
    TokenType operation;
    StringBuilder term = {0};
    size_t start_length = output->length;

    if(!parse_factor(compiler, &term))
    {
        free(term.data);
        return 0;
    }

    while(current_token(compiler)->tipo == TK_MULT ||
        current_token(compiler)->tipo == TK_DIV)
    {
        StringBuilder right = {0};
        StringBuilder combined = {0};

        operation = current_token(compiler)->tipo;
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: applying %s operator at line %d",
            operation == TK_MULT ? "multiplication" : "division",
            current_token(compiler)->linha);
        advance(compiler);
        if(!parse_factor(compiler, &right))
        {
            free(term.data);
            free(right.data);
            return 0;
        }

        if(operation == TK_DIV)
        {
            compiler->uses_division = 1;
            if(!builder_appendf(&combined, "compiler_div_helper(%s, %s)",
                term.data ? term.data : "", right.data ? right.data : ""))
            {
                free(term.data);
                free(right.data);
                free(combined.data);
                return parser_error(compiler, "interno",
                    current_token(compiler),
                    "memoria insuficiente ao gerar termo");
            }
        }
        else
        {
            compiler->uses_multiplication = 1;
            if(!builder_appendf(&combined,
                "compiler_multiply_helper(%s, %s)",
                term.data ? term.data : "", right.data ? right.data : ""))
            {
                free(term.data);
                free(right.data);
                free(combined.data);
                return parser_error(compiler, "interno",
                    current_token(compiler),
                    "memoria insuficiente ao gerar termo");
            }
        }

        free(term.data);
        free(right.data);
        term = combined;
    }

    if(!builder_append(output, term.data ? term.data : ""))
    {
        free(term.data);
        return parser_error(compiler, "interno", current_token(compiler),
            "memoria insuficiente ao gerar termo");
    }
    free(term.data);
    log_msg(LOG_LEVEL_DEBUG, "Codegen: term translated to '%s'",
        output->data ? output->data + start_length : "");
    return 1;
}

static int parse_expression(Compiler *compiler, StringBuilder *output)
{
    TokenType operation;
    StringBuilder expression = {0};
    size_t start_length = output->length;

    if(!parse_term(compiler, &expression))
    {
        free(expression.data);
        return 0;
    }

    while(current_token(compiler)->tipo == TK_SOMA ||
        current_token(compiler)->tipo == TK_SUB)
    {
        StringBuilder right = {0};
        StringBuilder combined = {0};

        operation = current_token(compiler)->tipo;
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: applying %s operator at line %d",
            operation == TK_SOMA ? "addition" : "subtraction",
            current_token(compiler)->linha);
        advance(compiler);
        if(!parse_term(compiler, &right))
        {
            free(expression.data);
            free(right.data);
            return 0;
        }

        if(operation == TK_SOMA)
        {
            compiler->uses_addition = 1;
            if(!builder_appendf(&combined,
                "compiler_add_helper(%s, %s)",
                expression.data ? expression.data : "",
                right.data ? right.data : ""))
            {
                free(expression.data);
                free(right.data);
                free(combined.data);
                return parser_error(compiler, "interno",
                    current_token(compiler),
                    "memoria insuficiente ao gerar expressao");
            }
        }
        else
        {
            compiler->uses_subtraction = 1;
            if(!builder_appendf(&combined,
                "compiler_subtract_helper(%s, %s)",
                expression.data ? expression.data : "",
                right.data ? right.data : ""))
            {
                free(expression.data);
                free(right.data);
                free(combined.data);
                return parser_error(compiler, "interno",
                    current_token(compiler),
                    "memoria insuficiente ao gerar expressao");
            }
        }

        free(expression.data);
        free(right.data);
        expression = combined;
    }

    if(!builder_append(output, expression.data ? expression.data : ""))
    {
        free(expression.data);
        return parser_error(compiler, "interno", current_token(compiler),
            "memoria insuficiente ao gerar expressao");
    }
    log_msg(LOG_LEVEL_DEBUG, "Codegen: arithmetic expression translated to '%s'",
        output->data ? output->data + start_length : "");
    free(expression.data);
    return 1;
}

static long long evaluate_constant_expression(ConstantEvaluator *evaluator);

static long long evaluate_constant_factor(ConstantEvaluator *evaluator)
{
    const Token *token;
    char *end;
    long long value;

    if(evaluator->position >= evaluator->end)
    {
        evaluator->valid = 0;
        return 0;
    }

    token = &evaluator->compiler->stream.items[evaluator->position++];
    if(token->tipo == TK_NUMERO)
    {
        errno = 0;
        value = strtoll(token->lexema, &end, 10);
        if(errno == ERANGE || *end != '\0')
        {
            evaluator->valid = 0;
            return 0;
        }
        return value;
    }

    if(token->tipo == TK_IDENTIFICADOR)
    {
        evaluator->constant = 0;
        return 0;
    }

    if(token->tipo == TK_ABRE_PAR)
    {
        value = evaluate_constant_expression(evaluator);
        if(evaluator->position >= evaluator->end ||
            evaluator->compiler->stream.items[evaluator->position].tipo != TK_FECHA_PAR)
        {
            evaluator->valid = 0;
            return 0;
        }
        evaluator->position++;
        return value;
    }

    evaluator->valid = 0;
    return 0;
}

static long long evaluate_constant_term(ConstantEvaluator *evaluator)
{
    long long value = evaluate_constant_factor(evaluator);

    while(evaluator->valid && evaluator->position < evaluator->end)
    {
        TokenType operation = evaluator->compiler->stream.items[evaluator->position].tipo;
        long long right;

        if(operation != TK_MULT && operation != TK_DIV)
            break;
        evaluator->position++;
        right = evaluate_constant_factor(evaluator);
        if(!evaluator->valid || !evaluator->constant)
            continue;

        if(value < INT_MIN || value > INT_MAX ||
            right < INT_MIN || right > INT_MAX)
        {
            evaluator->constant = 0;
            continue;
        }

        if(operation == TK_DIV && right == 0)
        {
            evaluator->valid = 0;
            continue;
        }

        value = operation == TK_MULT ? value * right : value / right;
        if(value < INT_MIN || value > INT_MAX)
            evaluator->constant = 0;
    }

    return value;
}

static long long evaluate_constant_expression(ConstantEvaluator *evaluator)
{
    long long value = evaluate_constant_term(evaluator);

    while(evaluator->valid && evaluator->position < evaluator->end)
    {
        TokenType operation = evaluator->compiler->stream.items[evaluator->position].tipo;
        long long right;

        if(operation != TK_SOMA && operation != TK_SUB)
            break;
        evaluator->position++;
        right = evaluate_constant_term(evaluator);
        if(!evaluator->valid || !evaluator->constant)
            continue;

        if(value < INT_MIN || value > INT_MAX ||
            right < INT_MIN || right > INT_MAX)
        {
            evaluator->constant = 0;
            continue;
        }

        value = operation == TK_SOMA ? value + right : value - right;
        if(value < INT_MIN || value > INT_MAX)
            evaluator->constant = 0;
    }

    return value;
}

static int evaluate_constant(const Compiler *compiler, size_t start,
    size_t end, long long *value)
{
    ConstantEvaluator evaluator;

    evaluator.compiler = compiler;
    evaluator.position = start;
    evaluator.end = end;
    evaluator.constant = 1;
    evaluator.valid = 1;
    *value = evaluate_constant_expression(&evaluator);

    return evaluator.valid && evaluator.constant && evaluator.position == end;
}

static int is_relational(TokenType type)
{
    return type == TK_MENOR || type == TK_MENOR_IGUAL ||
        type == TK_MAIOR || type == TK_MAIOR_IGUAL ||
        type == TK_IGUAL || type == TK_DIFERENTE;
}

static int group_contains_relation(Compiler *compiler)
{
    size_t i;
    int depth = 0;

    if(current_token(compiler)->tipo != TK_ABRE_PAR)
        return 0;

    for(i = compiler->position; i < compiler->stream.count; i++)
    {
        TokenType type = compiler->stream.items[i].tipo;
        if(type == TK_ABRE_PAR)
            depth++;
        else if(type == TK_FECHA_PAR)
        {
            depth--;
            if(depth == 0)
                return 0;
        }
        else if(depth > 0 && is_relational(type))
            return 1;
    }

    return 0;
}

static int parse_condition(Compiler *compiler, StringBuilder *output)
{
    TokenType operation;
    int relation_found = 0;
    size_t start_length = output->length;

    if(current_token(compiler)->tipo == TK_ABRE_PAR &&
        group_contains_relation(compiler))
    {
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: entering parenthesized relational condition at line %d",
            current_token(compiler)->linha);
        builder_append(output, "(");
        advance(compiler);
        if(!parse_condition(compiler, output))
            return 0;
        if(!expect(compiler, TK_FECHA_PAR, "')'"))
            return 0;
        builder_append(output, ")");
        relation_found = 1;
    }
    else if(!parse_expression(compiler, output))
        return 0;

    if(is_relational(current_token(compiler)->tipo))
    {
        operation = current_token(compiler)->tipo;
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: applying relational operator %s at line %d",
            current_token(compiler)->lexema, current_token(compiler)->linha);
        if(operation == TK_MENOR) builder_append(output, " < ");
        else if(operation == TK_MENOR_IGUAL) builder_append(output, " <= ");
        else if(operation == TK_MAIOR) builder_append(output, " > ");
        else if(operation == TK_MAIOR_IGUAL) builder_append(output, " >= ");
        else if(operation == TK_IGUAL) builder_append(output, " == ");
        else builder_append(output, " != ");

        advance(compiler);
        if(!parse_expression(compiler, output))
            return 0;
        relation_found = 1;
    }

    log_msg(LOG_LEVEL_DEBUG, "Codegen: relational condition translated to '%s'%s",
        output->data ? output->data + start_length : "",
        relation_found ? "" : " (no relational operator found)");
    if(output->failed)
        return parser_error(compiler, "interno", current_token(compiler),
            "memoria insuficiente ao gerar condicao");
    return relation_found;
}

static void emit_indent(Compiler *compiler)
{
    int i;
    for(i = 0; i < compiler->indent; i++)
        builder_append(&compiler->body, "    ");
}

static void emit_digital_check(Compiler *compiler, const Symbol *symbol, int line)
{
    log_msg(LOG_LEVEL_DEBUG,
        "Codegen: emitting runtime 0/1 guard for digital actuator '%s' (line %d)",
        symbol->name, line);
    emit_indent(compiler);
    builder_appendf(&compiler->body,
        "if (domus_%s != 0 && domus_%s != 1) {\n",
        symbol->name, symbol->name);
    compiler->indent++;
    emit_indent(compiler);
    builder_appendf(&compiler->body,
        "fprintf(stderr, \"Valor invalido para o atuador digital %s na linha %d.\\n\");\n",
        symbol->name, line);
    emit_indent(compiler);
    builder_append(&compiler->body, "return 1;\n");
    compiler->indent--;
    emit_indent(compiler);
    builder_append(&compiler->body, "}\n");
}

static int parse_device(Compiler *compiler)
{
    Token keyword = *current_token(compiler);
    SymbolKind kind = keyword.tipo == TK_SENSOR ? SYMBOL_SENSOR : SYMBOL_ACTUATOR;

    log_msg(LOG_LEVEL_DEBUG, "Parser: reading %s declaration at line %d",
        kind == SYMBOL_SENSOR ? "sensor" : "actuator", keyword.linha);
    advance(compiler);

    for(;;)
    {
        Token name = *current_token(compiler);
        Token port_token;
        DeviceType device_type;
        char *end;
        long port;
        Symbol *symbol;

        if(!expect(compiler, TK_IDENTIFICADOR, "identificador do dispositivo"))
            return 0;
        if(!expect(compiler, TK_DOIS_PONTOS, "':'"))
            return 0;

        if(current_token(compiler)->tipo == TK_ANALOGICO)
            device_type = DEVICE_INTEGER;
        else if(current_token(compiler)->tipo == TK_DIGITAL)
            device_type = DEVICE_DIGITAL;
        else
            return parser_error(compiler, "sintatico", current_token(compiler),
                "esperado tipo 'analogico' ou 'digital'");
        advance(compiler);

        if(!expect(compiler, TK_ABRE_PAR, "'('"))
            return 0;
        if(!expect(compiler, TK_PORTA, "'porta'"))
            return 0;

        port_token = *current_token(compiler);
        if(port_token.tipo != TK_NUMERO)
            return parser_error(compiler, "sintatico", &port_token,
                "esperado numero da porta");

        errno = 0;
        port = strtol(port_token.lexema, &end, 10);
        if(errno == ERANGE || *end != '\0' || port > INT_MAX)
            return parser_error(compiler, "semantico", &port_token,
                "numero da porta fora do intervalo permitido");
        advance(compiler);

        if(!expect(compiler, TK_FECHA_PAR, "')'"))
            return 0;

        symbol = add_symbol(compiler, name.lexema, kind, device_type,
            port, name.linha);
        if(!symbol)
            return 0;

        log_msg(LOG_LEVEL_DEBUG,
            "Parser: device declaration accepted: %s '%s', type=%s, port=%ld",
            kind == SYMBOL_SENSOR ? "sensor" : "actuator", name.lexema,
            device_type == DEVICE_DIGITAL ? "digital" : "analog", port);

        if(current_token(compiler)->tipo != TK_VIRGULA)
            break;
        advance(compiler);
    }

    return expect(compiler, TK_PONTO, "'.' ao final da declaracao");
}

static int parse_assignment(Compiler *compiler)
{
    Token name = *current_token(compiler);
    Symbol *symbol = find_symbol(compiler, name.lexema);
    StringBuilder expression = {0};
    size_t expression_start;
    long long constant_value;
    int constant_known;

    log_msg(LOG_LEVEL_DEBUG,
        "Parser: assignment command to '%s' at line %d",
        name.lexema, name.linha);

    if(!symbol)
        symbol = add_symbol(compiler, name.lexema, SYMBOL_VARIABLE,
            DEVICE_INTEGER, 0, name.linha);
    else if(symbol->kind == SYMBOL_SENSOR)
        return parser_error(compiler, "semantico", &name,
            "sensor '%s' nao pode receber atribuicao", name.lexema);
    if(!symbol)
        return 0;

    advance(compiler);
    if(!expect(compiler, TK_ATRIB, "'<-'"))
        return 0;
    expression_start = compiler->position;
    if(!parse_expression(compiler, &expression))
    {
        free(expression.data);
        return 0;
    }

    log_msg(LOG_LEVEL_DEBUG,
        "Semantic: assignment target '%s' is %s; translated expression='%s'",
        symbol->name, symbol_kind_name(symbol->kind),
        expression.data ? expression.data : "");

    if(symbol->kind == SYMBOL_ACTUATOR &&
        symbol->device_type == DEVICE_DIGITAL)
    {
        constant_known = evaluate_constant(compiler, expression_start,
            compiler->position, &constant_value);
        if(constant_known)
        {
            log_msg(LOG_LEVEL_DEBUG,
                "Semantic: constant expression for digital actuator '%s' evaluates to %lld",
                symbol->name, constant_value);
            if(constant_value != 0 && constant_value != 1)
            {
                free(expression.data);
                return parser_error(compiler, "semantico", &name,
                    "atuador digital '%s' aceita apenas os valores 0 e 1",
                    symbol->name);
            }
        }
        else
            log_msg(LOG_LEVEL_DEBUG,
                "Semantic: expression for digital actuator '%s' is not a valid compile-time constant; runtime guard will enforce 0/1",
                symbol->name);
    }

    log_msg(LOG_LEVEL_DEBUG,
        "Codegen: emitting assignment 'domus_%s = %s'",
        symbol->name, expression.data ? expression.data : "");
    emit_indent(compiler);
    builder_appendf(&compiler->body, "domus_%s = %s;\n",
        symbol->name, expression.data ? expression.data : "");
    free(expression.data);

    if(symbol->kind == SYMBOL_ACTUATOR &&
        symbol->device_type == DEVICE_DIGITAL)
        emit_digital_check(compiler, symbol, name.linha);

    return 1;
}

static int parse_input(Compiler *compiler)
{
    Token keyword = *current_token(compiler);
    Token name;
    Symbol *symbol;

    log_msg(LOG_LEVEL_DEBUG, "Parser: input command 'ler' at line %d",
        keyword.linha);
    advance(compiler);
    if(!expect(compiler, TK_ABRE_PAR, "'('"))
        return 0;

    name = *current_token(compiler);
    if(!expect(compiler, TK_IDENTIFICADOR, "identificador"))
        return 0;
    if(!expect(compiler, TK_FECHA_PAR, "')'"))
        return 0;

    symbol = find_symbol(compiler, name.lexema);
    if(!symbol)
        symbol = add_symbol(compiler, name.lexema, SYMBOL_VARIABLE,
            DEVICE_INTEGER, 0, name.linha);
    else if(symbol->kind == SYMBOL_SENSOR)
        return parser_error(compiler, "semantico", &name,
            "sensor '%s' nao pode ser alterado pelo comando ler", name.lexema);
    if(!symbol)
        return 0;

    compiler->uses_input = 1;
    log_msg(LOG_LEVEL_DEBUG,
        "Semantic: input target '%s' accepted as %s",
        symbol->name, symbol_kind_name(symbol->kind));
    log_msg(LOG_LEVEL_DEBUG,
        "Codegen: emitting checked integer input for 'domus_%s'", symbol->name);
    emit_indent(compiler);
    builder_appendf(&compiler->body,
        "if (!compiler_read_int(&domus_%s)) return 1;\n",
        symbol->name);
    if(symbol->kind == SYMBOL_ACTUATOR &&
        symbol->device_type == DEVICE_DIGITAL)
        emit_digital_check(compiler, symbol, keyword.linha);

    return 1;
}

static int parse_output(Compiler *compiler)
{
    StringBuilder expression = {0};

    log_msg(LOG_LEVEL_DEBUG, "Parser: output command 'escrever' at line %d",
        current_token(compiler)->linha);
    advance(compiler);
    if(!expect(compiler, TK_ABRE_PAR, "'('"))
        return 0;
    if(!parse_expression(compiler, &expression))
    {
        free(expression.data);
        return 0;
    }
    if(!expect(compiler, TK_FECHA_PAR, "')'"))
    {
        free(expression.data);
        return 0;
    }

    log_msg(LOG_LEVEL_DEBUG,
        "Codegen: emitting printf for expression '%s'",
        expression.data ? expression.data : "");
    emit_indent(compiler);
    builder_appendf(&compiler->body, "printf(\"%%d\\n\", (%s));\n",
        expression.data ? expression.data : "");
    free(expression.data);
    return 1;
}

static int parse_if(Compiler *compiler)
{
    StringBuilder condition = {0};
    int has_else;

    log_msg(LOG_LEVEL_DEBUG, "Parser: entering 'se' command at line %d",
        current_token(compiler)->linha);
    advance(compiler);
    if(!parse_condition(compiler, &condition))
    {
        free(condition.data);
        if(!compiler->failed)
            parser_error(compiler, "sintatico", current_token(compiler),
                "a condicao de 'se' deve conter um operador relacional");
        return 0;
    }
    if(!expect(compiler, TK_ENTAO, "'entao'"))
    {
        free(condition.data);
        return 0;
    }

    emit_indent(compiler);
    log_msg(LOG_LEVEL_DEBUG, "Codegen: emitting if condition '%s'",
        condition.data ? condition.data : "");
    builder_appendf(&compiler->body, "if (%s) {\n",
        condition.data ? condition.data : "");
    free(condition.data);
    compiler->indent++;
    if(!parse_block(compiler, 1, TK_FIM_SE))
        return 0;
    compiler->indent--;
    emit_indent(compiler);
    builder_append(&compiler->body, "}");

    has_else = current_token(compiler)->tipo == TK_SENAO;
    log_msg(LOG_LEVEL_DEBUG, "Parser: 'se' command has %s branch",
        has_else ? "an 'senao'" : "no 'senao'");
    if(has_else)
    {
        advance(compiler);
        builder_append(&compiler->body, " else {\n");
        compiler->indent++;
        if(!parse_block(compiler, 0, TK_FIM_SE))
            return 0;
        compiler->indent--;
        emit_indent(compiler);
        builder_append(&compiler->body, "}");
    }
    builder_append(&compiler->body, "\n");

    return expect(compiler, TK_FIM_SE, "'fim_se'");
}

static int parse_while(Compiler *compiler)
{
    StringBuilder condition = {0};

    log_msg(LOG_LEVEL_DEBUG, "Parser: entering 'enquanto' command at line %d",
        current_token(compiler)->linha);
    advance(compiler);
    if(!expect(compiler, TK_ABRE_PAR, "'('"))
        return 0;
    if(!parse_condition(compiler, &condition))
    {
        free(condition.data);
        if(!compiler->failed)
            parser_error(compiler, "sintatico", current_token(compiler),
                "a condicao de 'enquanto' deve conter um operador relacional");
        return 0;
    }
    if(!expect(compiler, TK_FECHA_PAR, "')'"))
    {
        free(condition.data);
        return 0;
    }

    emit_indent(compiler);
    log_msg(LOG_LEVEL_DEBUG, "Codegen: emitting while condition '%s'",
        condition.data ? condition.data : "");
    builder_appendf(&compiler->body, "while (%s) {\n",
        condition.data ? condition.data : "");
    free(condition.data);
    compiler->indent++;
    if(!parse_block(compiler, 0, TK_FIM_ENQUANTO))
        return 0;
    compiler->indent--;
    emit_indent(compiler);
    builder_append(&compiler->body, "}\n");

    return expect(compiler, TK_FIM_ENQUANTO, "'fim_enquanto'");
}

static int parse_command(Compiler *compiler)
{
    Token *token = current_token(compiler);

    log_msg(LOG_LEVEL_DEBUG,
        "Parser: dispatching command at line %d: %s ('%s')",
        token->linha, tokenToString(token->tipo), token->lexema);

    switch(current_token(compiler)->tipo)
    {
        case TK_IDENTIFICADOR:
            return parse_assignment(compiler);
        case TK_LER:
            return parse_input(compiler);
        case TK_ESCREVER:
            return parse_output(compiler);
        case TK_SE:
            return parse_if(compiler);
        case TK_ENQUANTO:
            return parse_while(compiler);
        default:
            return parser_error(compiler, "sintatico", current_token(compiler),
                "esperado comando, encontrado %s ('%s')",
                tokenToString(current_token(compiler)->tipo),
                current_token(compiler)->lexema);
    }
}

static int is_block_end(Compiler *compiler, int stop_at_else, TokenType stop_token)
{
    TokenType type = current_token(compiler)->tipo;
    int at_end = type == TK_EOF || type == stop_token ||
        (stop_at_else && type == TK_SENAO);

    if(at_end)
        log_msg(LOG_LEVEL_DEBUG,
            "Parser: block ends at line %d on %s ('%s')",
            current_token(compiler)->linha, tokenToString(type),
            current_token(compiler)->lexema);
    return at_end;
}

static int parse_block(Compiler *compiler, int stop_at_else, TokenType stop_token)
{
    size_t command_count = 0;

    log_msg(LOG_LEVEL_DEBUG,
        "Parser: entering command block at line %d (stop token=%s%s)",
        current_token(compiler)->linha, tokenToString(stop_token),
        stop_at_else ? ", also stops at SENAO" : "");
    while(!is_block_end(compiler, stop_at_else, stop_token))
    {
        if(!parse_command(compiler))
            return 0;
        if(!expect(compiler, TK_PONTO, "'.' ao final do comando"))
            return 0;
        command_count++;
    }

    log_msg(LOG_LEVEL_DEBUG,
        "Parser: command block completed with %zu command(s)", command_count);
    if(command_count == 0)
        return parser_error(compiler, "sintatico", current_token(compiler),
            "o bloco deve conter ao menos um comando");
    return 1;
}

static int parse_program(Compiler *compiler)
{
    log_msg(LOG_LEVEL_DEBUG,
        "Parser: starting program at line %d with %zu token(s)",
        current_token(compiler)->linha, compiler->stream.count);
    while(current_token(compiler)->tipo == TK_SENSOR ||
        current_token(compiler)->tipo == TK_ATUADOR)
    {
        if(!parse_device(compiler))
            return 0;
    }

    if(current_token(compiler)->tipo == TK_EOF)
        return parser_error(compiler, "sintatico", current_token(compiler),
            "programa deve conter ao menos um comando");

    if(!parse_block(compiler, 0, TK_EOF))
        return 0;

    if(current_token(compiler)->tipo != TK_EOF)
        return parser_error(compiler, "sintatico", current_token(compiler),
            "conteudo inesperado apos o bloco principal");
    log_msg(LOG_LEVEL_DEBUG, "Parser: program accepted successfully");
    return 1;
}

static const char *symbol_kind_name(SymbolKind kind)
{
    if(kind == SYMBOL_SENSOR) return "sensor";
    if(kind == SYMBOL_ACTUATOR) return "atuador";
    return "variavel";
}

static const char *device_type_name(const Symbol *symbol)
{
    if(symbol->kind == SYMBOL_VARIABLE) return "inteiro";
    return symbol->device_type == DEVICE_DIGITAL ? "digital" : "analogico";
}

static void print_symbol_table(const Compiler *compiler)
{
    size_t i;

    printf("\nTabela de simbolos\n");
    printf("%-24s %-10s %-12s %-8s %s\n",
        "Identificador", "Categoria", "Tipo", "Porta", "Linha");
    printf("---------------------------------------------------------------------\n");
    for(i = 0; i < compiler->symbol_count; i++)
    {
        const Symbol *symbol = &compiler->symbols[i];
        if(symbol->kind == SYMBOL_VARIABLE)
            printf("%-24s %-10s %-12s %-8s %d\n", symbol->name,
                symbol_kind_name(symbol->kind), device_type_name(symbol), "-",
                symbol->line);
        else
            printf("%-24s %-10s %-12s %-8ld %d\n", symbol->name,
                symbol_kind_name(symbol->kind), device_type_name(symbol),
                symbol->port, symbol->line);
    }
    printf("\n");

    for(i = 0; i < compiler->symbol_count; i++)
    {
        const Symbol *symbol = &compiler->symbols[i];
        log_msg(LOG_LEVEL_INFO,
            "Symbol: name=%s category=%s type=%s port=%ld line=%d",
            symbol->name, symbol_kind_name(symbol->kind),
            device_type_name(symbol), symbol->port, symbol->line);
    }
}

static int write_c_file(const char *path, const Compiler *compiler)
{
    FILE *output = fopen(path, "w");
    size_t i;
    int failed = 0;
    int uses_arithmetic = compiler->uses_addition ||
        compiler->uses_subtraction || compiler->uses_multiplication ||
        compiler->uses_division;

    if(!output)
    {
        report_error("arquivo", 0, "nao foi possivel criar '%s'", path);
        return 0;
    }

    log_msg(LOG_LEVEL_DEBUG,
        "Codegen: writing C translation unit to '%s' (%zu global symbol(s), %zu body byte(s))",
        path, compiler->symbol_count,
        compiler->body.data ? compiler->body.length : 0);
    fprintf(output, "#include <stdio.h>\n");
    if(uses_arithmetic || compiler->uses_input)
        fprintf(output, "#include <limits.h>\n#include <stdlib.h>\n");
    if(compiler->uses_input)
        fprintf(output,
            "#include <ctype.h>\n#include <errno.h>\n#include <string.h>\n");
    fputc('\n', output);

    if(compiler->uses_input)
    {
        fprintf(output,
            "static int compiler_read_int(int *value)\n"
            "{\n"
            "    char text[128];\n"
            "    char *end;\n"
            "    long parsed;\n"
            "    int next;\n"
            "\n"
            "    if (scanf(\"%%127s\", text) != 1)\n"
            "        goto invalid_input;\n"
            "    if (strlen(text) == sizeof(text) - 1) {\n"
            "        next = getchar();\n"
            "        if (next != EOF && !isspace((unsigned char)next)) {\n"
            "            do { next = getchar(); }\n"
            "            while (next != EOF && !isspace((unsigned char)next));\n"
            "            goto invalid_input;\n"
            "        }\n"
            "        if (next != EOF)\n"
            "            ungetc(next, stdin);\n"
            "    }\n"
            "\n"
            "    errno = 0;\n"
            "    parsed = strtol(text, &end, 10);\n"
            "    if (errno == ERANGE || end == text || *end != '\\0' ||\n"
            "        parsed < INT_MIN || parsed > INT_MAX)\n"
            "        goto invalid_input;\n"
            "\n"
            "    *value = (int)parsed;\n"
            "    return 1;\n"
            "\n"
            "invalid_input:\n"
            "    fputs(\"Erro em execucao: entrada nao e um inteiro valido no intervalo de int.\\n\", stderr);\n"
            "    return 0;\n"
            "}\n\n");
        log_msg(LOG_LEVEL_DEBUG, "Codegen: emitted checked integer input helper");
    }
    if(compiler->uses_addition)
    {
        fprintf(output,
            "static int compiler_add_helper(int left, int right)\n"
            "{\n"
            "    if ((right > 0 && left > INT_MAX - right) ||\n"
            "        (right < 0 && left < INT_MIN - right)) {\n"
            "        fputs(\"Erro em execucao: overflow na soma de inteiros.\\n\", stderr);\n"
            "        exit(EXIT_FAILURE);\n"
            "    }\n"
            "    return left + right;\n"
            "}\n\n");
        log_msg(LOG_LEVEL_DEBUG, "Codegen: emitted checked integer addition helper");
    }
    if(compiler->uses_subtraction)
    {
        fprintf(output,
            "static int compiler_subtract_helper(int left, int right)\n"
            "{\n"
            "    if ((right < 0 && left > INT_MAX + right) ||\n"
            "        (right > 0 && left < INT_MIN + right)) {\n"
            "        fputs(\"Erro em execucao: overflow na subtracao de inteiros.\\n\", stderr);\n"
            "        exit(EXIT_FAILURE);\n"
            "    }\n"
            "    return left - right;\n"
            "}\n\n");
        log_msg(LOG_LEVEL_DEBUG, "Codegen: emitted checked integer subtraction helper");
    }
    if(compiler->uses_multiplication)
    {
        fprintf(output,
            "static int compiler_multiply_helper(int left, int right)\n"
            "{\n"
            "    if ((left > 0 && ((right > 0 && left > INT_MAX / right) ||\n"
            "            (right < 0 && right < INT_MIN / left))) ||\n"
            "        (left < 0 && ((right > 0 && left < INT_MIN / right) ||\n"
            "            (right < 0 && left < INT_MAX / right)))) {\n"
            "        fputs(\"Erro em execucao: overflow na multiplicacao de inteiros.\\n\", stderr);\n"
            "        exit(EXIT_FAILURE);\n"
            "    }\n"
            "    return left * right;\n"
            "}\n\n");
        log_msg(LOG_LEVEL_DEBUG, "Codegen: emitted checked integer multiplication helper");
    }
    if(compiler->uses_division)
    {
        fprintf(output,
            "static int compiler_div_helper(int dividend, int divisor)\n"
            "{\n"
            "    if (divisor == 0) {\n"
            "        fputs(\"Erro em execucao: divisao por zero.\\n\", stderr);\n"
            "        exit(EXIT_FAILURE);\n"
            "    }\n"
            "    if (dividend == INT_MIN && divisor == -1) {\n"
            "        fputs(\"Erro em execucao: resultado da divisao fora do intervalo int.\\n\", stderr);\n"
            "        exit(EXIT_FAILURE);\n"
            "    }\n"
            "    return dividend / divisor;\n"
            "}\n\n");
        log_msg(LOG_LEVEL_DEBUG,
            "Codegen: emitted checked integer division helper");
    }
    for(i = 0; i < compiler->symbol_count; i++)
    {
        log_msg(LOG_LEVEL_DEBUG,
            "Codegen: emitting global declaration 'int domus_%s;'",
            compiler->symbols[i].name);
        fprintf(output, "int domus_%s;\n", compiler->symbols[i].name);
    }
    if(compiler->symbol_count > 0)
        fputc('\n', output);

    fprintf(output, "int main(void)\n{\n");
    if(compiler->body.data)
    {
        log_msg(LOG_LEVEL_DEBUG, "Codegen: emitting main body:\n%s",
            compiler->body.data);
        fputs(compiler->body.data, output);
    }
    fprintf(output, "    return 0;\n}\n");

    if(ferror(output))
        failed = 1;
    if(fclose(output) != 0)
        failed = 1;

    if(failed)
    {
        report_error("arquivo", 0, "falha ao gravar '%s'", path);
        return 0;
    }

    log_msg(LOG_LEVEL_INFO, "C source generated at %s", path);
    return 1;
}

int compiler_run(const char *input_path, const char *output_path)
{
    Compiler compiler = {0};
    FILE *source;
    int success;

    source = fopen(input_path, "r");
    if(!source)
    {
        report_error("arquivo", 0, "nao foi possivel abrir '%s'", input_path);
        return 1;
    }

    log_msg(LOG_LEVEL_INFO, "Compilation started: %s -> %s",
        input_path, output_path);
    lexer_reset();
    success = tokenize(source, &compiler.stream);
    fclose(source);

    if(success)
    {
        compiler.symbol_capacity = compiler.stream.count;
        compiler.symbols = calloc(compiler.symbol_capacity,
            sizeof(*compiler.symbols));
        if(!compiler.symbols)
        {
            report_error("interno", 0, "memoria insuficiente para a tabela de simbolos");
            success = 0;
        }
    }

    if(success)
        compiler.indent = 1;

    if(success)
        success = parse_program(&compiler);

    if(success && compiler.body.failed)
    {
        parser_error(&compiler, "interno", current_token(&compiler),
            "memoria insuficiente ao gerar codigo C");
        success = 0;
    }

    if(compiler.symbols)
        print_symbol_table(&compiler);

    if(success)
        success = write_c_file(output_path, &compiler);

    free(compiler.stream.items);
    free(compiler.symbols);
    free(compiler.body.data);

    if(success)
        log_msg(LOG_LEVEL_INFO, "Compilation completed successfully");
    else
        log_msg(LOG_LEVEL_ERROR, "Compilation failed");

    return success ? 0 : 1;
}