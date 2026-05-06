#include "app/mainwindow.h"

#include "common/i18n.h"

#include <QApplication>

int main(int argc, char *argv[]) {
  vinput::i18n::Init();
  QApplication::setDesktopSettingsAware(true);
  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);

  vinput::windows_app::MainWindow window;
  window.resize(820, 620);
  window.show();

  return app.exec();
}
