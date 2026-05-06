#include "gui/utils/runtime_bridge.h"

#include <mutex>
#include <utility>

namespace vinput::gui {

namespace {

std::mutex &BridgeMutex() {
  static std::mutex mutex;
  return mutex;
}

ReloadAsrBackendHandler &ReloadHandler() {
  static ReloadAsrBackendHandler handler;
  return handler;
}

} // namespace

void SetReloadAsrBackendHandler(ReloadAsrBackendHandler handler) {
  std::lock_guard<std::mutex> lock(BridgeMutex());
  ReloadHandler() = std::move(handler);
}

bool ReloadAsrBackend(std::string *error) {
  ReloadAsrBackendHandler handler;
  {
    std::lock_guard<std::mutex> lock(BridgeMutex());
    handler = ReloadHandler();
  }

  if (!handler) {
    if (error) {
      error->clear();
    }
    return true;
  }
  return handler(error);
}

} // namespace vinput::gui
