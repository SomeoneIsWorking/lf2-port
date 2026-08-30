#include "installer_extract.h"

#include <bzlib.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lf2::installer {
namespace {

constexpr std::size_t kMaximumInstallerBytes = 128u * 1024u * 1024u;
constexpr std::size_t kMaximumScriptBytes = 16u * 1024u * 1024u;
constexpr std::size_t kMaximumFileBytes = 64u * 1024u * 1024u;
constexpr std::uint64_t kMaximumExpandedBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaximumFiles = 10000;
constexpr std::size_t kMaximumPathBytes = 1024;
constexpr std::size_t kFileTableRecord = 5;
constexpr std::size_t kPayloadMarkerRecord = 6;
constexpr std::size_t kFirstTableEntry = 133;
constexpr std::size_t kTableNameOffset = 62;

struct DecodedStream {
    std::vector<std::uint8_t> bytes;
    std::size_t consumed = 0;
};

struct FileEntry {
    std::filesystem::path path;
    std::uint32_t compressed_size = 0;
    std::uint32_t expanded_size = 0;
};

bool read_u16(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint16_t &value)
{
    if (offset > bytes.size() || bytes.size() - offset < 2) return false;
    value = static_cast<std::uint16_t>(bytes[offset]) | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8u);
    return true;
}

bool read_u32(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint32_t &value)
{
    if (offset > bytes.size() || bytes.size() - offset < 4) return false;
    value = static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
    return true;
}

std::string ascii_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool read_installer(const std::filesystem::path &source, std::vector<std::uint8_t> &bytes, std::string &error)
{
    std::error_code status_error;
    const std::uintmax_t size = std::filesystem::file_size(source, status_error);
    if (status_error) {
        error = "cannot inspect the selected installer: " + status_error.message();
        return false;
    }
    if (size == 0 || size > kMaximumInstallerBytes) {
        error = "the selected installer exceeds the 128 MiB input limit";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    std::ifstream input(source, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "cannot read the selected installer";
        return false;
    }
    return true;
}

bool decode_zlib(std::span<const std::uint8_t> input, std::size_t output_limit,
                 std::optional<std::size_t> expected_size, DecodedStream &decoded, std::string &error)
{
    if (input.size() > std::numeric_limits<uInt>::max()) {
        error = "a zlib stream exceeds the supported input size";
        return false;
    }
    decoded.bytes.assign(output_limit + 1, 0);
    z_stream stream = {};
    stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(decoded.bytes.data());
    stream.avail_out = static_cast<uInt>(decoded.bytes.size());
    const int initialized = inflateInit(&stream);
    if (initialized != Z_OK) {
        error = "cannot initialize the installer zlib decoder";
        return false;
    }
    const int status = inflate(&stream, Z_FINISH);
    decoded.consumed = static_cast<std::size_t>(stream.total_in);
    const std::size_t produced = static_cast<std::size_t>(stream.total_out);
    inflateEnd(&stream);
    if (status != Z_STREAM_END || produced > output_limit || (expected_size && produced != *expected_size)) {
        error = expected_size ? "an installer zlib stream does not match its declared size"
                              : "an installer zlib stream exceeds the script byte limit";
        return false;
    }
    decoded.bytes.resize(produced);
    return true;
}

bool decode_bzip2(std::span<const std::uint8_t> input, std::size_t output_limit,
                  std::optional<std::size_t> expected_size, DecodedStream &decoded, std::string &error)
{
    if (input.size() > std::numeric_limits<unsigned int>::max()) {
        error = "a bzip2 stream exceeds the supported input size";
        return false;
    }
    decoded.bytes.assign(output_limit + 1, 0);
    bz_stream stream = {};
    stream.next_in = const_cast<char *>(reinterpret_cast<const char *>(input.data()));
    stream.avail_in = static_cast<unsigned int>(input.size());
    stream.next_out = reinterpret_cast<char *>(decoded.bytes.data());
    stream.avail_out = static_cast<unsigned int>(decoded.bytes.size());
    const int initialized = BZ2_bzDecompressInit(&stream, 0, 0);
    if (initialized != BZ_OK) {
        error = "cannot initialize the installer bzip2 decoder";
        return false;
    }
    const int status = BZ2_bzDecompress(&stream);
    decoded.consumed = (static_cast<std::size_t>(stream.total_in_hi32) << 32u) | stream.total_in_lo32;
    const std::size_t produced = (static_cast<std::size_t>(stream.total_out_hi32) << 32u) | stream.total_out_lo32;
    BZ2_bzDecompressEnd(&stream);
    if (status != BZ_STREAM_END || produced > output_limit || (expected_size && produced != *expected_size)) {
        error = expected_size ? "an installer bzip2 stream does not match its declared size"
                              : "an installer bzip2 stream exceeds the script byte limit";
        return false;
    }
    decoded.bytes.resize(produced);
    return true;
}

bool decode_stream(std::uint8_t method, std::span<const std::uint8_t> input, std::size_t output_limit,
                   std::optional<std::size_t> expected_size, DecodedStream &decoded, std::string &error)
{
    if (method == 1) return decode_zlib(input, output_limit, expected_size, decoded, error);
    if (method == 2) return decode_bzip2(input, output_limit, expected_size, decoded, error);
    if (method == 0) {
        if (!expected_size) {
            error = "a stored installer payload marker has no stream boundary";
            return false;
        }
        if (input.size() < *expected_size) {
            error = "a stored installer stream is truncated";
            return false;
        }
        decoded.bytes.assign(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(*expected_size));
        decoded.consumed = *expected_size;
        return true;
    }
    error = "the installer uses unknown compression method " + std::to_string(method);
    return false;
}

bool pe_overlay(std::span<const std::uint8_t> bytes, std::size_t &overlay, std::string &error)
{
    if (bytes.size() < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') {
        error = "the selected file is not a Windows PE installer";
        return false;
    }
    std::uint32_t pe = 0;
    std::uint16_t sections = 0;
    std::uint16_t optional_size = 0;
    if (!read_u32(bytes, 0x3c, pe) || pe > bytes.size() || bytes.size() - pe < 24 || bytes[pe] != 'P' ||
        bytes[pe + 1] != 'E' || bytes[pe + 2] != 0 || bytes[pe + 3] != 0 || !read_u16(bytes, pe + 6, sections) ||
        !read_u16(bytes, pe + 20, optional_size) || sections == 0 || sections > 96) {
        error = "the selected file has an invalid PE header";
        return false;
    }
    const std::size_t table = static_cast<std::size_t>(pe) + 24u + optional_size;
    if (table > bytes.size() || sections > (bytes.size() - table) / 40u) {
        error = "the selected file has a truncated PE section table";
        return false;
    }
    overlay = 0;
    for (std::size_t index = 0; index < sections; ++index) {
        std::uint32_t raw_size = 0;
        std::uint32_t raw_offset = 0;
        const std::size_t section = table + index * 40u;
        if (!read_u32(bytes, section + 16u, raw_size) || !read_u32(bytes, section + 20u, raw_offset) ||
            raw_offset > bytes.size() || raw_size > bytes.size() - raw_offset) {
            error = "the selected file has an invalid PE section range";
            return false;
        }
        overlay = std::max(overlay, static_cast<std::size_t>(raw_offset) + raw_size);
    }
    constexpr std::array<std::uint8_t, 4> magic = {'w', 'w', 'g', 'T'};
    if (overlay > bytes.size() || bytes.size() - overlay < 10 ||
        !std::equal(magic.begin(), magic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(overlay))) {
        error = "the selected executable is not the original LF2 v2.0a installer";
        return false;
    }
    return true;
}

bool locate_payload(std::span<const std::uint8_t> bytes, std::size_t overlay, std::vector<std::uint8_t> &table,
                    std::size_t &payload, std::string &error)
{
    std::size_t position = overlay + 10u;
    for (std::size_t index = 0; index <= kPayloadMarkerRecord; ++index) {
        std::uint32_t compressed_size = 0;
        std::uint32_t expanded_size = 0;
        if (!read_u32(bytes, position, compressed_size) || !read_u32(bytes, position + 4u, expanded_size) ||
            compressed_size < 5 || position > bytes.size() || bytes.size() - position < 9) {
            error = "the LF2 installer script table is truncated";
            return false;
        }
        const std::uint8_t method = bytes[position + 8u];
        const bool marker = index == kPayloadMarkerRecord;
        if (!marker && expanded_size > kMaximumScriptBytes) {
            error = "an LF2 installer script record exceeds the expanded byte limit";
            return false;
        }
        std::size_t input_size = bytes.size() - (position + 9u);
        if (!marker) {
            input_size = compressed_size - 5u;
            if (input_size > bytes.size() - (position + 9u) || compressed_size > bytes.size() - (position + 8u)) {
                error = "the LF2 installer has a truncated script record";
                return false;
            }
        } else if (compressed_size < bytes.size() - (position + 8u)) {
            error = "the LF2 installer payload marker does not span the file payload";
            return false;
        }

        DecodedStream decoded;
        const std::optional<std::size_t> expected_size = marker ? std::nullopt : std::optional(expanded_size);
        const std::size_t output_limit = marker ? kMaximumScriptBytes : expanded_size;
        if (!decode_stream(method, bytes.subspan(position + 9u, input_size), output_limit, expected_size, decoded,
                           error))
            return false;
        if (!marker && decoded.consumed != input_size) {
            error = "an LF2 installer script record contains trailing compressed data";
            return false;
        }
        if (index == kFileTableRecord) table = std::move(decoded.bytes);
        if (marker) {
            payload = position + 9u + decoded.consumed;
            return !table.empty();
        }
        position += 8u + compressed_size;
    }
    error = "the LF2 installer has no file payload";
    return false;
}

bool safe_installer_path(std::string_view raw, std::filesystem::path &path, std::string &error)
{
    if (raw.empty() || raw.size() > kMaximumPathBytes) {
        error = "the LF2 installer contains an empty or overlong file path";
        return false;
    }
    std::string normalized;
    normalized.reserve(raw.size());
    for (unsigned char character : raw) {
        if (character < 0x20 || character >= 0x7f || character == ':') {
            error = "the LF2 installer contains a non-portable file path";
            return false;
        }
        normalized.push_back(character == '\\' ? '/' : static_cast<char>(character));
    }
    if (normalized.front() == '/') {
        error = "the LF2 installer contains an absolute file path";
        return false;
    }
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        const std::size_t end = normalized.find('/', begin);
        const std::string_view component(normalized.data() + begin,
                                         (end == std::string::npos ? normalized.size() : end) - begin);
        if (component.empty() || component == "." || component == "..") {
            error = "the LF2 installer contains an unsafe file path";
            return false;
        }
        if (end == std::string::npos) break;
        begin = end + 1u;
    }
    path = std::filesystem::path(normalized);
    return true;
}

bool parse_file_table(std::span<const std::uint8_t> table, std::vector<FileEntry> &entries,
                      std::filesystem::path &executable, std::string &error)
{
    if (table.size() < kFirstTableEntry) {
        error = "the LF2 installer file table is truncated";
        return false;
    }
    std::size_t position = kFirstTableEntry;
    std::uint64_t expanded_total = 0;
    std::unordered_set<std::string> names;
    while (position < table.size()) {
        std::uint32_t length = 0;
        std::uint32_t compressed_size = 0;
        std::uint32_t expanded_size = 0;
        if (!read_u32(table, position, length) || length <= kTableNameOffset || length > table.size() - position ||
            !read_u32(table, position + 10u, compressed_size) || !read_u32(table, position + 18u, expanded_size) ||
            compressed_size < 2 || compressed_size > kMaximumFileBytes || expanded_size > kMaximumFileBytes) {
            error = "the LF2 installer contains an invalid file-table entry";
            return false;
        }
        const auto name_bytes = table.subspan(position + kTableNameOffset, length - kTableNameOffset);
        const auto terminator = std::find(name_bytes.begin(), name_bytes.end(), 0);
        if (terminator == name_bytes.end()) {
            error = "the LF2 installer contains an unterminated file name";
            return false;
        }
        const std::string_view raw_name(reinterpret_cast<const char *>(name_bytes.data()),
                                        static_cast<std::size_t>(terminator - name_bytes.begin()));
        FileEntry entry;
        if (!safe_installer_path(raw_name, entry.path, error)) return false;
        entry.compressed_size = compressed_size;
        entry.expanded_size = expanded_size;
        const std::string folded_name = ascii_lower(entry.path.generic_string());
        if (!names.insert(folded_name).second) {
            error = "the LF2 installer contains duplicate output path " + entry.path.generic_string();
            return false;
        }
        expanded_total += expanded_size;
        if (expanded_total > kMaximumExpandedBytes) {
            error = "the LF2 installer exceeds the 512 MiB expanded byte limit";
            return false;
        }
        if (ascii_lower(entry.path.filename().string()) == "lf2.exe") {
            if (!executable.empty()) {
                error = "the LF2 installer contains more than one lf2.exe";
                return false;
            }
            executable = entry.path;
        }
        entries.push_back(std::move(entry));
        if (entries.size() > kMaximumFiles) {
            error = "the LF2 installer exceeds the 10000-file entry limit";
            return false;
        }
        position += length;
    }
    if (entries.empty() || executable.empty()) {
        error = "the selected installer contains no lf2.exe";
        return false;
    }
    return true;
}

bool write_file(const std::filesystem::path &path, std::span<const std::uint8_t> bytes, std::string &error)
{
    std::error_code status_error;
    std::filesystem::create_directories(path.parent_path(), status_error);
    if (status_error) {
        error = "cannot create installer output directory: " + status_error.message();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output ||
        !output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "cannot write extracted installer file " + path.string();
        return false;
    }
    return true;
}

std::uint64_t size_key(const FileEntry &entry)
{
    return (static_cast<std::uint64_t>(entry.compressed_size) << 32u) | entry.expanded_size;
}

bool extract_files(std::span<const std::uint8_t> bytes, std::size_t payload, const std::vector<FileEntry> &entries,
                   const std::filesystem::path &destination, std::string &error)
{
    std::unordered_map<std::uint64_t, std::filesystem::path> extracted_by_size;
    for (const FileEntry &entry : entries) {
        const std::filesystem::path target = destination / entry.path;
        bool consumed_payload = false;
        std::string decode_error;
        if (payload < bytes.size() && entry.compressed_size <= bytes.size() - payload) {
            const std::uint8_t method = bytes[payload];
            if (method == 1 || method == 2) {
                DecodedStream decoded;
                const auto stream = bytes.subspan(payload + 1u, entry.compressed_size - 1u);
                if (decode_stream(method, stream, entry.expanded_size, entry.expanded_size, decoded, decode_error) &&
                    decoded.consumed == stream.size()) {
                    if (!write_file(target, decoded.bytes, error)) return false;
                    payload += entry.compressed_size;
                    extracted_by_size.emplace(size_key(entry), target);
                    consumed_payload = true;
                }
            }
        }
        if (consumed_payload) continue;

        const auto duplicate = extracted_by_size.find(size_key(entry));
        if (duplicate == extracted_by_size.end()) {
            error = decode_error.empty() ? "the LF2 installer payload is truncated" : decode_error;
            return false;
        }
        std::error_code copy_error;
        std::filesystem::create_directories(target.parent_path(), copy_error);
        if (!copy_error)
            std::filesystem::copy_file(duplicate->second, target, std::filesystem::copy_options::none, copy_error);
        if (copy_error) {
            error = "cannot reproduce a deduplicated installer file: " + copy_error.message();
            return false;
        }
    }
    if (payload != bytes.size()) {
        error = "the LF2 installer contains unclaimed payload bytes";
        return false;
    }
    return true;
}

} // namespace

bool extract_install(const std::filesystem::path &source, const std::filesystem::path &destination,
                     std::filesystem::path &executable, std::string &error)
{
    executable.clear();
    error.clear();
    std::vector<std::uint8_t> bytes;
    if (!read_installer(source, bytes, error)) return false;

    std::size_t overlay = 0;
    std::size_t payload = 0;
    std::vector<std::uint8_t> table;
    if (!pe_overlay(bytes, overlay, error) || !locate_payload(bytes, overlay, table, payload, error)) return false;

    std::vector<FileEntry> entries;
    std::filesystem::path relative_executable;
    if (!parse_file_table(table, entries, relative_executable, error)) return false;

    std::error_code status_error;
    if (std::filesystem::exists(destination, status_error)) {
        if (status_error || !std::filesystem::is_directory(destination, status_error) ||
            !std::filesystem::is_empty(destination, status_error)) {
            error = "the installer preparation directory is not empty";
            return false;
        }
    } else {
        std::filesystem::create_directories(destination, status_error);
        if (status_error) {
            error = "cannot create the installer preparation directory: " + status_error.message();
            return false;
        }
    }
    if (!extract_files(bytes, payload, entries, destination, error)) return false;
    executable = destination / relative_executable;
    return true;
}

} // namespace lf2::installer
