#pragma once

#include <QWidget>

class QLabel;
class QMovie;

namespace vinput::windows_app {

class HudBubbleWidget;

class StatusHud : public QWidget {
  Q_OBJECT

public:
  explicit StatusHud(QWidget *parent = nullptr);

  void showState(const QString &state_text);
  void hideHud();

private:
  QLabel *imageLabel_ = nullptr;
  QMovie *movie_ = nullptr;
  HudBubbleWidget *bubble_ = nullptr;
};

} // namespace vinput::windows_app
