#include "common/utils/path_utils.h"
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace vinput::path {

namespace {

constexpr std::string_view kDaemonServiceUnitName = "vinput-daemon.service";
constexpr std::string_view kCliExecutableName = "vinput";
constexpr std::string_view kDaemonExecutableName = "vinput-daemon";

std::filesystem::path FlatpakAddonRootDir() {
  return std::filesystem::path("/app") / "addons" / "Vinput";
}

std::filesystem::path FlatpakBundledSystemdUnitPath(std::string_view unit_name) {
  return FlatpakAddonRootDir() / "share" / "systemd" / "user" /
         std::filesystem::path(unit_name);
}

std::filesystem::path FlatpakBundledExecutablePath(std::string_view name) {
  return FlatpakAddonRootDir() / "bin" / std::filesystem::path(name);
}

bool IsInsideFlatpak() {
#ifdef _WIN32
  return false;
#else
  struct stat st;
  return stat("/.flatpak-info", &st) == 0;
#endif
}

std::filesystem::path UserSystemdUnitPath(std::string_view unit_name) {
  return vinput::path::UserSystemdUnitDir() / std::filesystem::path(unit_name);
}

std::filesystem::path XdgConfigHome() {
#ifdef _WIN32
  const char *appdata = std::getenv("APPDATA");
  if (appdata && appdata[0] != '\0') {
    return std::filesystem::path(appdata);
  }
  const char *profile = std::getenv("USERPROFILE");
  if (profile && profile[0] != '\0') {
    return std::filesystem::path(profile) / "AppData" / "Roaming";
  }
  return {};
#else
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".config";
#endif
}

std::filesystem::path XdgDataHome() {
#ifdef _WIN32
  const char *local = std::getenv("LOCALAPPDATA");
  if (local && local[0] != '\0') {
    return std::filesystem::path(local);
  }
  const char *profile = std::getenv("USERPROFILE");
  if (profile && profile[0] != '\0') {
    return std::filesystem::path(profile) / "AppData" / "Local";
  }
  return {};
#else
  const char *xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".local" / "share";
#endif
}

std::filesystem::path XdgCacheHome() {
#ifdef _WIN32
  return XdgDataHome() / "Cache";
#else
  const char *xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".cache";
#endif
}

std::filesystem::path VinputConfigDir() { return XdgConfigHome() / "vinput"; }
std::filesystem::path VinputDataDir() { return XdgDataHome() / "vinput"; }
std::filesystem::path VinputCacheDir() { return XdgCacheHome() / "vinput"; }

} // namespace

std::string_view DaemonServiceUnitName() { return kDaemonServiceUnitName; }

std::string_view CliExecutableName() { return kCliExecutableName; }

std::filesystem::path CliExecutablePath() {
  if (IsInsideFlatpak()) {
    return FlatpakBundledExecutablePath(kCliExecutableName);
  }
  return std::filesystem::path(kCliExecutableName);
}

std::filesystem::path DaemonExecutablePath() {
  if (IsInsideFlatpak()) {
    return FlatpakBundledExecutablePath(kDaemonExecutableName);
  }
  return std::filesystem::path(kDaemonExecutableName);
}

std::filesystem::path DaemonServiceUnitInstallPath() {
  return UserSystemdUnitPath(DaemonServiceUnitName());
}

std::filesystem::path DaemonServiceUnitTemplatePath() {
  if (IsInsideFlatpak()) {
    return FlatpakBundledSystemdUnitPath(DaemonServiceUnitName());
  }
  return {};
}

std::filesystem::path ExpandUserPath(std::string_view path) {
#ifdef _WIN32
  if (path.empty() || path[0] != '~') {
    return std::filesystem::path(path);
  }
  const char *profile = std::getenv("USERPROFILE");
  if (!profile || profile[0] == '\0') {
    return {};
  }
  return std::filesystem::path(profile) /
         std::filesystem::path(
             path.substr(path.size() > 1 &&
                                 (path[1] == '/' || path[1] == '\\')
                             ? 2
                             : 1));
#else
  if (path.empty() || path[0] != '~') {
    return std::filesystem::path(path);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};
  return std::filesystem::path(home) /
         std::filesystem::path(
             path.substr(path.size() > 1 && path[1] == '/' ? 2 : 1));
#endif
}

std::filesystem::path DefaultModelBaseDir() {
  return VinputDataDir() / "models";
}

std::filesystem::path CoreConfigPath() {
  return VinputConfigDir() / "config.json";
}

std::filesystem::path FcitxAddonConfigPath() {
#ifdef _WIN32
  return CoreConfigPath();
#else
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "fcitx5" / "conf" / "vinput.conf";
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".config" / "fcitx5" / "conf" /
         "vinput.conf";
#endif
}

std::filesystem::path RegistryCacheDir() {
  return VinputCacheDir() / "registry";
}

std::filesystem::path UserSystemdUnitDir() {
  return XdgConfigHome() / "systemd" / "user";
}

std::filesystem::path ManagedResourceDir(std::string_view category) {
  return VinputDataDir() / std::filesystem::path(category);
}

std::filesystem::path ManagedAsrProviderDir() {
  return ManagedResourceDir("providers");
}

std::filesystem::path ManagedLlmAdapterDir() {
  return ManagedResourceDir("adapters");
}

std::filesystem::path AdapterRuntimeDir() {
#ifdef _WIN32
  return VinputCacheDir() / "adapters";
#else
  const char *xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime && xdg_runtime[0] != '\0') {
    return std::filesystem::path(xdg_runtime) / "vinput" / "adapters";
  }

  const char *tmpdir = std::getenv("TMPDIR");
  std::filesystem::path base =
      (tmpdir && tmpdir[0] != '\0') ? std::filesystem::path(tmpdir)
                                    : std::filesystem::path("/tmp");
  return base / "vinput" / "adapters";
#endif
}

std::filesystem::path ContextCachePath() {
  return VinputCacheDir() / "context.jsonl";
}

std::filesystem::path ReadNotificationsPath() {
  return VinputCacheDir() / "read_notifications";
}

} // namespace vinput::path
