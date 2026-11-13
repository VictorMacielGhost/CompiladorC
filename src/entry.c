/*
 * 
 *   entry.c is a file created by Victor Maciel
 *   created at Thu Sep 03 2026 08:08:07
*/

#include "cli/cli.h"
#include "compiler/compiler.h"
#include "logger/logger.h"

int main(int argc, char *argv[])
{
    Logger logger;
    int result;
    int i;

    cli_init(argc, argv);

    logger_init(&logger);
    cli_enable_debug_mode();
    log_msg(LOG_LEVEL_INFO, "Build initiated at %s", getDate());
    log_msg(LOG_LEVEL_INFO, "Command-line arguments:");
    for(i = 0; i < argc; i++)
        log_msg(LOG_LEVEL_INFO, "arg %d: %s", i, argv[i]);

    result = compiler_run(cli_get_input_file(), cli_get_output_file());

    logger_close();
    return result;
}