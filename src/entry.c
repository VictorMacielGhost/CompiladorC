/*
 * 
 *   entry.c is a file created by Victor Maciel
 *   created at Thu Sep 03 2026 08:08:07
*/

#include "cli/cli.h"
#include "lexer/lexer.h"
#include "logger/logger.h"

int main(int argc, char *argv[])
{
    Logger logger;

    cli_init(argc, argv);
    // cli_enable_debug_mode();

    logger_init(&logger);

    lexer_init();

    logger_close();
    return 0;
}