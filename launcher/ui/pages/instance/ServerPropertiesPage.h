// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include "minecraft/MinecraftInstance.h"
#include "server/ServerProperties.h"
#include "ui/pages/BasePage.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

/// Edits the server's `server.properties`, keeping its comments and order.
class ServerPropertiesPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit ServerPropertiesPage(MinecraftInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Properties"); }
    QIcon icon() const override { return QIcon::fromTheme("settings"); }
    QString id() const override { return "properties"; }
    QString helpPage() const override { return "Server-properties"; }
    bool apply() override;
    void openedImpl() override;

   private slots:
    void reload();
    void addProperty();
    void applyFilter(const QString& text);

   private:
    QString path() const;

    MinecraftInstance* m_instance;
    Server::Properties m_properties;
    QLabel* m_note;
    QLineEdit* m_filter;
    QTableWidget* m_table;
    QPushButton* m_add;
    QPushButton* m_reload;
    bool m_loaded = false;
};
