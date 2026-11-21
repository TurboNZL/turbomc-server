// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMap>
#include <QWidget>

#include "server/ServerLookup.h"
#include "ui/pages/BasePage.h"

class NewInstanceDialog;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

/// "New server" page of the new-instance dialog: pick server software and a Minecraft version.
class ServerPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit ServerPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    ~ServerPage() override = default;

    QString displayName() const override { return tr("New server"); }
    QIcon icon() const override { return QIcon::fromTheme("minecraft"); }
    QString id() const override { return "server"; }
    QString helpPage() const override { return "Server-platform"; }
    void retranslate() override;
    void openedImpl() override;

   private slots:
    void reload(bool force = false);
    void selectionChanged();

   private:
    void suggestCurrent();
    Server::Software software() const;

    NewInstanceDialog* m_dialog;
    QComboBox* m_software;
    QCheckBox* m_unstable;
    QPushButton* m_refresh;
    QListWidget* m_list;
    QLabel* m_status;
    QMap<QString, QList<Server::VersionEntry>> m_cache;  // key: software + unstable flag
    shared_qobject_ptr<Server::VersionsTask> m_task;
    bool m_initialized = false;
};
