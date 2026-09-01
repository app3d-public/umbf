#pragma once

#include <acul/log.hpp>

extern struct LogContext
{
    acul::log::log_service *log_service;
    acul::log::logger_base *logger;
} g_log;

#define UMBF_LOG_DEFAULT(level, ...) acul::log::write(g_log.log_service, g_log.logger, level, __VA_ARGS__)
#define UMBF_LOG_INFO(...)           UMBF_LOG_DEFAULT(acul::log::level::info, __VA_ARGS__)
#define UMBF_LOG_DEBUG(...)          UMBF_LOG_DEFAULT(acul::log::level::debug, __VA_ARGS__)
#define UMBF_LOG_TRACE(...)          UMBF_LOG_DEFAULT(acul::log::level::trace, __VA_ARGS__)
#define UMBF_LOG_WARN(...)           UMBF_LOG_DEFAULT(acul::log::level::warn, __VA_ARGS__)
#define UMBF_LOG_ERROR(...)          UMBF_LOG_DEFAULT(acul::log::level::error, __VA_ARGS__)
#define UMBF_LOG_FATAL(...)          UMBF_LOG_DEFAULT(acul::log::level::fatal, __VA_ARGS__)