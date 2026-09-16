#include "logger.h"
#include "cli/cli.h" // for gathering version and debug mode information

static Logger *logger = NULL;

char *getDate(void)
{
    static char date[20];

    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);

    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", timeinfo);

    return date;
}

void log_msg(LogLevel level, const char *fmt, ...)
{
    if(!cli_is_debug_mode_enabled() || logger->file == NULL)
        return;

    va_list args;
    va_start(args, fmt);
    fprintf(logger->file, "[%s][%s]: ", getDate(), log_level_to_string(level));
    vfprintf(logger->file, fmt, args);
    fprintf(logger->file, "\n");
    fflush(logger->file);
    va_end(args);
}

void logger_init(Logger *log)
{
    logger = log;
    logger->file = fopen(LOG_FILE, "w");
    logger->debug_mode = cli_is_debug_mode_enabled();
    logger->line = 0;
    logger->column = 0;

    if(!logger->file)
    {
        fprintf(stderr, "Error opening log file.\n");
        exit(1);
        return;
    }

    if(cli_is_debug_mode_enabled())
    {
        log_msg(LOG_LEVEL_INFO, "Tiny Compiler v%s started! Built at %s %s", COMPILER_VERSION, __DATE__, __TIME__);
        log_msg(LOG_LEVEL_INFO, "Debug mode IS activated.");
        log_msg(LOG_LEVEL_INFO, "Input file: %s", cli_get_input_file());
        log_msg(LOG_LEVEL_INFO, "Output file: %s", cli_get_output_file());
    }

    FILE *input = fopen(cli_get_input_file(), "r");
    if(!input)
    {
        fprintf(stderr, "Error opening input file: %s\n", cli_get_input_file());
        log_msg(LOG_LEVEL_ERROR, "Error opening input file: %s", cli_get_input_file());
        if(logger->file)
            fclose(logger->file);
        return;
    }
    FILE *output = fopen(cli_get_output_file(), "w");
    if(!output)
    {
        fprintf(stderr, "Error opening output file: %s\n", cli_get_output_file());
        log_msg(LOG_LEVEL_ERROR, "Error opening output file: %s", cli_get_output_file());
        fclose(input);
        if(logger->file)
            fclose(logger->file);
        return;
    }
}

void logger_close()
{
    if(logger && logger->file)
    {
        log_msg(LOG_LEVEL_INFO, "Tiny Compiler v%s finished!", COMPILER_VERSION);
        fclose(logger->file);
        logger->file = NULL;
    }
}

char *log_level_to_string(LogLevel level)
{
    switch(level)
    {
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_WARNING:
            return "WARNING";
        case LOG_LEVEL_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}