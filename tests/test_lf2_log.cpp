#include "lf2_log.h"

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

void test_formatted_messages_are_complete_lucent_records()
{
    std::vector<std::string> lines;
    lucent::set_sink([&lines](lucent::Level, std::string_view line) { lines.emplace_back(line); });

    lf2_log_writef(LF2_LOG_INFO, "com", "objects=%d", 3);

    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(without_timestamp(lines[0]) == "[com] objects=3");
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

} // namespace

int main()
{
    test_formatted_messages_are_complete_lucent_records();
    test_complete_messages_split_lines_and_preserve_severity();
    return failures == 0 ? 0 : 1;
}
