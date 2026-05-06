#include "runtime/voice_input_controller.h"

#include "common/config/core_config.h"
#include "common/dbus/error_info.h"
#include "common/utils/path_utils.h"
#include "daemon/asr/runtime/backend_factory.h"
#include "daemon/asr/runtime/recognition_session_manager.h"
#include "gui/utils/config_manager.h"
#include "runtime/audio_recorder.h"
#include "runtime/doubao_asr_client.h"
#include "runtime/status_hud.h"
#include "runtime/text_corrector.h"
#include "runtime/text_injector.h"

#include <QMetaObject>
#include <QFileInfo>
#include <QThreadPool>

#include <filesystem>
#include <span>

namespace vinput::windows_app {
namespace {

const DoubaoAsrProvider *ActiveDoubaoProvider(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  return provider ? std::get_if<DoubaoAsrProvider>(provider) : nullptr;
}

const LocalAsrProvider *ActiveLocalProvider(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  return provider ? std::get_if<LocalAsrProvider>(provider) : nullptr;
}

bool IsConfigComplete(const CoreConfig &config, QString *reason) {
  if (const auto *doubao = ActiveDoubaoProvider(config)) {
    if (doubao->apiKeyPath.empty()) {
      if (reason) {
        *reason = QObject::tr("Please configure the Doubao API key file.");
      }
      return false;
    }
    if (!QFileInfo::exists(QString::fromStdString(doubao->apiKeyPath))) {
      if (reason) {
        *reason = QObject::tr("Doubao API key file does not exist: %1")
                      .arg(QString::fromStdString(doubao->apiKeyPath));
      }
      return false;
    }
    return true;
  }

  if (const auto *local = ActiveLocalProvider(config)) {
    if (local->model.empty()) {
      if (reason) {
        *reason = QObject::tr(
            "Please choose or download a local ASR model before recording.");
      }
      return false;
    }
    return true;
  }

  if (reason) {
    *reason = QObject::tr("Please choose an ASR mode: local model or Doubao.");
  }
  return false;
}

DoubaoAsrResult LocalResultFromRun(
    const vinput::daemon::asr::RecognitionRunResult &run) {
  DoubaoAsrResult result;
  result.ok = run.ok;
  result.text = run.text;
  result.error = run.error;
  return result;
}

} // namespace

VoiceInputController::VoiceInputController(QObject *parent)
    : QObject(parent),
      recorder_(new AudioRecorder(this)),
      hud_(new StatusHud()),
      pipeline_(&post_processor_) {
  setStatusText(tr("Idle"));
}

VoiceInputController::~VoiceInputController() {
  hud_->hideHud();
  {
    std::lock_guard<std::mutex> lock(backend_mutex_);
    local_backend_.reset();
  }
  delete hud_;
}

bool VoiceInputController::initialize(QString *error) {
  std::string init_error;
  std::error_code ec;
  if (!std::filesystem::exists(vinput::path::CoreConfigPath(), ec) &&
      !InitializeCoreConfig(&init_error)) {
    if (error) {
      *error = QString::fromStdString(init_error);
    }
    return false;
  }

  initialized_ = true;
  QString config_reason;
  if (!isConfigurationComplete(&config_reason)) {
    setStatusText(tr("Configuration required"));
    if (error) {
      *error = config_reason;
    }
    return true;
  }

  std::string reload_error;
  if (!reloadAsrBackend(&reload_error)) {
    if (error) {
      *error = QString::fromStdString(reload_error);
    }
    return false;
  }
  if (error) {
    error->clear();
  }
  return true;
}

bool VoiceInputController::reloadAsrBackend(std::string *error) {
  const CoreConfig config = vinput::gui::ConfigManager::Get().Load();
  QString config_reason;
  if (!IsConfigComplete(config, &config_reason)) {
    std::lock_guard<std::mutex> lock(backend_mutex_);
    local_backend_.reset();
    local_provider_id_.clear();
    local_model_id_.clear();
    if (error) {
      *error = config_reason.toStdString();
    }
    setStatusText(tr("Configuration required"));
    return false;
  }

  if (ActiveDoubaoProvider(config)) {
    std::lock_guard<std::mutex> lock(backend_mutex_);
    local_backend_.reset();
    local_provider_id_.clear();
    local_model_id_.clear();
    if (error) {
      error->clear();
    }
    setStatusText(tr("Ready"));
    return true;
  }

  if (!ensureLocalBackend(config, error)) {
    setStatusText(tr("Configuration required"));
    return false;
  }
  if (error) {
    error->clear();
  }
  setStatusText(tr("Ready"));
  return true;
}

void VoiceInputController::toggleRecording() {
  if (busy_) {
    return;
  }
  if (recording_) {
    stopRecording();
  } else {
    startRecording();
  }
}

void VoiceInputController::beginPressToTalk() {
  if (busy_ || recording_) {
    return;
  }
  startRecording();
}

void VoiceInputController::endPressToTalk() {
  if (busy_ || !recording_) {
    return;
  }
  stopRecording();
}

bool VoiceInputController::isRecording() const { return recording_; }

bool VoiceInputController::isBusy() const { return busy_; }

bool VoiceInputController::isConfigurationComplete(QString *reason) const {
  return IsConfigComplete(vinput::gui::ConfigManager::Get().Load(), reason);
}

QString VoiceInputController::statusText() const { return status_text_; }

void VoiceInputController::setRecording(bool recording) {
  if (recording_ == recording) {
    return;
  }
  recording_ = recording;
  emit recordingStateChanged(recording_);
}

void VoiceInputController::setBusy(bool busy) {
  if (busy_ == busy) {
    return;
  }
  busy_ = busy;
  emit busyStateChanged(busy_);
}

void VoiceInputController::setStatusText(const QString &status_text) {
  if (status_text_ == status_text) {
    return;
  }
  status_text_ = status_text;
  emit statusTextChanged(status_text_);
}

bool VoiceInputController::ensureLocalBackend(const CoreConfig &settings,
                                              std::string *error) {
  const auto *local = ActiveLocalProvider(settings);
  if (!local) {
    if (error) {
      *error = "Active ASR provider is not a local provider.";
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(backend_mutex_);
  if (local_backend_ && local_provider_id_ == settings.asr.activeProvider &&
      local_model_id_ == local->model) {
    if (error) {
      error->clear();
    }
    return true;
  }

  auto backend = vinput::daemon::asr::CreateBackend(settings, error);
  if (!backend) {
    return false;
  }
  local_backend_ = std::move(backend);
  local_provider_id_ = settings.asr.activeProvider;
  local_model_id_ = local->model;
  if (error) {
    error->clear();
  }
  return true;
}

void VoiceInputController::startRecording() {
  QString config_reason;
  if (!isConfigurationComplete(&config_reason)) {
    setStatusText(tr("Configuration required"));
    emit errorOccurred(config_reason);
    return;
  }

  QString error;
  const CoreConfig config = vinput::gui::ConfigManager::Get().Load();
  if (!recorder_->start(QString::fromStdString(config.global.captureDevice), &error)) {
    setStatusText(error);
    emit errorOccurred(error);
    return;
  }

  setRecording(true);
  setStatusText(tr("Listening"));
  hud_->showState(tr("listening"));
}

void VoiceInputController::stopRecording() {
  setRecording(false);
  setBusy(true);
  setStatusText(tr("Recognizing"));
  hud_->showState(tr("typing"));

  auto pcm = recorder_->stop();
  const CoreConfig settings = vinput::gui::ConfigManager::Get().Load();

  QThreadPool::globalInstance()->start([this, pcm = std::move(pcm), settings]() mutable {
    std::string commit_text;
    std::string pipeline_error;
    DoubaoAsrResult recognition_result;

    if (const auto *doubao = ActiveDoubaoProvider(settings)) {
      recognition_result = DoubaoAsrClient::recognize(
          pcm, QString::fromStdString(doubao->apiKeyPath));
    } else {
      std::lock_guard<std::mutex> lock(backend_mutex_);
      if (!local_backend_) {
        recognition_result.error = "Local ASR backend is not loaded.";
      } else {
        std::string session_error;
        auto session = local_backend_->CreateSession(&session_error);
        if (!session) {
          recognition_result.error = session_error;
        } else if (!session->PushAudio(std::span<const int16_t>(pcm.data(), pcm.size()),
                                       &session_error)) {
          recognition_result.error = session_error;
        } else {
          recognition_result = LocalResultFromRun(
              vinput::daemon::asr::RecognitionSessionManager::ConsumeEvents(
                  &session, false, &session_error));
        }
      }
    }
    if (!recognition_result.ok) {
      pipeline_error = recognition_result.error;
    } else if (!recognition_result.text.empty()) {
      const std::string corrected_text =
          correctRecognizedText(recognition_result.text);
      vinput::daemon::runtime::RecognitionOrder order;
      order.recognized_text =
          corrected_text.empty() ? recognition_result.text : corrected_text;
      order.scene_id = settings.scenes.activeScene;

      auto output = pipeline_.Process(order, settings);
      commit_text = output.payload.commitText.empty()
                        ? order.recognized_text
                        : output.payload.commitText;
      if (commit_text.empty() && !output.errors.empty()) {
        pipeline_error = !output.errors.front().raw_message.empty()
                             ? output.errors.front().raw_message
                             : output.errors.front().detail;
      }
    }

    QMetaObject::invokeMethod(this, [this, commit_text, pipeline_error]() {
      hud_->hideHud();
      setBusy(false);

      const std::string error_text = pipeline_error;
      if (!error_text.empty()) {
        setStatusText(QString::fromStdString(error_text));
        emit errorOccurred(QString::fromStdString(error_text));
        return;
      }

      if (commit_text.empty()) {
        setStatusText(tr("No speech recognized"));
        emit errorOccurred(tr("No speech was recognized from the last capture."));
        return;
      }

      QString injection_error;
      if (!TextInjector::insertText(QString::fromStdString(commit_text),
                                    &injection_error)) {
        setStatusText(injection_error);
        emit errorOccurred(injection_error);
        return;
      }

      setStatusText(tr("Inserted"));
      emit commitSucceeded(QString::fromStdString(commit_text));
    });
  });
}

} // namespace vinput::windows_app
