#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
using pid_t = int;
#else
#include <sys/types.h>
#endif

namespace vinput::process {

struct CommandSpec {
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  int timeout_ms = 0;
};

struct CommandResult {
  int exit_code = -1;
  bool timed_out = false;
  bool launch_failed = false;
  std::string stdout_text;
  std::string stderr_text;
};

struct SpawnedProcess {
  pid_t pid = -1;
  int stderr_fd = -1;
};

CommandResult RunCommandWithInput(const CommandSpec &spec,
                                  std::span<const std::byte> input);
bool SpawnDetached(const CommandSpec &spec,
                   const std::filesystem::path &working_dir, pid_t *pid_out,
                   std::string *error);
bool SpawnForMonitoring(const CommandSpec &spec,
                        const std::filesystem::path &working_dir,
                        SpawnedProcess *process_out, std::string *error);

}  // namespace vinput::process
