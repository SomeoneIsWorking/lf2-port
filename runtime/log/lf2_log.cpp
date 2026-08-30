#include "lf2_log.h"

#undef fprintf
#undef printf
#undef fputs
#undef perror

#include <lucent/log.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct PendingLine {
    std::string channel;
    std::string text;
};

struct ThreadLines {
    PendingLine standard_error;
    PendingLine standard_output;
};

struct State {
    std::mutex mutex;
    std::unordered_map<std::thread::id, ThreadLines> threads;
};

struct CompleteLine {
    std::string channel;
    std::string text;
};

State &state()
{
    static State *value = new State();
    return *value;
}

std::string channel_from_source(const char *source)
{
    std::string_view path = source ? source : "lf2";
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
    const std::size_t dot = path.find_last_of('.');
    if (dot != std::string_view::npos) path = path.substr(0, dot);
    return path.empty() ? "lf2" : std::string(path);
}

PendingLine &pending_for(ThreadLines &lines, FILE *stream)
{
    return stream == stdout ? lines.standard_output : lines.standard_error;
}

void emit(std::vector<CompleteLine> lines, lucent::Level level = lucent::Level::Info)
{
    for (const CompleteLine &line : lines) lucent::log(level, line.channel, line.text);
}

void emit_message(std::string_view channel, std::string_view text, lucent::Level level)
{
    std::vector<CompleteLine> complete;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        if (end != start) complete.push_back({std::string(channel), std::string(text.substr(start, end - start))});
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    emit(std::move(complete), level);
}

void append_log(FILE *stream, const char *source, std::string_view text)
{
    std::vector<CompleteLine> complete;
    {
        std::lock_guard lock(state().mutex);
        PendingLine &pending = pending_for(state().threads[std::this_thread::get_id()], stream);
        std::string channel = channel_from_source(source);
        if (!pending.text.empty() && pending.channel != channel) {
            complete.push_back({std::move(pending.channel), std::move(pending.text)});
            pending = {};
        }
        pending.channel = std::move(channel);

        std::size_t start = 0;
        while (start < text.size()) {
            const std::size_t newline = text.find('\n', start);
            const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
            pending.text.append(text.substr(start, end - start));
            if (newline == std::string_view::npos) break;
            if (!pending.text.empty()) complete.push_back({pending.channel, std::move(pending.text)});
            pending.text.clear();
            start = newline + 1;
        }
    }
    emit(std::move(complete));
}

int append_formatted(FILE *stream, const char *source, const char *format, va_list arguments)
{
    va_list measure;
    va_copy(measure, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (length < 0) return length;

    std::vector<char> text(static_cast<std::size_t>(length) + 1);
    va_list render;
    va_copy(render, arguments);
    const int rendered = std::vsnprintf(text.data(), text.size(), format, render);
    va_end(render);
    if (rendered < 0) return rendered;
    append_log(stream, source, std::string_view(text.data(), static_cast<std::size_t>(rendered)));
    return rendered;
}

} // namespace

extern "C" int lf2_log_fprintf(FILE *stream, const char *source, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result;
    if (stream == stderr || stream == stdout) result = append_formatted(stream, source, format, arguments);
    else result = std::vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

extern "C" int lf2_log_printf(const char *source, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = append_formatted(stdout, source, format, arguments);
    va_end(arguments);
    return result;
}

extern "C" int lf2_log_fputs(const char *source, const char *text, FILE *stream)
{
    if (stream != stderr && stream != stdout) return std::fputs(text, stream);
    append_log(stream, source, std::string_view(text));
    return 0;
}

extern "C" void lf2_log_perror(const char *source, const char *message)
{
    const int error = errno;
    const std::string detail = std::error_code(error, std::generic_category()).message();
    std::string text;
    if (message && *message) {
        text.append(message);
        text.append(": ");
    }
    text.append(detail);
    emit({{channel_from_source(source), std::move(text)}}, lucent::Level::Error);
}

extern "C" void lf2_log_write(Lf2LogLevel level, const char *channel, const char *message)
{
    lucent::Level lucent_level = lucent::Level::Info;
    if (level == LF2_LOG_WARNING) lucent_level = lucent::Level::Warn;
    if (level == LF2_LOG_ERROR) lucent_level = lucent::Level::Error;
    emit_message(channel && *channel ? channel : "lf2", message ? message : "", lucent_level);
}

extern "C" void lf2_log_flush(void)
{
    std::vector<CompleteLine> complete;
    {
        std::lock_guard lock(state().mutex);
        for (auto &[_, lines] : state().threads) {
            for (PendingLine *pending : {&lines.standard_error, &lines.standard_output}) {
                if (!pending->text.empty()) complete.push_back({std::move(pending->channel), std::move(pending->text)});
                *pending = {};
            }
        }
    }
    emit(std::move(complete));
}
