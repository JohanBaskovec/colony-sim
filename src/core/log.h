/*
 * Minimal logging facility.
 *
 * Log messages are developer-facing diagnostics: they are always written in
 * English and are never translated. Anything shown to the player must go
 * through the i18n module instead (see core/i18n.h).
 */
#ifndef COLONY_CORE_LOG_H
#define COLONY_CORE_LOG_H

#if defined(__GNUC__) || defined(__clang__)
#define COLONY_PRINTF_FORMAT(format_index, args_index) \
    __attribute__((format(printf, format_index, args_index)))
#else
#define COLONY_PRINTF_FORMAT(format_index, args_index)
#endif

typedef enum LogLevel {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
} LogLevel;

/* Receives every formatted message at or above the minimum level. */
typedef void (*LogSink)(LogLevel level, const char *message, void *userdata);

void log_set_min_level(LogLevel level);
LogLevel log_min_level(void);

/* Redirects log output. Passing NULL restores the default sink (stderr). */
void log_set_sink(LogSink sink, void *userdata);

void log_message(LogLevel level, const char *format, ...) COLONY_PRINTF_FORMAT(2, 3);

const char *log_level_name(LogLevel level);

#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...) log_message(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif /* COLONY_CORE_LOG_H */
