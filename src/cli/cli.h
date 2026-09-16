#ifndef CLI_H
#define CLI_H
#define COMPILER_VERSION "0.1.0"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include "../logger/logger.h"

typedef struct
{
    const char *input_file;
    const char *output_file;
    bool debug_mode;
} CLI;

void cli_init(int argc, char *argv[]);
void cli_print_help();
void cli_print_version();
void cli_process_args(int argc, char *argv[]);
void cli_set_input_file(const char *filename);
void cli_set_output_file(const char *filename);
void cli_enable_debug_mode();
void cli_disable_debug_mode();
const char *cli_get_input_file();
const char *cli_get_output_file();
bool cli_is_debug_mode_enabled();

#endif