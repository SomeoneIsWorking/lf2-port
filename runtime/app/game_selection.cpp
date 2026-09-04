#include "lf2_log.h"
#include "game_selection.h"
#include "game_data.h"
#include "installer_extract.h"

#include <lucent/platform.h>
#include <lucent/zip.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

enum class ImportKind { zip, installer };

const char *kind_name(ImportKind kind)
{
    return kind == ImportKind::zip ? "ZIP" : "installer";
}

bool copy_path(const std::filesystem::path &path, char *output, std::size_t capacity, char *error,
               std::size_t error_capacity)
{
    const std::string value = path.string();
    const int written = std::snprintf(output, capacity, "%s", value.c_str());
    if (written >= 0 && static_cast<std::size_t>(written) < capacity) return true;
    std::snprintf(error, error_capacity, "resolved game path is longer than %zu bytes", capacity - 1);
    return false;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool equal_extension(const std::filesystem::path &path, std::string_view wanted)
{
    return lowercase(path.extension().string()) == wanted;
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

bool restore_interrupted_import(const std::filesystem::path &destination, const std::filesystem::path &previous,
                                char *error, std::size_t error_capacity)
{
    std::error_code status_error;
    const bool destination_exists = std::filesystem::exists(destination, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot inspect the previous game import: %s",
                      status_error.message().c_str());
        return false;
    }
    if (destination_exists || !std::filesystem::exists(previous, status_error)) {
        if (!status_error) return true;
        std::snprintf(error, error_capacity, "cannot inspect the game-import backup: %s",
                      status_error.message().c_str());
        return false;
    }
    std::filesystem::rename(previous, destination, status_error);
    if (!status_error) return true;
    std::snprintf(error, error_capacity, "cannot restore the previous game import: %s", status_error.message().c_str());
    return false;
}

bool prepare_import(const std::filesystem::path &source, ImportKind kind, const std::filesystem::path &preparing,
                    std::filesystem::path &resolved, char *error, std::size_t error_capacity)
{
    std::string extraction_error;
    const bool extracted = kind == ImportKind::zip
                               ? lucent::zip::extract_install(source, preparing, "lf2.exe", resolved, extraction_error)
                               : lf2::installer::extract_install(source, preparing, resolved, extraction_error);
    if (extracted) return true;

    std::error_code cleanup_error;
    std::filesystem::remove_all(preparing, cleanup_error);
    std::snprintf(error, error_capacity, "that %s could not be used: %s%s", kind_name(kind), extraction_error.c_str(),
                  cleanup_error ? "; its incomplete preparation could not be removed" : "");
    return false;
}

bool validate_prepared_import(const std::filesystem::path &resolved, const std::filesystem::path &preparing,
                              ImportKind kind, std::filesystem::path &relative_executable, char *error,
                              std::size_t error_capacity)
{
    GameData prepared_game;
    if (!game_data_validate_executable(resolved.string().c_str(), &prepared_game)) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(preparing, cleanup_error);
        std::snprintf(error, error_capacity, "that %s is not a complete LF2 v2.0a install: %s%s", kind_name(kind),
                      prepared_game.error, cleanup_error ? "; its invalid preparation could not be removed" : "");
        return false;
    }
    std::error_code canonical_error;
    const std::filesystem::path canonical_preparing = std::filesystem::canonical(preparing, canonical_error);
    if (canonical_error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(preparing, cleanup_error);
        std::snprintf(error, error_capacity, "cannot resolve the prepared %s import: %s%s", kind_name(kind),
                      canonical_error.message().c_str(),
                      cleanup_error ? "; its invalid preparation could not be removed" : "");
        return false;
    }
    relative_executable = std::filesystem::path(prepared_game.executable).lexically_relative(canonical_preparing);
    if (!relative_executable.empty() && !relative_executable.is_absolute() &&
        relative_executable.begin() != relative_executable.end() && *relative_executable.begin() != "..")
        return true;

    std::error_code cleanup_error;
    std::filesystem::remove_all(preparing, cleanup_error);
    std::snprintf(error, error_capacity, "that %s produced an invalid executable path", kind_name(kind));
    return false;
}

bool commit_import(const std::filesystem::path &preparing, const std::filesystem::path &destination,
                   const std::filesystem::path &previous, bool destination_exists, ImportKind kind, char *error,
                   std::size_t error_capacity)
{
    std::error_code status_error;
    std::filesystem::remove_all(previous, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot clean the game-import backup: %s", status_error.message().c_str());
        return false;
    }
    bool moved_previous = false;
    if (destination_exists) {
        std::filesystem::rename(destination, previous, status_error);
        moved_previous = !status_error;
    }
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot preserve the previous game import: %s",
                      status_error.message().c_str());
        return false;
    }
    std::filesystem::rename(preparing, destination, status_error);
    if (status_error) {
        std::error_code restore_error;
        if (moved_previous) std::filesystem::rename(previous, destination, restore_error);
        std::snprintf(error, error_capacity, "cannot accept the prepared %s import: %s%s", kind_name(kind),
                      status_error.message().c_str(),
                      restore_error ? "; the previous import could not be restored" : "");
        return false;
    }
    if (moved_previous) {
        std::filesystem::remove_all(previous, status_error);
        if (status_error)
            lf2_log_writef(LF2_LOG_INFO, "game_selection",
                           "setup: accepted %s but could not remove its previous import: %s\n", kind_name(kind),
                           status_error.message().c_str());
    }
    return true;
}

bool resolve_import(const std::filesystem::path &source, ImportKind kind, char *executable,
                    std::size_t executable_capacity, char *error, std::size_t error_capacity)
{
    const auto user_data = lucent::platform::user_data_directory("lf2-port");
    if (!user_data) {
        std::snprintf(error, error_capacity, "cannot resolve the OS user-data directory for %s extraction",
                      kind_name(kind));
        return false;
    }
    std::string directory_error;
    if (!lucent::platform::ensure_user_data_directory(*user_data, directory_error)) {
        std::snprintf(error, error_capacity, "cannot create the game-import directory: %s", directory_error.c_str());
        return false;
    }

    const std::filesystem::path destination = *user_data / "game-import";
    const std::filesystem::path preparing = destination.string() + ".preparing";
    const std::filesystem::path previous = destination.string() + ".previous";
    std::error_code status_error;
    std::filesystem::remove_all(preparing, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot clean stale game-import preparation: %s",
                      status_error.message().c_str());
        return false;
    }
    if (!restore_interrupted_import(destination, previous, error, error_capacity)) return false;
    const bool destination_exists = std::filesystem::exists(destination, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot inspect the previous game import: %s",
                      status_error.message().c_str());
        return false;
    }

    std::filesystem::path resolved;
    if (!prepare_import(source, kind, preparing, resolved, error, error_capacity)) return false;
    std::filesystem::path relative_executable;
    if (!validate_prepared_import(resolved, preparing, kind, relative_executable, error, error_capacity)) return false;
    if (!copy_path(destination / relative_executable, executable, executable_capacity, error, error_capacity)) {
        std::filesystem::remove_all(preparing, status_error);
        return false;
    }
    if (commit_import(preparing, destination, previous, destination_exists, kind, error, error_capacity)) return true;
    executable[0] = 0;
    return false;
}

bool resolve_staged_import(const std::filesystem::path &source, ImportKind kind, char *executable,
                           std::size_t executable_capacity, char *error, std::size_t error_capacity)
{
    const std::filesystem::path preparing = source.parent_path() / "prepared";
    std::error_code status_error;
    std::filesystem::remove_all(preparing, status_error);
    if (status_error) {
        std::snprintf(error, error_capacity, "cannot clean stale staged import preparation: %s",
                      status_error.message().c_str());
        return false;
    }

    std::filesystem::path resolved;
    if (!prepare_import(source, kind, preparing, resolved, error, error_capacity)) return false;
    std::filesystem::path relative_executable;
    if (!validate_prepared_import(resolved, preparing, kind, relative_executable, error, error_capacity)) return false;
    return copy_path(preparing / relative_executable, executable, executable_capacity, error, error_capacity);
}

int resolve_selection(const char *selection, bool staged, char *executable, std::size_t executable_capacity,
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
    const auto resolve_archive = [&](ImportKind kind) {
        return staged ? resolve_staged_import(source, kind, executable, executable_capacity, error, error_capacity)
                      : resolve_import(source, kind, executable, executable_capacity, error, error_capacity);
    };
    if (equal_extension(source, ".zip")) return resolve_archive(ImportKind::zip);
    if (equal_extension(source, ".exe") && lowercase(source.filename().string()) != "lf2.exe")
        return resolve_archive(ImportKind::installer);
    return validate_candidate(source, executable, executable_capacity, error, error_capacity);
}

} // namespace

extern "C" int game_selection_resolve(const char *selection, char *executable, std::size_t executable_capacity,
                                      char *error, std::size_t error_capacity)
{
    return resolve_selection(selection, false, executable, executable_capacity, error, error_capacity);
}

extern "C" int game_selection_resolve_staged(const char *selection, char *executable, std::size_t executable_capacity,
                                             char *error, std::size_t error_capacity)
{
    return resolve_selection(selection, true, executable, executable_capacity, error, error_capacity);
}
