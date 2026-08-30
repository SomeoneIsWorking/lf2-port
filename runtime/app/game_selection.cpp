#include "game_selection.h"
#include "game_data.h"

#include <lucent/platform.h>
#include <lucent/zip.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

bool copy_path(const std::filesystem::path &path, char *output, std::size_t capacity, char *error,
               std::size_t error_capacity)
{
    const std::string value = path.string();
    const int written = std::snprintf(output, capacity, "%s", value.c_str());
    if (written >= 0 && static_cast<std::size_t>(written) < capacity) return true;
    std::snprintf(error, error_capacity, "resolved game path is longer than %zu bytes", capacity - 1);
    return false;
}

bool equal_extension(std::string extension, std::string_view wanted)
{
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == wanted;
}

bool validate_candidate(const std::filesystem::path &path, char *executable, std::size_t executable_capacity,
                        char *error, std::size_t error_capacity)
{
    GameData game;
    if (!game_data_validate_executable(path.string().c_str(), &game)) {
        std::snprintf(error, error_capacity, "%s", game.error);
        return false;
    }
    return copy_path(game.executable, executable, executable_capacity, error, error_capacity);
}

} // namespace

extern "C" int game_selection_resolve(const char *selection, char *executable, std::size_t executable_capacity,
                                      char *error, std::size_t error_capacity)
{
    if (!selection || !*selection || !executable || executable_capacity == 0 || !error || error_capacity == 0) return 0;
    executable[0] = 0;
    error[0] = 0;

    const std::filesystem::path source(selection);
    std::error_code status_error;
    if (std::filesystem::is_directory(source, status_error))
        return validate_candidate(source / "lf2.exe", executable, executable_capacity, error, error_capacity);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot inspect the selected path: %s", status_error.message().c_str());
        return 0;
    }
    if (!equal_extension(source.extension().string(), ".zip"))
        return validate_candidate(source, executable, executable_capacity, error, error_capacity);

    const auto user_data = lucent::platform::user_data_directory("lf2-port");
    if (!user_data) {
        std::snprintf(error, error_capacity, "cannot resolve the OS user-data directory for ZIP extraction");
        return 0;
    }
    std::string directory_error;
    if (!lucent::platform::ensure_user_data_directory(*user_data, directory_error)) {
        std::snprintf(error, error_capacity, "cannot create the ZIP extraction directory: %s", directory_error.c_str());
        return 0;
    }

    const std::filesystem::path destination = *user_data / "game-import";
    const std::filesystem::path preparing = destination.string() + ".preparing";
    const std::filesystem::path previous = destination.string() + ".previous";
    std::filesystem::remove_all(preparing, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot clean stale ZIP preparation: %s", status_error.message().c_str());
        return 0;
    }
    const bool destination_exists = std::filesystem::exists(destination, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot inspect the previous ZIP extraction: %s",
                      status_error.message().c_str());
        return 0;
    }
    if (!destination_exists && std::filesystem::exists(previous, status_error)) {
        std::filesystem::rename(previous, destination, status_error);
        if (status_error) {
            std::snprintf(error, error_capacity, "cannot restore the previous ZIP extraction: %s",
                          status_error.message().c_str());
            return 0;
        }
    } else if (status_error) {
        std::snprintf(error, error_capacity, "cannot inspect the ZIP extraction backup: %s",
                      status_error.message().c_str());
        return 0;
    }

    std::filesystem::path resolved;
    std::string extraction_error;
    if (!lucent::zip::extract_install(source, preparing, "lf2.exe", resolved, extraction_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(preparing, cleanup_error);
        std::snprintf(error, error_capacity, "that ZIP could not be used: %s%s", extraction_error.c_str(),
                      cleanup_error ? "; its incomplete preparation could not be removed" : "");
        return 0;
    }
    GameData prepared_game;
    if (!game_data_validate_executable(resolved.string().c_str(), &prepared_game)) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(preparing, cleanup_error);
        std::snprintf(error, error_capacity, "that ZIP is not a complete LF2 v2.0a install: %s%s", prepared_game.error,
                      cleanup_error ? "; its invalid preparation could not be removed" : "");
        return 0;
    }
    const std::filesystem::path relative_executable =
        std::filesystem::path(prepared_game.executable).lexically_relative(preparing);
    if (relative_executable.empty() || relative_executable.is_absolute() ||
        relative_executable.begin() == relative_executable.end() || *relative_executable.begin() == "..") {
        std::filesystem::remove_all(preparing, status_error);
        std::snprintf(error, error_capacity, "that ZIP produced an invalid executable path");
        return 0;
    }
    if (!copy_path(destination / relative_executable, executable, executable_capacity, error, error_capacity)) {
        std::filesystem::remove_all(preparing, status_error);
        return 0;
    }

    status_error.clear();
    std::filesystem::remove_all(previous, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot clean the ZIP extraction backup: %s",
                      status_error.message().c_str());
        return 0;
    }
    bool moved_previous = false;
    if (std::filesystem::exists(destination, status_error)) {
        std::filesystem::rename(destination, previous, status_error);
        moved_previous = !status_error;
    }
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot preserve the previous ZIP extraction: %s",
                      status_error.message().c_str());
        return 0;
    }
    std::filesystem::rename(preparing, destination, status_error);
    if (status_error) {
        executable[0] = 0;
        std::error_code restore_error;
        if (moved_previous) std::filesystem::rename(previous, destination, restore_error);
        std::snprintf(error, error_capacity, "cannot accept the prepared ZIP extraction: %s%s",
                      status_error.message().c_str(),
                      restore_error ? "; the previous extraction could not be restored" : "");
        return 0;
    }
    if (moved_previous) {
        std::filesystem::remove_all(previous, status_error);
        if (status_error)
            fprintf(stderr, "setup: accepted ZIP but could not remove its previous extraction: %s\n",
                    status_error.message().c_str());
    }
    return 1;
}
