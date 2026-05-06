#include "runtime/text_injector.h"

#include <QObject>
#include <vector>
#include <windows.h>

namespace vinput::windows_app {

bool TextInjector::insertText(const QString &text, QString *error) {
  std::vector<INPUT> inputs;
  inputs.reserve(static_cast<std::size_t>(text.size()) * 2);

  for (const QChar original : text) {
    const QChar ch = (original == u'\n') ? QChar(u'\r') : original;
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = ch.unicode();
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs.push_back(up);
  }

  if (inputs.empty()) {
    if (error) {
      error->clear();
    }
    return true;
  }

  const UINT sent =
      SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  if (sent != inputs.size()) {
    if (error) {
      *error = QObject::tr("Windows text injection failed.");
    }
    return false;
  }

  if (error) {
    error->clear();
  }
  return true;
}

} // namespace vinput::windows_app
