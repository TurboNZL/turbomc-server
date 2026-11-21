// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMap>
#include <QWidget>

#include "minecraft/MinecraftInstance.h"
#include "server/ServerLookup.h"
#include "ui/pages/BasePage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

/// Shows which server software/version is installed and switches to another one.
class ServerVersionPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit ServerVersionPage(MinecraftInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Server version"); }
    QIcon icon() const override { return QIcon::fromTheme("minecraft"); }
    QString id() const override { return "version"; }
    QString helpPage() const override { return "Server-version"; }
    void openedImpl() override;

   private slots:
    void reload(bool force = false);
    void install();

   private:
    void updateInstalledLabel();
    Server::Software software() const;

    MinecraftInstance* m_instance;
    QLabel* m_installed;
    QComboBox* m_software;
    QCheckBox* m_unstable;
    QPushButton* m_refresh;
    QListWidget* m_list;
    QPushButton* m_install;
    QLabel* m_status;
    QMap<QString, QList<Server::VersionEntry>> m_cache;
    shared_qobject_ptr<Server::VersionsTask> m_task;
    bool m_initialized = false;
};
