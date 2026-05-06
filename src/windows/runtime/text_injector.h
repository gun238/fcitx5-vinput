#pragma once

#include <QString>

namespace vinput::windows_app {

class TextInjector {
public:
  static bool insertText(const QString &text, QString *error);
};

} // namespace vinput::windows_app
