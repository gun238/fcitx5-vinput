#pragma once

#include <QMainWindow>

class QCloseEvent;
class QSystemTrayIcon;
class QTabWidget;

namespace vinput::gui {
class HotwordPage;
class ResourcePage;
}

namespace vinput::windows_app {

class GlobalHotkey;
class VoiceInputController;
class WindowsControlPage;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void onSaveClicked();
  void onOpenConfigClicked();
  void showWindow();
  void updateTrayMenu();

private:
  void createTrayIcon();
  void reloadPages();

  VoiceInputController *controller_ = nullptr;
  GlobalHotkey *hotkey_ = nullptr;
  WindowsControlPage *controlPage_ = nullptr;
  vinput::gui::ResourcePage *resourcePage_ = nullptr;
  vinput::gui::HotwordPage *hotwordPage_ = nullptr;
  QTabWidget *tabWidget_ = nullptr;
  QSystemTrayIcon *trayIcon_ = nullptr;
};

} // namespace vinput::windows_app
