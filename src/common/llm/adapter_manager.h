#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <sys/types.h>
#else
using pid_t = int;
#endif

struct CoreConfig;
struct LlmAdapter;
namespace vinput::process {
struct CommandSpec;
}

namespace vinput::adapter {

vinput::process::CommandSpec BuildCommandSpec(const LlmAdapter &adapter);
std::filesystem::path ResolveWorkingDir(const LlmAdapter &adapter);
std::filesystem::path PidPath(std::string_view adapter_id);
bool WritePidFile(std::string_view adapter_id, pid_t pid, std::string *error);
void RemovePidFile(std::string_view adapter_id);
bool IsRunning(std::string_view adapter_id);
bool Stop(std::string_view adapter_id, std::string *error);

}  // namespace vinput::adapter
