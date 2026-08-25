#include "lf2_log.h"

#undef fprintf
#undef printf
#undef fputs
#undef perror

#include <lucent/log.h>

#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::fprintf(stderr, "test_lf2_log:%d: %s\n", __LINE__, #condition);                                       \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

std::string without_timestamp(std::string_view line)
{
    constexpr std::size_t kTimestampLength = 27;
    const bool shape = line.size() >= kTimestampLength && line[0] == '[' && line[5] == '-' && line[8] == '-' &&
                       line[11] == 'T' && line[14] == ':' && line[17] == ':' && line[20] == '.' && line[24] == 'Z' &&
                       line[25] == ']' && line[26] == ' ';
    bool digits = shape;
    for (std::size_t i : {1u, 2u, 3u, 4u, 6u, 7u, 9u, 10u, 12u, 13u, 15u, 16u, 18u, 19u, 21u, 22u, 23u})
        digits = digits && std::isdigit(static_cast<unsigned char>(line[i]));
    CHECK(shape && digits);
    return shape && digits ? std::string(line.substr(kTimestampLength)) : std::string(line);
}

void test_fragments_and_embedded_newlines_are_complete_lucent_records()
{
    std::vector<std::string> lines;
    lucent::set_sink([&lines](lucent::Level, std::string_view line) { lines.emplace_back(line); });

    lf2_log_fprintf(stderr, "/runtime/win32/com.c", "com releases:");
    lf2_log_fprintf(stderr, "/runtime/win32/com.c", " none");
    lf2_log_fputs("/runtime/win32/com.c", "\nsecond\nthird", stderr);
    lf2_log_fprintf(stderr, "/runtime/win32/com.c", " tail\n");
    lf2_log_fputs("/runtime/win32/com.c", "\n", stderr);

    CHECK(lines.size() == 3);
    if (lines.size() == 3) {
        CHECK(without_timestamp(lines[0]) == "[com] com releases: none");
        CHECK(without_timestamp(lines[1]) == "[com] second");
        CHECK(without_timestamp(lines[2]) == "[com] third tail");
    }
    lucent::set_sink(nullptr);
}

void test_complete_messages_split_lines_and_preserve_severity()
{
    std::vector<std::string> lines;
    lucent::set_sink([&lines](lucent::Level, std::string_view line) { lines.emplace_back(line); });

    lf2_log_write(LF2_LOG_WARNING, "rmlui", "first\nsecond\n");

    CHECK(lines.size() == 2);
    if (lines.size() == 2) {
        CHECK(without_timestamp(lines[0]) == "[rmlui:warn] first");
        CHECK(without_timestamp(lines[1]) == "[rmlui:warn] second");
    }
    lucent::set_sink(nullptr);
}

void test_non_log_file_output_is_unchanged()
{
    FILE *file = std::tmpfile();
    CHECK(file != nullptr);
    if (!file) return;
    CHECK(lf2_log_fprintf(file, "/runtime/app/config.c", "width %d\n", 800) == 10);
    std::rewind(file);
    char text[32] = {};
    CHECK(std::fread(text, 1, sizeof(text) - 1, file) == 10);
    CHECK(std::string(text) == "width 800\n");
    std::fclose(file);
}

} // namespace

int main()
{
    test_fragments_and_embedded_newlines_are_complete_lucent_records();
    test_complete_messages_split_lines_and_preserve_severity();
    test_non_log_file_output_is_unchanged();
    lf2_log_flush();
    return failures == 0 ? 0 : 1;
}
