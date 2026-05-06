#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace vinput::windows_app {

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT

public:
  explicit GlobalHotkey(QObject *parent = nullptr);
  ~GlobalHotkey() override;

  QString hotkeyText() const;
  void handleRightAltEvent(bool pressed);
  bool nativeEventFilter(const QByteArray &event_type, void *message,
                         qintptr *result) override;

  static GlobalHotkey *instance();

signals:
  void activated();
  void released();

private:
  void *hook_ = nullptr;
  bool right_alt_down_ = false;
  bool registered_ = false;
};

} // namespace vinput::windows_app
