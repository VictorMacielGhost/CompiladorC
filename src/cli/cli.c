#include "cli.h"

static CLI cli;

void cli_init(int argc, char *argv[])
{
    cli.input_file = "../tests/new.domus";
    cli.output_file = "../tests/build.txt";

    cli_process_args(argc, argv);
    log_msg(LOG_LEVEL_INFO, "Build initiated at %s", getDate());
    log_msg(LOG_LEVEL_INFO, "Build parameters:");
    for(int i = 0; i < argc; i++)
    {
        log_msg(LOG_LEVEL_INFO, "arg %d: %s", i, argv[i]);
    }
}

void cli_print_help()
{
    printf("Usage: domusc [options]\n");
    printf("Options:\n");
    printf("  --in <file>     Input file\n");
    printf("  --out <file>    Output file\n");
    printf("  --debug         Enable debug mode\n");
    printf("  --help          Show this help message\n");
    printf("  --version       Show compiler version\n");
}

void cli_print_version()
{
    printf("Compiler Version: %s\n", COMPILER_VERSION);
}

void cli_process_args(int argc, char *argv[])
{
    for(int i = 1; i < argc; i++)
    {
        if(strcmp(argv[i], "--in") == 0 && i + 1 < argc)
        {
            cli_set_input_file(argv[++i]);
        }
        else if(strcmp(argv[i], "--out") == 0 && i + 1 < argc)
        {
            cli_set_output_file(argv[++i]);
        }
        else if(strcmp(argv[i], "--debug") == 0)
        {
            cli_enable_debug_mode();
        }
        else if(strcmp(argv[i], "--help") == 0)
        {
            cli_print_help();
            exit(0);
        }
        else if(strcmp(argv[i], "--version") == 0)
        {
            cli_print_version();
            exit(0);
        }
    }
}

void cli_set_input_file(const char *filename)
{
    cli.input_file = filename;
}

void cli_set_output_file(const char *filename)
{
    cli.output_file = filename;
}

void cli_enable_debug_mode()
{
    cli.debug_mode = true;
}

void cli_disable_debug_mode()
{
    cli.debug_mode = false;
}

const char *cli_get_input_file()
{
    return cli.input_file;
}

const char *cli_get_output_file()
{
    return cli.output_file;
}

bool cli_is_debug_mode_enabled()
{
    return cli.debug_mode;
}