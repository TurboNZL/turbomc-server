// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include "minecraft/MinecraftInstance.h"
#include "ui/pages/BasePage.h"

class QLabel;
class QPushButton;
class QTableWidget;

/// Lists, creates, restores and deletes world backups.
class ServerBackupsPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit ServerBackupsPage(MinecraftInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Backups"); }
    QIcon icon() const override { return QIcon::fromTheme("export"); }
    QString id() const override { return "backups"; }
    QString helpPage() const override { return "Server-backups"; }
    void openedImpl() override;

   private slots:
    void refresh();
    void backupNow();
    void restore();
    void remove();
    void openFolder();

   private:
    /// Runs a task in a progress dialog; returns true when it finished successfully.
    bool runTask(Task::Ptr task);
    QString selectedPath() const;

    MinecraftInstance* m_instance;
    QLabel* m_note;
    QTableWidget* m_table;
    QPushButton* m_backup;
    QPushButton* m_restore;
    QPushButton* m_delete;
};
