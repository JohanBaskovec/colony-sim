#include "core/log.h"

#include <stdarg.h>
#include <stdio.h>

static LogLevel s_min_level = LOG_LEVEL_INFO;
static LogSink s_sink = NULL;
static void *s_sink_userdata = NULL;

void log_set_min_level(LogLevel level)
{
    s_min_level = level;
}

LogLevel log_min_level(void)
{
    return s_min_level;
}

void log_set_sink(LogSink sink, void *userdata)
{
    s_sink = sink;
    s_sink_userdata = userdata;
}

const char *log_level_name(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_INFO: return "INFO";
    case LOG_LEVEL_WARN: return "WARN";
    case LOG_LEVEL_ERROR: return "ERROR";
    }
    return "?";
}

void log_message(LogLevel level, const char *format, ...)
{
    if (level < s_min_level) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (s_sink != NULL) {
        s_sink(level, buffer, s_sink_userdata);
    } else {
        fprintf(stderr, "[%s] %s\n", log_level_name(level), buffer);
    }
}
