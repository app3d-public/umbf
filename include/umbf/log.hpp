#pragma once

#include <acul/log.hpp>
#include <umbf/symbol_export.h>

namespace umbf
{
    UMBF_EXPORT void attach_logger(acul::log::log_service *log_service, acul::log::logger_base *logger) noexcept;
}