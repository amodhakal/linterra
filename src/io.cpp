#include "io.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace {

// Directory containing the running executable, so resources can be located
// regardless of the process's current working directory.
std::filesystem::path executableDir() {
  std::string buf;
#if defined(_WIN32)
  char path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  buf = path;
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  buf.resize(size);
  _NSGetExecutablePath(buf.data(), &size);
  buf.resize(std::strlen(buf.c_str()));
#else
  buf = std::filesystem::read_symlink("/proc/self/exe").string();
#endif
  return std::filesystem::path(buf).parent_path();
}

}  // namespace

namespace IO {

// Resolve filePath against the current working directory first; if it does not
// exist there and the path is relative, fall back to the executable's
// directory so shaders/resources load no matter where the app was launched.
namespace {

// Every location resolvePath searched, in order, as absolute-ish strings.
// Populated by resolvePath so a failure can name them; see the note there.
std::vector<std::string> g_SearchedPaths;

}  // namespace

const std::string resolvePath(const char *filePath) {
  namespace fs = std::filesystem;
  const fs::path requested(filePath);

  g_SearchedPaths.clear();
  g_SearchedPaths.push_back(requested.lexically_normal().string());

  if (requested.is_absolute() || fs::exists(requested)) {
    return std::string(filePath);
  }

  // Only relative paths get the executable-directory fallback.
  const fs::path fallback = executableDir() / requested;
  g_SearchedPaths.push_back(fallback.lexically_normal().string());
  return fs::exists(fallback) ? fallback.lexically_normal().string()
                              : std::string(filePath);
}

const std::string getFullFileContents(const char *filePath) {
  // Open in binary mode so original line endings (\n, \r\n, ...) are preserved
  // verbatim instead of being stripped/re-normalized.
  const std::string resolved = resolvePath(filePath);
  std::ifstream file(resolved, std::ios::binary);
  if (!file.is_open()) {
    // Name every location that was searched. A bare relative path says
    // nothing about whether the executable's directory was tried, and that is
    // usually where the answer is. Naming both turns "couldn't open
    // ./shaders/render.vert" into something actionable -- either the working
    // directory is wrong, or shaders/ was not copied next to the binary
    // (#144).
    std::string message = "Couldn't open file: " + resolved;
    if (g_SearchedPaths.size() > 1) {
      message += " (searched: ";
      for (std::size_t i = 0; i < g_SearchedPaths.size(); ++i) {
        if (i != 0) {
          message += ", ";
        }
        message += g_SearchedPaths[i];
      }
      message += ")";
    }
    throw std::runtime_error(message);
  }

  std::ostringstream content;
  content << file.rdbuf();
  return content.str();
}

}  // namespace IO
