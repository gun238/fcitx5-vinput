#include "common/utils/file_utils.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <cstdio>
#else
#include <unistd.h>
#endif

namespace {

constexpr int kMaxSymlinkDepth = 32;

std::filesystem::path ResolveRelativeSymlinkTarget(
    const std::filesystem::path& symlink_path,
    const std::filesystem::path& link_target) {
  if (link_target.is_absolute()) {
    return link_target;
  }

  const auto parent = symlink_path.parent_path();
  if (parent.empty()) {
    return link_target.lexically_normal();
  }

  std::error_code ec;
  const auto canonical_parent = std::filesystem::canonical(parent, ec);
  if (!ec) {
    return (canonical_parent / link_target).lexically_normal();
  }
  return (parent / link_target).lexically_normal();
}

bool ResolveSymlinkPathImpl(const std::filesystem::path& path,
                            std::filesystem::path* resolved,
                            std::string* error, int depth) {
  if (depth >= kMaxSymlinkDepth) {
    if (error) {
      *error = "Too many symlink levels while resolving " + path.string();
    }
    return false;
  }

  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    const auto parent = path.parent_path();
    if (parent.empty()) {
      *resolved = path;
      return true;
    }

    std::filesystem::path resolved_parent;
    if (!ResolveSymlinkPathImpl(parent, &resolved_parent, error, depth)) {
      return false;
    }

    const auto filename = path.filename();
    if (filename.empty()) {
      *resolved = resolved_parent;
    } else {
      *resolved = (resolved_parent / filename).lexically_normal();
    }
    return true;
  }
  if (ec) {
    if (error) {
      *error = "Failed to inspect path " + path.string() + ": " +
               ec.message();
    }
    return false;
  }
  if (status.type() != std::filesystem::file_type::symlink) {
    *resolved = path;
    return true;
  }

  const auto link_target = std::filesystem::read_symlink(path, ec);
  if (ec) {
    if (error) {
      *error = "Failed to read symlink " + path.string() + ": " +
               ec.message();
    }
    return false;
  }

  return ResolveSymlinkPathImpl(
      ResolveRelativeSymlinkTarget(path, link_target), resolved, error,
      depth + 1);
}

}  // namespace

namespace vinput::file {

bool ResolveSymlinkPath(const std::filesystem::path& path,
                        std::filesystem::path* resolved,
                        std::string* error) {
  if (!resolved) {
    if (error) {
      *error = "ResolveSymlinkPath requires a destination path.";
    }
    return false;
  }
  return ResolveSymlinkPathImpl(path, resolved, error, 0);
}

bool EnsureParentDirectory(const std::filesystem::path& path, std::string* error) {
  auto parent = path.parent_path();
  if (parent.empty() || std::filesystem::exists(parent)) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(parent, ec);
  if (ec) {
    if (error) *error = "Failed to create directory " + parent.string() + ": " + ec.message();
    return false;
  }
  return true;
}

bool ReadTextFile(const std::filesystem::path& path, std::string* content,
                  std::string* error) {
  if (!content) {
    if (error) {
      *error = "ReadTextFile requires a destination buffer.";
    }
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    if (error) {
      *error = "Failed to open file " + path.string();
    }
    return false;
  }

  content->assign(std::istreambuf_iterator<char>(file),
                  std::istreambuf_iterator<char>());
  if (!file.good() && !file.eof()) {
    if (error) {
      *error = "Failed to read file " + path.string();
    }
    return false;
  }

  if (error) {
    error->clear();
  }
  return true;
}

bool AtomicWriteTextFile(const std::filesystem::path& target, std::string_view content, std::string* error) {
  std::filesystem::path resolved_target;
  if (!ResolveSymlinkPath(target, &resolved_target, error)) {
    return false;
  }

  if (!EnsureParentDirectory(resolved_target, error)) {
    return false;
  }

#ifdef _WIN32
  const auto tmp_path =
      resolved_target.parent_path() /
      (resolved_target.filename().string() + ".tmp");
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      if (error) {
        *error = "Failed to open temporary file " + tmp_path.string();
      }
      return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good()) {
      if (error) {
        *error = "Failed to write temporary file " + tmp_path.string();
      }
      out.close();
      std::error_code cleanup_ec;
      std::filesystem::remove(tmp_path, cleanup_ec);
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::remove(resolved_target, ec);
  ec.clear();
  std::filesystem::rename(tmp_path, resolved_target, ec);
  if (ec) {
    if (error) {
      *error = "rename failed: " + ec.message();
    }
    std::error_code cleanup_ec;
    std::filesystem::remove(tmp_path, cleanup_ec);
    return false;
  }
  return true;
#else
  auto tmp_path = resolved_target.parent_path() /
                  (resolved_target.filename().string() + ".tmp.XXXXXX");
  std::string tmp_str = tmp_path.string();

  int fd = mkstemp(tmp_str.data());
  if (fd < 0) {
    if (error) *error = std::string("mkstemp failed: ") + std::strerror(errno);
    return false;
  }

  // Write content
  const char* data = content.data();
  size_t remaining = content.size();
  while (remaining > 0) {
    ssize_t written = write(fd, data, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      if (error) *error = std::string("write failed: ") + std::strerror(errno);
      close(fd);
      unlink(tmp_str.c_str());
      return false;
    }
    data += written;
    remaining -= static_cast<size_t>(written);
  }

  if (fsync(fd) != 0) {
    if (error) *error = std::string("fsync failed: ") + std::strerror(errno);
    close(fd);
    unlink(tmp_str.c_str());
    return false;
  }

  close(fd);

  std::error_code ec;
  std::filesystem::rename(tmp_str, resolved_target, ec);
  if (ec) {
    if (error) *error = "rename failed: " + ec.message();
    unlink(tmp_str.c_str());
    return false;
  }

  return true;
#endif
}

}  // namespace vinput::file
