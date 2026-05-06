#pragma once

#include <functional>
#include <string>

namespace vinput::gui {

using ReloadAsrBackendHandler = std::function<bool(std::string *error)>;

void SetReloadAsrBackendHandler(ReloadAsrBackendHandler handler);
bool ReloadAsrBackend(std::string *error = nullptr);

} // namespace vinput::gui
