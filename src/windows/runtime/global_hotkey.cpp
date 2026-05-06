#include "runtime/global_hotkey.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <windows.h>

namespace vinput::windows_app {

namespace {

GlobalHotkey *g_instance = nullptr;

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
  if (code == HC_ACTION && GlobalHotkey::instance()) {
    const auto *event = reinterpret_cast<KBDLLHOOKSTRUCT *>(lparam);
    if (event && event->vkCode == VK_RMENU) {
      if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) {
        GlobalHotkey::instance()->handleRightAltEvent(true);
      } else if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP) {
        GlobalHotkey::instance()->handleRightAltEvent(false);
      }
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

} // namespace

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent) {
  g_instance = this;
  QCoreApplication::instance()->installNativeEventFilter(this);
  hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, nullptr, 0);
  registered_ = hook_ != nullptr;
}

GlobalHotkey::~GlobalHotkey() {
  if (hook_) {
    UnhookWindowsHookEx(static_cast<HHOOK>(hook_));
    hook_ = nullptr;
  }
  if (QCoreApplication::instance()) {
    QCoreApplication::instance()->removeNativeEventFilter(this);
  }
  if (g_instance == this) {
    g_instance = nullptr;
  }
}

QString GlobalHotkey::hotkeyText() const { return tr("Right Alt"); }

GlobalHotkey *GlobalHotkey::instance() { return g_instance; }

void GlobalHotkey::handleRightAltEvent(bool pressed) {
  if (pressed) {
    if (right_alt_down_) {
      return;
    }
    right_alt_down_ = true;
    QMetaObject::invokeMethod(this, [this]() { emit activated(); },
                              Qt::QueuedConnection);
    return;
  }
  if (right_alt_down_) {
    QMetaObject::invokeMethod(this, [this]() { emit released(); },
                              Qt::QueuedConnection);
  }
  right_alt_down_ = false;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &event_type, void *message,
                                     qintptr *result) {
  (void)event_type;
  (void)message;
  (void)result;
  return false;
}

} // namespace vinput::windows_app
