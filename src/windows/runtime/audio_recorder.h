#pragma once

#include <QObject>
#include <QByteArray>
#include <QAudioFormat>
#include <QList>
#include <QString>

#include <cstdint>
#include <vector>

#include <QAudioDevice>

class QAudioSource;
class QIODevice;

namespace vinput::windows_app {

class AudioRecorder : public QObject {
  Q_OBJECT

public:
  explicit AudioRecorder(QObject *parent = nullptr);
  ~AudioRecorder() override;

  bool start(const QString &device_key, QString *error);
  std::vector<int16_t> stop();
  bool isRecording() const;

  static QList<QAudioDevice> availableInputDevices();
  static QString deviceKey(const QAudioDevice &device);

private:
  void reset();
  std::vector<int16_t> convertToAsrPcm() const;

  QAudioSource *source_ = nullptr;
  QIODevice *device_ = nullptr;
  QByteArray pcm_bytes_;
  QAudioFormat capture_format_;
};

} // namespace vinput::windows_app
