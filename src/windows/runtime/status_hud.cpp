#include "runtime/status_hud.h"

#include "config.h"

#include <QBoxLayout>
#include <QCursor>
#include <QGuiApplication>
#include <QLabel>
#include <QMovie>
#include <QPainter>
#include <QScreen>

#include <filesystem>

namespace vinput::windows_app {

class HudBubbleWidget : public QWidget {
public:
  explicit HudBubbleWidget(QWidget *parent = nullptr) : QWidget(parent) {
    setFixedSize(72, 36);
  }

  void setText(const QString &text) {
    text_ = text;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRect bubble_rect(12, 2, width() - 14, height() - 4);
    const QPolygon tail({QPoint(2, 18), QPoint(12, 12), QPoint(12, 24)});

    painter.setPen(QPen(QColor(5, 5, 5), 3));
    painter.setBrush(QColor(255, 255, 255, 250));
    painter.drawRoundedRect(bubble_rect, 6, 6);
    painter.drawPolygon(tail);

    painter.setPen(QColor(12, 12, 12));
    painter.drawText(bubble_rect, Qt::AlignCenter, text_);
  }

private:
  QString text_;
};

namespace {

QString HudGifPath() {
  return QString::fromStdString(
      std::filesystem::path(VINPUT_HUD_GIF_SOURCE_PATH).string());
}

} // namespace

StatusHud::StatusHud(QWidget *parent) : QWidget(parent) {
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                 Qt::WindowDoesNotAcceptFocus | Qt::BypassWindowManagerHint);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_TransparentForMouseEvents);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  imageLabel_ = new QLabel(this);
  movie_ = new QMovie(HudGifPath(), QByteArray(), this);
  imageLabel_->setMovie(movie_);
  imageLabel_->setFixedSize(84, 84);
  movie_->start();

  bubble_ = new HudBubbleWidget(this);

  layout->addWidget(imageLabel_);
  layout->addWidget(bubble_);
  hide();
}

void StatusHud::showState(const QString &state_text) {
  bubble_->setText(state_text);
  adjustSize();

  const QPoint cursor = QCursor::pos();
  QScreen *screen = QGuiApplication::screenAt(cursor);
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);

  const int x = qMin(area.right() - width(), qMax(area.left(), cursor.x() + 14));
  const int y = qMin(area.bottom() - height(), qMax(area.top(), cursor.y() + 18));
  move(x, y);
  show();
}

void StatusHud::hideHud() { hide(); }

} // namespace vinput::windows_app
