#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace vinput::windows_app {

class VoiceInputController;

class WindowsControlPage : public QWidget {
  Q_OBJECT

public:
  explicit WindowsControlPage(VoiceInputController *controller,
                              QWidget *parent = nullptr);

  QString currentDeviceId() const;
  QString selectedProviderId() const;
  QString doubaoApiKeyPath() const;
  void reload();
  void setHotkeyLabel(const QString &hotkey_text);

private slots:
  void updateRecordButton();
  void reloadBackend();

private:
  void populateDevices();
  void populateProviders();

  VoiceInputController *controller_ = nullptr;
  QLabel *statusValue_ = nullptr;
  QLabel *providerValue_ = nullptr;
  QLabel *modelValue_ = nullptr;
  QLabel *hotkeyValue_ = nullptr;
  QComboBox *providerCombo_ = nullptr;
  QLineEdit *doubaoApiKeyPathEdit_ = nullptr;
  QComboBox *deviceCombo_ = nullptr;
  QPushButton *recordButton_ = nullptr;
  QPushButton *reloadButton_ = nullptr;
};

} // namespace vinput::windows_app
