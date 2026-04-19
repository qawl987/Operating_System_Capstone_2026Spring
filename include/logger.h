/**
 * Logger System for Lab 3
 *
 * Log Levels:
 * - LOG_LEVEL_NONE  (0): No logs at all
 * - LOG_LEVEL_SPEC  (1): Only spec-required logs (for demo)
 * - LOG_LEVEL_INFO  (2): Spec + initialization info
 * - LOG_LEVEL_DEBUG (3): All logs including detailed debug info
 *
 * Usage:
 *   log_spec("message %d", value);   - Spec-required logs
 *   log_info("message %s", str);     - Informational logs
 *   log_debug("message 0x%x", addr); - Debug logs
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "helper.h"

/* Log levels */
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_SPEC 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_DEBUG 3

/* Default log level - can be overridden in config.h */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

/* Spec-required logs (for demo) */
#if LOG_LEVEL >= LOG_LEVEL_SPEC
#define log_spec(...) printf(__VA_ARGS__)
#else
#define log_spec(...) ((void)0)
#endif

/* Informational logs */
#if LOG_LEVEL >= LOG_LEVEL_INFO
#define log_info(...) printf(__VA_ARGS__)
#else
#define log_info(...) ((void)0)
#endif

/* Debug logs */
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define log_debug(...) printf(__VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif

#endif /* LOGGER_H */
