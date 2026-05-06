#pragma once

#include "common/config/core_config.h"
#include "daemon/postprocess/post_processor.h"
#include "daemon/runtime/recognition_pipeline.h"

#include <QObject>
#include <QString>

#include <memory>
#include <mutex>

namespace vinput::daemon::asr {
class AsrBackend;
}

namespace vinput::windows_app {

class AudioRecorder;
class StatusHud;

class VoiceInputController : public QObject {
  Q_OBJECT

public:
  explicit VoiceInputController(QObject *parent = nullptr);
  ~VoiceInputController() override;

  bool initialize(QString *error = nullptr);
  bool reloadAsrBackend(std::string *error = nullptr);
  void toggleRecording();
  void beginPressToTalk();
  void endPressToTalk();
  bool isRecording() const;
  bool isBusy() const;
  bool isConfigurationComplete(QString *reason = nullptr) const;
  QString statusText() const;

signals:
  void recordingStateChanged(bool recording);
  void busyStateChanged(bool busy);
  void statusTextChanged(const QString &status_text);
  void errorOccurred(const QString &message);
  void commitSucceeded(const QString &text);

private:
  void setRecording(bool recording);
  void setBusy(bool busy);
  void setStatusText(const QString &status_text);
  bool ensureLocalBackend(const CoreConfig &settings, std::string *error);
  void startRecording();
  void stopRecording();

  AudioRecorder *recorder_ = nullptr;
  StatusHud *hud_ = nullptr;
  bool initialized_ = false;
  bool recording_ = false;
  bool busy_ = false;
  QString status_text_;
  mutable std::mutex backend_mutex_;
  std::unique_ptr<vinput::daemon::asr::AsrBackend> local_backend_;
  std::string local_provider_id_;
  std::string local_model_id_;
  PostProcessor post_processor_;
  vinput::daemon::runtime::RecognitionPipeline pipeline_;
};

} // namespace vinput::windows_app
