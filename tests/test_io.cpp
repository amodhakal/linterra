#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "doctest/doctest.h"
#include "io.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

namespace fs = std::filesystem;

// Scratch file whose contents are fully controlled, including the bytes that
// text-mode reads would mangle.
class TempFile {
 public:
  explicit TempFile(const std::string& contents, std::string name = "io_test_tmp.bin") {
    m_Path = fs::temp_directory_path() / name;
    std::ofstream out(m_Path, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }

  ~TempFile() {
    std::error_code ignored;
    fs::remove(m_Path, ignored);
  }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  // By value: fs::path::string() returns a temporary.
  std::string path() const { return m_Path.string(); }

 private:
  fs::path m_Path;
};

// Unique name for the executable-directory probe, so repeated runs cannot
// collide on a leftover file. Uses the clock rather than getpid() to stay
// portable to Windows.
std::string uniqueName() {
  const auto tick =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return "linterra_exedir_probe_" + std::to_string(tick) + ".txt";
}

// Directory holding the running test binary. This mirrors the lookup in
// src/io.cpp on purpose: the test has to stand next to the code it checks, and
// the probe file is written into that same directory.
fs::path executableDir() {
#if defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size, '\0');
  _NSGetExecutablePath(buf.data(), &size);
  buf.resize(std::strlen(buf.c_str()));
  return fs::path(buf).parent_path();
#elif defined(_WIN32)
  return fs::current_path();  // ctest runs from the build directory
#else
  return fs::path(fs::read_symlink("/proc/self/exe")).parent_path();
#endif
}

// Restores the working directory on scope exit, including on a failed
// REQUIRE, so one test cannot leave the rest of the suite somewhere else.
class CwdGuard {
 public:
  explicit CwdGuard(fs::path previous) : m_Previous(std::move(previous)) {}
  ~CwdGuard() {
    std::error_code ignored;
    fs::current_path(m_Previous, ignored);
  }

  CwdGuard(const CwdGuard&) = delete;
  CwdGuard& operator=(const CwdGuard&) = delete;

 private:
  fs::path m_Previous;
};

// Deletes a file on scope exit. The probe file is written next to the test
// binary, so without this a failing test would litter the build directory on
// every run.
class FileRemover {
 public:
  explicit FileRemover(fs::path path) : m_Path(std::move(path)) {}
  ~FileRemover() {
    std::error_code ignored;
    fs::remove(m_Path, ignored);
  }

  FileRemover(const FileRemover&) = delete;
  FileRemover& operator=(const FileRemover&) = delete;

 private:
  fs::path m_Path;
};

}  // namespace

TEST_CASE("getFullFileContents reads a file back verbatim") {
  const std::string contents = "first line\nsecond line\n";
  const TempFile file(contents);
  CHECK(IO::getFullFileContents(file.path().c_str()) == contents);
}

TEST_CASE("getFullFileContents preserves CRLF line endings") {
  // Read in text mode, a CRLF file would come back LF-only. This is the
  // regression that #42 fixed; GLSL on some drivers is sensitive to it.
  const std::string contents = "line one\r\nline two\r\n";
  const TempFile file(contents, "io_test_crlf.txt");
  const std::string read = IO::getFullFileContents(file.path().c_str());
  CHECK(read == contents);
  CHECK(read.find("\r\n") != std::string::npos);
}

TEST_CASE("getFullFileContents preserves mixed and lone line endings") {
  const std::string contents = "unix\nwindows\r\nold-mac\rno-trailing-newline";
  const TempFile file(contents, "io_test_mixed.txt");
  CHECK(IO::getFullFileContents(file.path().c_str()) == contents);
}

TEST_CASE("getFullFileContents is byte-exact for binary payloads") {
  std::string contents;
  for (int i = 0; i < 256; ++i) {
    contents.push_back(static_cast<char>(i));
  }
  const TempFile file(contents, "io_test_binary.bin");
  CHECK(IO::getFullFileContents(file.path().c_str()) == contents);
}

TEST_CASE("getFullFileContents reads an empty file as empty") {
  const TempFile file("", "io_test_empty.txt");
  CHECK(IO::getFullFileContents(file.path().c_str()).empty());
}

TEST_CASE("getFullFileContents throws for a missing file") {
  CHECK_THROWS_AS(
      IO::getFullFileContents("definitely_not_a_real_file_9d2f1a.txt"),
      std::runtime_error);
}

TEST_CASE("resolvePath returns an absolute path unchanged") {
  const std::string absolute = (fs::temp_directory_path() / "whatever.bin").string();
  CHECK(IO::resolvePath(absolute.c_str()) == absolute);
}

TEST_CASE("resolvePath returns a cwd-relative path unchanged when it exists") {
  const TempFile file("x", "io_test_cwd_probe.txt");
  // The temp file is not under the working directory, so this exercises the
  // "does not exist relative to cwd" branch; the string must come back
  // untouched rather than being rewritten into something nonexistent.
  const std::string name = fs::path(file.path()).filename().string();
  const std::string resolved = IO::resolvePath(name.c_str());
  CHECK(resolved == name);
}

TEST_CASE("resolvePath falls back to the executable directory") {
  // The engine is launched from arbitrary working directories but has to find
  // shaders/ and resources/ next to the binary, so a relative path that is
  // absent from the cwd must be retried against the executable's directory.
  const std::string name = uniqueName();
  const fs::path probe = executableDir() / name;
  const FileRemover removeProbe{probe};

  {
    std::ofstream out(probe);
    REQUIRE(out.is_open());
    out << "probe";
  }

  // ctest runs the binary with the build directory as the working directory,
  // which on macOS is also the executable's directory -- in which case the
  // cwd-relative lookup would succeed and the fallback would never be reached.
  // Move somewhere else for the duration so the two paths are distinguishable.
  const fs::path originalCwd = fs::current_path();
  const fs::path elsewhere =
      fs::temp_directory_path() / ("linterra_cwd_" + name);
  const FileRemover removeCwdDir{elsewhere};
  fs::create_directories(elsewhere);
  fs::current_path(elsewhere);
  const CwdGuard restoreCwd{originalCwd};

  // Guard the premise: the probe must NOT be reachable relative to the cwd,
  // otherwise this would pass without exercising the fallback.
  REQUIRE_FALSE(fs::exists(fs::path(name)));

  const std::string resolved = IO::resolvePath(name.c_str());
  // Compare canonically: the executable path can arrive through a symlink
  // (/tmp on macOS) while the probe is built from the same base, so normalise
  // both sides before comparing.
  CHECK(fs::weakly_canonical(resolved) == fs::weakly_canonical(probe));
}
