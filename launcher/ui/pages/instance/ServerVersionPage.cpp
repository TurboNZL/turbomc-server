// SPDX-License-Identifier: GPL-3.0-only
#include "ServerVersionPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "server/ServerBackup.h"
#include "server/ServerInstallTask.h"
#include "ui/dialogs/ProgressDialog.h"

ServerVersionPage::ServerVersionPage(MinecraftInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);

    m_installed = new QLabel(this);
    m_installed->setTextFormat(Qt::RichText);
    layout->addWidget(m_installed);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Switch to:"), this));
    m_software = new QComboBox(this);
    for (auto sw : Server::allSoftware()) {
        m_software->addItem(Server::softwareName(sw), Server::softwareKey(sw));
    }
    m_software->setCurrentIndex(m_software->findData(Server::softwareKey(m_instance->serverSoftware())));
    top->addWidget(m_software, 1);
    m_unstable = new QCheckBox(tr("Show snapshots and release candidates"), this);
    top->addWidget(m_unstable);
    m_refresh = new QPushButton(tr("Refresh"), this);
    top->addWidget(m_refresh);
    layout->addLayout(top);

    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);

    auto* bottom = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);
    m_install = new QPushButton(tr("Install selected version"), this);
    m_install->setEnabled(false);
    bottom->addWidget(m_install);
    layout->addLayout(bottom);

    connect(m_software, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(m_unstable, &QCheckBox::toggled, this, [this] { reload(); });
    connect(m_refresh, &QPushButton::clicked, this, [this] { reload(true); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this] { m_install->setEnabled(m_list->currentItem() != nullptr); });
    connect(m_install, &QPushButton::clicked, this, &ServerVersionPage::install);
    updateInstalledLabel();
}

Server::Software ServerVersionPage::software() const
{
    return Server::softwareFromKey(m_software->currentData().toString());
}

void ServerVersionPage::openedImpl()
{
    updateInstalledLabel();
    if (!m_initialized) {
        m_initialized = true;
        reload();
    }
}

void ServerVersionPage::updateInstalledLabel()
{
    const QString version = m_instance->serverMcVersion();
    if (version.isEmpty()) {
        m_installed->setText(tr("<b>No server software is installed yet.</b>"));
        return;
    }
    const QString build = m_instance->settings()->get("ServerBuild").toString();
    const int java = m_instance->settings()->get("ServerJavaMajor").toInt();
    QString text = tr("<b>Installed:</b> %1 for Minecraft %2").arg(Server::softwareName(m_instance->serverSoftware()), version);
    if (!build.isEmpty()) {
        text += tr(" (build %1)").arg(build);
    }
    if (java > 0) {
        text += tr(", needs Java %1").arg(java);
    }
    m_installed->setText(text);
}

void ServerVersionPage::reload(bool force)
{
    const QString key = Server::softwareKey(software()) + (m_unstable->isChecked() ? ":all" : ":stable");
    if (m_task) {
        m_task->disconnect(this);
        m_task->abort();
        m_task.reset();
    }
    if (!force && m_cache.contains(key)) {
        m_list->clear();
        for (const auto& v : m_cache[key]) {
            auto* item = new QListWidgetItem(v.stable ? v.id : tr("%1 (snapshot)").arg(v.id), m_list);
            item->setData(Qt::UserRole, v.id);
        }
        m_status->clear();
        return;
    }
    m_list->clear();
    m_status->setText(tr("Loading versions..."));
    m_refresh->setEnabled(false);
    m_task.reset(new Server::VersionsTask(software(), m_unstable->isChecked()));
    connect(m_task.get(), &Task::succeeded, this, [this, key] {
        m_cache[key] = m_task->versions();
        m_refresh->setEnabled(true);
        reload(false);
    });
    connect(m_task.get(), &Task::failed, this, [this](const QString& reason) {
        m_refresh->setEnabled(true);
        m_status->setText(tr("Could not load versions: %1").arg(reason));
    });
    m_task->start();
}

void ServerVersionPage::install()
{
    auto* item = m_list->currentItem();
    if (!item) {
        return;
    }
    if (m_instance->isRunning()) {
        QMessageBox::warning(this, tr("Server is running"), tr("Stop the server before changing its version."));
        return;
    }
    const QString version = item->data(Qt::UserRole).toString();
    const auto sw = software();
    const auto answer = QMessageBox::question(
        this, tr("Change server version"),
        tr("Install %1 %2 into this server?\n\nThe world and settings are kept, but a world saved by a newer Minecraft version "
           "cannot be opened by an older one. Make a copy of the server folder first if you are going back to an older version.")
            .arg(Server::softwareName(sw), version));
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (Server::Backups::hasWorld(m_instance)) {
        QString error;
        auto backup = Server::Backups::create(m_instance, QString("before-%1-%2").arg(Server::softwareKey(sw), version), &error);
        bool backedUp = false;
        if (backup) {
            ProgressDialog backupDialog(this);
            backedUp = backupDialog.execWithTask(backup.get()) == QDialog::Accepted && backup->wasSuccessful();
        }
        if (!backedUp) {
            const auto go = QMessageBox::question(this, tr("Backup failed"),
                                                  tr("The world could not be backed up first. Install anyway?"));
            if (go != QMessageBox::Yes) {
                return;
            }
        }
    }
    auto task = makeShared<Server::InstallTask>(m_instance, sw, version);
    ProgressDialog dialog(this);
    const bool ok = dialog.execWithTask(task.get());
    updateInstalledLabel();
    if (!ok) {
        QMessageBox::warning(this, tr("Could not install"), task->failReason());
    }
}
