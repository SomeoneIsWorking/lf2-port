#ifndef LF2_INSTALLER_EXTRACT_H
#define LF2_INSTALLER_EXTRACT_H

#include <filesystem>
#include <string>

namespace lf2::installer {

/* Extract the original LF2 v2.0a self-extracting installer into an empty
 * preparation directory. The custom container, paths, counts, and byte
 * budgets are validated before the resolved lf2.exe is returned. */
bool extract_install(const std::filesystem::path &source, const std::filesystem::path &destination,
                     std::filesystem::path &executable, std::string &error);

} // namespace lf2::installer

#endif
