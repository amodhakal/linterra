#pragma once
#include <string>

namespace IO {
// Resolve filePath against the working directory, falling back to the
// executable's directory for relative paths.
const std::string resolvePath(const char *filePath);
// Read the entire file byte-for-byte, preserving original line endings.
const std::string getFullFileContents(const char *filePath);
}
