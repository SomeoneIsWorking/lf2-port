#include "lf2_log.h"

#include <lucent/log.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>

namespace {

void emit_message(std::string_view channel, std::string_view text, lucent::Level level)
{
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        if (end != start) lucent::log(level, channel, text.substr(start, end - start));
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
}

int format_text(const char *format, va_list arguments, std::string &text)
{
    va_list measure;
    va_copy(measure, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (length < 0) return length;

    text.resize(static_cast<std::size_t>(length) + 1);
    va_list render;
    va_copy(render, arguments);
    const int rendered = std::vsnprintf(text.data(), text.size(), format, render);
    va_end(render);
    if (rendered < 0) return rendered;
    text.resize(static_cast<std::size_t>(rendered));
    return rendered;
}

} // namespace

extern "C" void lf2_log_perror(const char *channel, const char *message)
{
    const int error = errno;
    const std::string detail = std::error_code(error, std::generic_category()).message();
    std::string text;
    if (message && *message) {
        text.append(message);
        text.append(": ");
    }
    text.append(detail);
    emit_message(channel && *channel ? channel : "lf2", text, lucent::Level::Error);
}

extern "C" void lf2_log_write(Lf2LogLevel level, const char *channel, const char *message)
{
    lucent::Level lucent_level = lucent::Level::Info;
    if (level == LF2_LOG_WARNING) lucent_level = lucent::Level::Warn;
    if (level == LF2_LOG_ERROR) lucent_level = lucent::Level::Error;
    emit_message(channel && *channel ? channel : "lf2", message ? message : "", lucent_level);
}

extern "C" void lf2_log_writef(Lf2LogLevel level, const char *channel, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::string text;
    const int rendered = format_text(format, arguments, text);
    va_end(arguments);
    if (rendered >= 0) lf2_log_write(level, channel, text.c_str());
}
