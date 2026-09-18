#ifndef LOGGER_H
#define LOGGER_H
#define LOG_FILE "compile.log"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>

typedef enum 
{
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
} LogLevel;

typedef struct
{
    FILE *file;
    bool debug_mode;
    int line;
    int column;
} Logger;

char *log_level_to_string(LogLevel level);
void log_msg(LogLevel level, const char *fmt, ...);
char *getDate();
void logger_init(Logger *log);
void logger_close();

#endif