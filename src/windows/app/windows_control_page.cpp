#include "app/windows_control_page.h"

#include "common/config/core_config.h"
#include "common/utils/path_utils.h"
#include "gui/utils/config_manager.h"
#include "runtime/audio_recorder.h"
#include "runtime/voice_input_controller.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace vinput::windows_app {

WindowsControlPage::WindowsControlPage(VoiceInputController *controller,
                                       QWidget *parent)
    : QWidget(parent), controller_(controller) {
  auto *layout = new QVBoxLayout(this);

  auto *summaryLabel = new QLabel(
      tr("This Windows build keeps the original cat status animation, but runs "
         "as a standalone voice typing tool instead of an Fcitx addon."),
      this);
  summaryLabel->setWordWrap(true);
  layout->addWidget(summaryLabel);

  auto *form = new QFormLayout();
  statusValue_ = new QLabel(tr("Idle"), this);
  providerValue_ = new QLabel(this);
  modelValue_ = new QLabel(this);
  hotkeyValue_ = new QLabel(this);
  providerCombo_ = new QComboBox(this);
  doubaoApiKeyPathEdit_ = new QLineEdit(this);
  deviceCombo_ = new QComboBox(this);

  form->addRow(tr("Status:"), statusValue_);
  form->addRow(tr("Trigger Hotkey:"), hotkeyValue_);
  form->addRow(tr("ASR Mode:"), providerCombo_);
  form->addRow(tr("Active Provider:"), providerValue_);
  form->addRow(tr("Active Model:"), modelValue_);
  auto *apiKeyRow = new QWidget(this);
  auto *apiKeyLayout = new QHBoxLayout(apiKeyRow);
  apiKeyLayout->setContentsMargins(0, 0, 0, 0);
  auto *browseApiKeyButton = new QPushButton(tr("Browse"), this);
  apiKeyLayout->addWidget(doubaoApiKeyPathEdit_);
  apiKeyLayout->addWidget(browseApiKeyButton);
  form->addRow(tr("Doubao API Key File:"), apiKeyRow);
  form->addRow(tr("Microphone:"), deviceCombo_);
  layout->addLayout(form);

  auto *buttonLayout = new QHBoxLayout();
  recordButton_ = new QPushButton(tr("Start Recording"), this);
  reloadButton_ = new QPushButton(tr("Reload Backend"), this);
  auto *modelsButton = new QPushButton(tr("Open Models Folder"), this);
  buttonLayout->addWidget(recordButton_);
  buttonLayout->addWidget(reloadButton_);
  buttonLayout->addWidget(modelsButton);
  buttonLayout->addStretch();
  layout->addLayout(buttonLayout);
  layout->addStretch();

  connect(recordButton_, &QPushButton::clicked, controller_,
          &VoiceInputController::toggleRecording);
  connect(reloadButton_, &QPushButton::clicked, this,
          &WindowsControlPage::reloadBackend);
  connect(modelsButton, &QPushButton::clicked, this, []() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(
        vinput::path::DefaultModelBaseDir().string())));
  });
  connect(browseApiKeyButton, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose Doubao API Key File"), doubaoApiKeyPathEdit_->text(),
        tr("Text files (*.txt);;All files (*.*)"));
    if (!path.isEmpty()) {
      doubaoApiKeyPathEdit_->setText(QDir::toNativeSeparators(path));
    }
  });
  connect(providerCombo_, &QComboBox::currentIndexChanged, this, [this]() {
    const bool doubao = selectedProviderId() == QStringLiteral("doubao");
    doubaoApiKeyPathEdit_->setEnabled(doubao);
  });
  connect(controller_, &VoiceInputController::recordingStateChanged, this,
          &WindowsControlPage::updateRecordButton);
  connect(controller_, &VoiceInputController::busyStateChanged, this,
          &WindowsControlPage::updateRecordButton);
  connect(controller_, &VoiceInputController::statusTextChanged, this,
          [this](const QString &status_text) { statusValue_->setText(status_text); });

  reload();
}

QString WindowsControlPage::currentDeviceId() const {
  return deviceCombo_->currentData().toString();
}

QString WindowsControlPage::selectedProviderId() const {
  return providerCombo_->currentData().toString();
}

QString WindowsControlPage::doubaoApiKeyPath() const {
  return QDir::fromNativeSeparators(doubaoApiKeyPathEdit_->text().trimmed());
}

void WindowsControlPage::reload() {
  populateProviders();
  populateDevices();

  const CoreConfig config = vinput::gui::ConfigManager::Get().Load();
  providerValue_->setText(QString::fromStdString(config.asr.activeProvider));
  modelValue_->setText(
      QString::fromStdString(ResolvePreferredLocalModel(config)));
  statusValue_->setText(controller_->statusText());
  updateRecordButton();
}

void WindowsControlPage::setHotkeyLabel(const QString &hotkey_text) {
  hotkeyValue_->setText(hotkey_text);
}

void WindowsControlPage::updateRecordButton() {
  if (controller_->isBusy()) {
    recordButton_->setText(tr("Recognizing..."));
    recordButton_->setEnabled(false);
    return;
  }
  recordButton_->setEnabled(true);
  recordButton_->setText(
      controller_->isRecording() ? tr("Stop Recording") : tr("Start Recording"));
}

void WindowsControlPage::reloadBackend() {
  std::string error;
  if (!controller_->reloadAsrBackend(&error)) {
    QMessageBox::warning(this, tr("Backend Reload"),
                         QString::fromStdString(error));
    return;
  }
  reload();
}

void WindowsControlPage::populateDevices() {
  const QString selected_key =
      QString::fromStdString(vinput::gui::ConfigManager::Get().Load().global.captureDevice);

  deviceCombo_->clear();
  for (const auto &device : AudioRecorder::availableInputDevices()) {
    const QString key = AudioRecorder::deviceKey(device);
    deviceCombo_->addItem(device.description(), key);
  }

  if (!selected_key.isEmpty()) {
    const int index = deviceCombo_->findData(selected_key);
    if (index >= 0) {
      deviceCombo_->setCurrentIndex(index);
    }
  }
}

void WindowsControlPage::populateProviders() {
  const CoreConfig config = vinput::gui::ConfigManager::Get().Load();

  providerCombo_->clear();
  for (const auto &provider : config.asr.providers) {
    const QString id = QString::fromStdString(AsrProviderId(provider));
    QString label = id;
    if (std::holds_alternative<LocalAsrProvider>(provider)) {
      label = tr("Local model (%1)").arg(id);
    } else if (std::holds_alternative<DoubaoAsrProvider>(provider)) {
      label = tr("Doubao ASR (%1)").arg(id);
    }
    providerCombo_->addItem(label, id);
  }

  int index = providerCombo_->findData(QString::fromStdString(config.asr.activeProvider));
  if (index < 0) {
    index = providerCombo_->findData(QStringLiteral("doubao"));
  }
  if (index >= 0) {
    providerCombo_->setCurrentIndex(index);
  }

  QString api_key_path = QStringLiteral("C:/Users/Administrator/Documents/apikeys/doubao.txt");
  for (const auto &provider : config.asr.providers) {
    if (const auto *doubao = std::get_if<DoubaoAsrProvider>(&provider)) {
      if (!doubao->apiKeyPath.empty()) {
        api_key_path = QString::fromStdString(doubao->apiKeyPath);
      }
    }
  }
  doubaoApiKeyPathEdit_->setText(QDir::toNativeSeparators(api_key_path));
  doubaoApiKeyPathEdit_->setEnabled(selectedProviderId() == QStringLiteral("doubao"));
}

} // namespace vinput::windows_app
