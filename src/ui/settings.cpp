#include "core/core.h"
#include "library/database.h"
#include "models/model_files.h"
#include "ui/ui_helpers.h"
#include "ui/window.h"
namespace piclocate {
void Window::settingsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("PicLocate settings"));
    dialog.setMinimumSize(480, 420);
    dialog.resize(600, 620);
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(24, 24, 24, 20);
    outer->setSpacing(16);
    outer->addWidget(label(QStringLiteral("Settings"), QStringLiteral("PageTitle")));
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *body = new QWidget;
    auto *l = new QVBoxLayout(body);
    l->setContentsMargins(0, 0, 10, 0);
    l->setSpacing(12);
    l->addWidget(label(QStringLiteral("Search"), QStringLiteral("SectionLabel")));
    const bool modelsReady = modelsPresent(paths_.models());
    auto *model = label(modelsReady ? QStringLiteral("Models installed · ready to search offline")
                                    : QStringLiteral("Download models to enable visual search."));
    model->setWordWrap(true);
    l->addWidget(model);
    auto *modelInfo =
        label(QStringLiteral(
                  "Models download from Hugging Face. Images and queries stay on this device."),
              QStringLiteral("Muted"));
    modelInfo->setWordWrap(true);
    l->addWidget(modelInfo);
    auto *download =
        button(modelsReady ? QStringLiteral("Verify models") : QStringLiteral("Download models"),
               QStringLiteral("spark"));
    download->setEnabled(!download_.running() && !indexing_);
    l->addWidget(download);
    auto *bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->hide();
    l->addWidget(bar);
    auto *cancelDownload = button(QStringLiteral("Cancel download"));
    cancelDownload->hide();
    l->addWidget(cancelDownload);
    connect(download, &QPushButton::clicked, &dialog, [this, bar, download, cancelDownload] {
        download->setEnabled(false);
        bar->show();
        cancelDownload->show();
        download_.start(paths_.models());
    });
    connect(cancelDownload, &QPushButton::clicked, &download_, &ModelDownload::cancel);
    connect(&download_, &ModelDownload::progress, &dialog,
            [bar, model](qint64 done, qint64 total, const QString &name) {
                bar->setValue(total ? int(done * 1000 / total) : 0);
                model->setText(
                    QStringLiteral("%1 / %2 · %3").arg(fileSize(done), fileSize(total), name));
            });
    connect(&download_, &ModelDownload::finished, &dialog,
            [this, model, download, bar, cancelDownload](bool ok, const QString &message) {
                model->setText(message);
                download->setEnabled(true);
                bar->hide();
                cancelDownload->hide();
                refreshStats();
                if (ok) {
                    requestSearch(true);
                    scan();
                }
            });
    auto *gpu = new QCheckBox(QStringLiteral("Use GPU when available"));
    gpu->setToolTip(QStringLiteral(
        "Speeds up indexing on supported Windows GPUs, with automatic CPU fallback."));
    gpu->setChecked(settings_.value(QStringLiteral("library/gpu"), true).toBool());
#ifndef Q_OS_WIN
    gpu->setChecked(false);
    gpu->setEnabled(false);
    gpu->setText(QStringLiteral("GPU indexing is available on Windows"));
#endif
    l->addWidget(gpu);
    auto *ocr = new QCheckBox(QStringLiteral("Read text inside images"));
    ocr->setToolTip(QStringLiteral("Use Windows text recognition while indexing."));
    ocr->setChecked(settings_.value(QStringLiteral("library/ocr"), true).toBool());
#ifndef Q_OS_WIN
    ocr->setChecked(false);
    ocr->setEnabled(false);
    ocr->setText(QStringLiteral("Text recognition is available on Windows"));
#endif
    l->addWidget(ocr);
    auto *autoScan = new QCheckBox(QStringLiteral("Check folders for changes"));
    autoScan->setObjectName(QStringLiteral("AutoScan"));
    autoScan->setToolTip(
        QStringLiteral("Check connected folders once a minute. Unchanged images are skipped."));
    autoScan->setChecked(settings_.value(QStringLiteral("library/autoScan"), false).toBool());
    l->addWidget(autoScan);
    const auto saveSettings = [this, gpu, ocr, autoScan](const QCheckBox *autoUpdates) {
        settings_.setValue(QStringLiteral("library/gpu"), gpu->isChecked());
        settings_.setValue(QStringLiteral("library/ocr"), ocr->isChecked());
        settings_.setValue(QStringLiteral("library/autoScan"), autoScan->isChecked());
        if (autoUpdates)
            settings_.setValue(QStringLiteral("updates/automatic"), autoUpdates->isChecked());
        settings_.sync();
    };
    QCheckBox *autoUpdates = nullptr;
    if (updater_.enabled()) {
        l->addSpacing(12);
        l->addWidget(label(QStringLiteral("Updates"), QStringLiteral("SectionLabel")));
        autoUpdates = new QCheckBox(QStringLiteral("Check for updates automatically"));
        autoUpdates->setObjectName(QStringLiteral("AutoUpdates"));
        autoUpdates->setToolTip(
            QStringLiteral("Checks GitHub once a day. No library data is sent."));
        autoUpdates->setChecked(
            settings_.value(QStringLiteral("updates/automatic"), true).toBool());
        l->addWidget(autoUpdates);
        auto *updateStatus =
            label(updater_.message().isEmpty() ? QStringLiteral("Updates download from GitHub.")
                                               : updater_.message(),
                  QStringLiteral("Muted"));
        updateStatus->setWordWrap(true);
        l->addWidget(updateStatus);
        auto *updateButton = button(QStringLiteral("Check for updates"), QStringLiteral("refresh"));
        updateButton->setObjectName(QStringLiteral("CheckUpdates"));
        l->addWidget(updateButton);
        auto *updateProgress = new QProgressBar;
        updateProgress->setRange(0, 1000);
        updateProgress->hide();
        l->addWidget(updateProgress);
        const auto refresh = [this, updateStatus, updateButton, updateProgress] {
            const auto state = updater_.state();
            updateStatus->setText(updater_.message().isEmpty()
                                      ? QStringLiteral("Updates download from GitHub.")
                                      : updater_.message());
            updateButton->setEnabled(state != AppUpdate::State::Checking);
            updateButton->setText(
                state == AppUpdate::State::Available     ? QStringLiteral("Download update")
                : state == AppUpdate::State::Ready       ? QStringLiteral("Install and restart")
                : state == AppUpdate::State::Downloading ? QStringLiteral("Cancel update")
                                                         : QStringLiteral("Check for updates"));
            updateProgress->setVisible(state == AppUpdate::State::Downloading);
        };
        connect(&updater_, &AppUpdate::changed, &dialog, refresh);
        connect(&updater_, &AppUpdate::progress, &dialog,
                [updateProgress](qint64 done, qint64 total) {
                    updateProgress->setValue(
                        total > 0 ? int(qMin(qint64(1000), done * 1000 / total)) : 0);
                });
        connect(updateButton, &QPushButton::clicked, &dialog,
                [this, &dialog, saveSettings, autoUpdates] {
                    switch (updater_.state()) {
                    case AppUpdate::State::Available:
                        updater_.download();
                        break;
                    case AppUpdate::State::Downloading:
                        updater_.cancel();
                        break;
                    case AppUpdate::State::Ready: {
                        QString error;
                        if (updater_.install(paths_.root, &error)) {
                            saveSettings(autoUpdates);
                            dialog.accept();
                            close();
                        } else
                            showError(QStringLiteral("Update could not start"), error);
                        break;
                    }
                    default:
                        updater_.check();
                        break;
                    }
                });
        refresh();
    }
    l->addSpacing(12);
    l->addWidget(label(QStringLiteral("Library folder"), QStringLiteral("SectionLabel")));
    auto *path = label(QDir::toNativeSeparators(paths_.root), QStringLiteral("Muted"));
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(path);
    auto *maintenance = button(QStringLiteral("Maintenance"), QStringLiteral("settings"));
    maintenance->setCheckable(true);
    maintenance->setObjectName(QStringLiteral("ToggleMaintenance"));
    l->addWidget(maintenance);
    auto *maintenancePanel = new QWidget;
    maintenancePanel->setObjectName(QStringLiteral("MaintenancePanel"));
    auto *maintenanceLayout = new QVBoxLayout(maintenancePanel);
    maintenanceLayout->setContentsMargins(0, 0, 0, 0);
    maintenanceLayout->setSpacing(12);
    connect(maintenance, &QPushButton::toggled, maintenancePanel, &QWidget::setVisible);
    auto *appearanceIndex =
        button(QStringLiteral("Build color and shape index"), QStringLiteral("spark"));
    appearanceIndex->setEnabled(!indexing_ && !db_->folders().isEmpty());
    connect(appearanceIndex, &QPushButton::clicked, &dialog, [this, &dialog] {
        dialog.accept();
        scan(false, true);
    });
    maintenanceLayout->addWidget(appearanceIndex);
    auto *reindex = button(QStringLiteral("Rebuild search index"), QStringLiteral("refresh"));
    reindex->setToolTip(QStringLiteral(
        "Reprocess all connected images, including visual search and text recognition."));
    reindex->setEnabled(!indexing_ && !db_->folders().isEmpty());
    connect(reindex, &QPushButton::clicked, &dialog, [this, &dialog, ocr, gpu] {
        settings_.setValue(QStringLiteral("library/ocr"), ocr->isChecked());
        settings_.setValue(QStringLiteral("library/gpu"), gpu->isChecked());
        dialog.accept();
        scan(true);
    });
    maintenanceLayout->addWidget(reindex);
    auto *formats = label(
        QStringLiteral("Formats available on this system: ") +
            [&] {
                QStringList formats;
                for (const auto &f : QImageReader::supportedImageFormats())
                    formats << QString::fromLatin1(f);
                formats.removeDuplicates();
                return formats.join(QStringLiteral(", "));
            }(),
        QStringLiteral("Muted"));
    formats->setWordWrap(true);
    maintenanceLayout->addWidget(formats);
    maintenancePanel->hide();
    l->addWidget(maintenancePanel);
    l->addStretch();
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(label(QStringLiteral("PicLocate ") + QString::fromLatin1(PICLOCATE_VERSION),
                             QStringLiteral("Muted")));
    buttons->addStretch();
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    actions->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("Primary"));
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    buttons->addWidget(actions);
    outer->addLayout(buttons);
    connect(actions, &QDialogButtonBox::accepted, &dialog,
            [this, &dialog, saveSettings, autoUpdates] {
                saveSettings(autoUpdates);
                dialog.accept();
                requestSearch();
            });
    dialog.exec();
}
} // namespace piclocate
