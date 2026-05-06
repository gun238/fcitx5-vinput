#include "config.h"

#include "app/mainwindow.h"

#include "app/windows_control_page.h"
#include "common/config/core_config.h"
#include "common/utils/path_utils.h"
#include "gui/pages/hotwords/hotword_page.h"
#include "gui/pages/resources/resource_page.h"
#include "gui/utils/config_manager.h"
#include "gui/utils/i18n_cache.h"
#include "gui/utils/runtime_bridge.h"
#include "runtime/global_hotkey.h"
#include "runtime/voice_input_controller.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QApplication>

#include <filesystem>

namespace vinput::windows_app {
namespace {

constexpr const char *kDoubaoProviderId = "doubao";
constexpr const char *kLocalProviderId = "sherpa-onnx";
constexpr const char *kDefaultDoubaoKeyPath =
    "C:/Users/Administrator/Documents/apikeys/doubao.txt";

void EnsureWindowsAsrProviders(CoreConfig *config) {
  if (!config) {
    return;
  }

  bool has_local = false;
  bool has_doubao = false;
  for (const auto &provider : config->asr.providers) {
    has_local = has_local || std::holds_alternative<LocalAsrProvider>(provider);
    has_doubao = has_doubao || std::holds_alternative<DoubaoAsrProvider>(provider);
  }

  if (!has_local) {
    LocalAsrProvider local;
    local.id = kLocalProviderId;
    local.timeoutMs = 15000;
    config->asr.providers.push_back(std::move(local));
  }
  if (!has_doubao) {
    DoubaoAsrProvider doubao;
    doubao.id = kDoubaoProviderId;
    doubao.apiKeyPath = kDefaultDoubaoKeyPath;
    doubao.timeoutMs = 70000;
    config->asr.providers.push_back(std::move(doubao));
  }
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(tr("Vinput for Windows"));

  controller_ = new VoiceInputController(this);
  hotkey_ = new GlobalHotkey(this);

  QString init_error;
  if (!controller_->initialize(&init_error) && !init_error.isEmpty()) {
    QMessageBox::warning(this, tr("ASR Backend"),
                         tr("Windows runtime initialized with a warning:\n%1")
                             .arg(init_error));
  }

  {
    CoreConfig config = vinput::gui::ConfigManager::Get().Load();
    const std::size_t provider_count = config.asr.providers.size();
    EnsureWindowsAsrProviders(&config);
    if (config.asr.providers.size() != provider_count) {
      vinput::gui::ConfigManager::Get().Save(config);
    }
  }

  vinput::gui::SetReloadAsrBackendHandler(
      [this](std::string *error) { return controller_->reloadAsrBackend(error); });
  vinput::gui::I18nCache::Get().Initialize(vinput::gui::ConfigManager::Get().Load());

  auto *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);
  auto *mainLayout = new QVBoxLayout(centralWidget);

  tabWidget_ = new QTabWidget(this);
  mainLayout->addWidget(tabWidget_);

  controlPage_ = new WindowsControlPage(controller_, this);
  controlPage_->setHotkeyLabel(hotkey_->hotkeyText());
  resourcePage_ = new vinput::gui::ResourcePage(this);
  hotwordPage_ = new vinput::gui::HotwordPage(this);

  tabWidget_->addTab(controlPage_, tr("Control"));
  tabWidget_->addTab(resourcePage_, tr("Resources"));
  tabWidget_->addTab(hotwordPage_, tr("Hotwords"));

  auto *bottomLayout = new QHBoxLayout();
  auto *hintLabel =
      new QLabel(tr("Hold %1 anywhere to record; release it to recognize and type.")
                     .arg(hotkey_->hotkeyText()),
                 this);
  auto *openConfigButton = new QPushButton(tr("Open Config"), this);
  auto *saveButton = new QPushButton(tr("Save Settings"), this);
  bottomLayout->addWidget(hintLabel);
  bottomLayout->addStretch();
  bottomLayout->addWidget(openConfigButton);
  bottomLayout->addWidget(saveButton);
  mainLayout->addLayout(bottomLayout);

  connect(openConfigButton, &QPushButton::clicked, this,
          &MainWindow::onOpenConfigClicked);
  connect(saveButton, &QPushButton::clicked, this, &MainWindow::onSaveClicked);
  connect(hotkey_, &GlobalHotkey::activated, controller_,
          &VoiceInputController::beginPressToTalk);
  connect(hotkey_, &GlobalHotkey::released, controller_,
          &VoiceInputController::endPressToTalk);
  connect(controller_, &VoiceInputController::errorOccurred, this,
          [this](const QString &message) {
            if (trayIcon_) {
              trayIcon_->showMessage(tr("Vinput"), message,
                                     QSystemTrayIcon::Warning);
            }
          });
  connect(resourcePage_, &vinput::gui::ResourcePage::configChanged, this,
          &MainWindow::reloadPages);
  connect(&vinput::gui::ConfigManager::Get(),
          &vinput::gui::ConfigManager::configChanged, this,
          &MainWindow::reloadPages);

  createTrayIcon();
  reloadPages();

  QTimer::singleShot(0, this, [this]() {
    QString reason;
    if (!controller_->isConfigurationComplete(&reason)) {
      showWindow();
      QMessageBox::information(
          this, tr("Vinput Setup Required"),
          tr("Please finish ASR setup before using voice input.\n\n%1")
              .arg(reason));
    }
  });
}

MainWindow::~MainWindow() {
  vinput::gui::SetReloadAsrBackendHandler({});
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (trayIcon_ && trayIcon_->isVisible()) {
    hide();
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
}

void MainWindow::onSaveClicked() {
  CoreConfig config = vinput::gui::ConfigManager::Get().Load();
  EnsureWindowsAsrProviders(&config);

  config.asr.activeProvider = controlPage_->selectedProviderId().toStdString();
  const QString device_key = controlPage_->currentDeviceId();
  config.global.captureDevice = device_key.toStdString();
  for (auto &provider : config.asr.providers) {
    if (auto *doubao = std::get_if<DoubaoAsrProvider>(&provider)) {
      doubao->apiKeyPath = controlPage_->doubaoApiKeyPath().toStdString();
    }
  }

  const QString hotwordsFile = hotwordPage_->hotwordsFilePath();
  for (auto &provider : config.asr.providers) {
    if (auto *local = std::get_if<LocalAsrProvider>(&provider)) {
      local->hotwordsFile = hotwordsFile.toStdString();
    }
  }

  if (!hotwordsFile.isEmpty()) {
    QFile file(hotwordsFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream(&file) << hotwordPage_->hotwordsContent();
    }
  }

  if (config.asr.activeProvider.empty()) {
    QMessageBox::warning(this, tr("Setup Required"),
                         tr("Please choose an ASR mode before saving."));
    return;
  }
  const AsrProvider *active_provider = ResolveActiveAsrProvider(config);
  if (const auto *doubao =
          active_provider ? std::get_if<DoubaoAsrProvider>(active_provider)
                          : nullptr) {
    if (doubao->apiKeyPath.empty() ||
        !QFileInfo::exists(QString::fromStdString(doubao->apiKeyPath))) {
      QMessageBox::warning(
          this, tr("Setup Required"),
          tr("Please choose an existing Doubao API key file before saving."));
      return;
    }
  }

  if (!vinput::gui::ConfigManager::Get().Save(config)) {
    QMessageBox::critical(this, tr("Error"), tr("Failed to save config."));
    return;
  }

  std::string error;
  if (!controller_->reloadAsrBackend(&error)) {
    QMessageBox::warning(
        this, tr("Warning"),
        tr("Settings were saved, but the ASR backend could not be reloaded:\n%1")
            .arg(QString::fromStdString(error)));
    return;
  }

  reloadPages();
  QMessageBox::information(this, tr("Saved"),
                           tr("Windows voice input settings have been updated."));
}

void MainWindow::onOpenConfigClicked() {
  QDesktopServices::openUrl(
      QUrl::fromLocalFile(QString::fromStdString(GetCoreConfigPath())));
}

void MainWindow::showWindow() {
  showNormal();
  raise();
  activateWindow();
}

void MainWindow::updateTrayMenu() {
  if (!trayIcon_ || !trayIcon_->contextMenu()) {
    return;
  }

  const auto actions = trayIcon_->contextMenu()->actions();
  if (actions.size() >= 2) {
    actions[1]->setText(controller_->isRecording() ? tr("Stop Recording")
                                                   : tr("Start Recording"));
  }
}

void MainWindow::createTrayIcon() {
  trayIcon_ = new QSystemTrayIcon(this);
  trayIcon_->setToolTip(tr("Vinput for Windows"));
  trayIcon_->setIcon(QIcon(QString::fromStdString(
      std::filesystem::path(VINPUT_APP_ICON_SOURCE_PATH).string())));

  auto *menu = new QMenu(this);
  auto *showAction = menu->addAction(tr("Show Window"));
  auto *toggleAction = menu->addAction(tr("Start Recording"));
  menu->addSeparator();
  auto *quitAction = menu->addAction(tr("Exit"));

  connect(showAction, &QAction::triggered, this, &MainWindow::showWindow);
  connect(toggleAction, &QAction::triggered, controller_,
          &VoiceInputController::toggleRecording);
  connect(quitAction, &QAction::triggered, this, [this]() {
    if (trayIcon_) {
      trayIcon_->hide();
    }
    qApp->quit();
  });
  connect(trayIcon_, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger ||
                reason == QSystemTrayIcon::DoubleClick) {
              showWindow();
            }
          });
  connect(controller_, &VoiceInputController::recordingStateChanged, this,
          &MainWindow::updateTrayMenu);

  trayIcon_->setContextMenu(menu);
  trayIcon_->show();
  updateTrayMenu();
}

void MainWindow::reloadPages() {
  vinput::gui::I18nCache::Get().Initialize(vinput::gui::ConfigManager::Get().Load());
  controlPage_->reload();
  resourcePage_->reload();
  hotwordPage_->reload();
  updateTrayMenu();
}

} // namespace vinput::windows_app
