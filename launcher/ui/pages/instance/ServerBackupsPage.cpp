// SPDX-License-Identifier: GPL-3.0-only
#include "ServerBackupsPage.h"

#include <QEventLoop>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "DesktopServices.h"
#include "server/ServerBackup.h"
#include "ui/dialogs/ProgressDialog.h"

ServerBackupsPage::ServerBackupsPage(MinecraftInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({ tr("Backup"), tr("Created"), tr("Size") });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    auto* buttons = new QHBoxLayout();
    m_backup = new QPushButton(tr("Back up now"), this);
    m_restore = new QPushButton(tr("Restore..."), this);
    m_delete = new QPushButton(tr("Delete"), this);
    auto* open = new QPushButton(tr("Open folder"), this);
    buttons->addWidget(m_backup);
    buttons->addWidget(m_restore);
    buttons->addWidget(m_delete);
    buttons->addStretch(1);
    buttons->addWidget(open);
    layout->addLayout(buttons);

    connect(m_backup, &QPushButton::clicked, this, &ServerBackupsPage::backupNow);
    connect(m_restore, &QPushButton::clicked, this, &ServerBackupsPage::restore);
    connect(m_delete, &QPushButton::clicked, this, &ServerBackupsPage::remove);
    connect(open, &QPushButton::clicked, this, &ServerBackupsPage::openFolder);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool any = !selectedPath().isEmpty();
        m_restore->setEnabled(any);
        m_delete->setEnabled(any);
    });
    m_restore->setEnabled(false);
    m_delete->setEnabled(false);
}

void ServerBackupsPage::openedImpl()
{
    refresh();
}

QString ServerBackupsPage::selectedPath() const
{
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? QString() : m_table->item(rows.first().row(), 0)->data(Qt::UserRole).toString();
}

void ServerBackupsPage::refresh()
{
    m_table->setRowCount(0);
    const auto entries = Server::Backups::list(m_instance);
    qint64 total = 0;
    for (const auto& e : entries) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* name = new QTableWidgetItem(e.name);
        name->setData(Qt::UserRole, e.path);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, new QTableWidgetItem(QLocale().toString(e.time, QLocale::ShortFormat)));
        m_table->setItem(row, 2, new QTableWidgetItem(QLocale().formattedDataSize(e.size)));
        total += e.size;
    }
    m_note->setText(entries.isEmpty() ? tr("No backups yet. A backup is also made automatically before you change the server version.")
                                      : tr("%n backup(s), %1 in total. A backup is also made automatically before you change the server "
                                           "version.",
                                           "", entries.size())
                                            .arg(QLocale().formattedDataSize(total)));
}

bool ServerBackupsPage::runTask(Task::Ptr task)
{
    ProgressDialog dialog(this);
    const bool ok = dialog.execWithTask(task.get()) == QDialog::Accepted;
    if (!ok && !task->wasSuccessful() && !task->failReason().isEmpty()) {
        QMessageBox::warning(this, tr("Backup problem"), task->failReason());
    }
    return ok && task->wasSuccessful();
}

void ServerBackupsPage::backupNow()
{
    auto* server = m_instance->serverProcess();
    const bool running = server->state() == Server::Process::State::Running;
    if (running) {
        // Ask the server to flush the world to disk and stop writing while the files are copied.
        server->sendCommand("save-off");
        server->sendCommand("save-all flush");
        QEventLoop wait;
        QTimer::singleShot(4000, &wait, &QEventLoop::quit);
        wait.exec();
    }
    QString error;
    auto task = Server::Backups::create(m_instance, QString(), &error);
    if (!task) {
        if (running) {
            server->sendCommand("save-on");
        }
        QMessageBox::information(this, tr("Nothing to back up"), error);
        return;
    }
    runTask(task);
    if (running) {
        server->sendCommand("save-on");
    }
    refresh();
}

void ServerBackupsPage::restore()
{
    const QString path = selectedPath();
    if (path.isEmpty()) {
        return;
    }
    if (m_instance->isRunning()) {
        QMessageBox::warning(this, tr("Server is running"), tr("Stop the server before restoring a backup."));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Restore backup"),
        tr("Replace the current world with this backup?\n\nYour current world is backed up first, so you can undo this."));
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (Server::Backups::hasWorld(m_instance)) {
        QString error;
        auto safety = Server::Backups::create(m_instance, "before-restore", &error);
        if (!safety || !runTask(safety)) {
            QMessageBox::warning(this, tr("Restore cancelled"), tr("Could not back up the current world first, so nothing was changed."));
            return;
        }
    }
    if (!Server::Backups::removeWorlds(m_instance)) {
        QMessageBox::warning(this, tr("Restore failed"), tr("Could not remove the current world folder."));
        refresh();
        return;
    }
    if (!runTask(Server::Backups::restore(m_instance, path))) {
        QMessageBox::warning(this, tr("Restore failed"),
                             tr("The backup could not be unpacked. Your previous world is saved as the newest \"before-restore\" backup."));
    }
    refresh();
}

void ServerBackupsPage::remove()
{
    const QString path = selectedPath();
    if (path.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Delete backup"), tr("Permanently delete this backup?")) != QMessageBox::Yes) {
        return;
    }
    QFile::remove(path);
    refresh();
}

void ServerBackupsPage::openFolder()
{
    DesktopServices::openPath(Server::Backups::folder(m_instance), true);
}
