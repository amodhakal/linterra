#include "io.h"

#include <filesystem>
#include <fstream>
#include <sstream>

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
const std::string resolvePath(const char *filePath) {
  namespace fs = std::filesystem;
  const fs::path requested(filePath);
  if (requested.is_absolute() || fs::exists(requested)) {
    return std::string(filePath);
  }
  const fs::path fallback = executableDir() / requested;
  return fs::exists(fallback) ? fallback.lexically_normal().string()
                              : std::string(filePath);
}

const std::string getFullFileContents(const char *filePath) {
  // Open in binary mode so original line endings (\n, \r\n, ...) are preserved
  // verbatim instead of being stripped/re-normalized.
  std::ifstream file(resolvePath(filePath), std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Couldn't open file: " + std::string(filePath));
  }

  std::ostringstream content;
  content << file.rdbuf();
  return content.str();
}

}  // namespace IO
