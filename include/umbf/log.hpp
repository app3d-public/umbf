#pragma once

#include <acul/log.hpp>

namespace umbf
{
    APPLIB_API void attach_logger(acul::log::log_service *log_service, acul::log::logger_base *logger) noexcept;
}