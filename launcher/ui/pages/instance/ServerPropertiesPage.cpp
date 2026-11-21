// SPDX-License-Identifier: GPL-3.0-only
#include "ServerPropertiesPage.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

ServerPropertiesPage::ServerPropertiesPage(MinecraftInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    auto* top = new QHBoxLayout();
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter properties"));
    m_filter->setClearButtonEnabled(true);
    top->addWidget(m_filter, 1);
    m_add = new QPushButton(tr("Add property..."), this);
    m_reload = new QPushButton(tr("Reload from file"), this);
    top->addWidget(m_add);
    top->addWidget(m_reload);
    layout->addLayout(top);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({ tr("Property"), tr("Value") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &ServerPropertiesPage::applyFilter);
    connect(m_add, &QPushButton::clicked, this, &ServerPropertiesPage::addProperty);
    connect(m_reload, &QPushButton::clicked, this, &ServerPropertiesPage::reload);
}

QString ServerPropertiesPage::path() const
{
    return QDir(m_instance->gameRoot()).absoluteFilePath("server.properties");
}

void ServerPropertiesPage::openedImpl()
{
    if (!m_loaded) {
        reload();
    }
}

void ServerPropertiesPage::reload()
{
    m_loaded = true;
    m_table->setRowCount(0);
    const bool exists = QFileInfo::exists(path()) && m_properties.load(path());
    m_add->setEnabled(exists);
    m_table->setEnabled(exists);
    if (!exists) {
        m_note->setText(tr("There is no <code>server.properties</code> yet. The server creates it the first time it starts, "
                           "so start it once and come back."));
        return;
    }
    m_note->setText(m_instance->isRunning() ? tr("The server is running: changes are saved now but only apply after a restart.")
                                            : tr("Changes are saved when you close this window."));
    for (const auto& [key, value] : m_properties.entries()) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* keyItem = new QTableWidgetItem(key);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        const QString description = Server::Properties::describe(key);
        if (!description.isEmpty()) {
            keyItem->setToolTip(description);
        }
        auto* valueItem = new QTableWidgetItem(value);
        if (!description.isEmpty()) {
            valueItem->setToolTip(description);
        }
        m_table->setItem(row, 0, keyItem);
        m_table->setItem(row, 1, valueItem);
    }
    applyFilter(m_filter->text());
}

void ServerPropertiesPage::addProperty()
{
    bool ok = false;
    const QString key = QInputDialog::getText(this, tr("Add property"), tr("Property name (for example <b>motd</b>):"), QLineEdit::Normal,
                                              QString(), &ok)
                            .trimmed();
    if (!ok || key.isEmpty()) {
        return;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->text() == key) {
            m_table->selectRow(row);
            m_table->scrollToItem(m_table->item(row, 1));
            return;
        }
    }
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* keyItem = new QTableWidgetItem(key);
    keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 0, keyItem);
    m_table->setItem(row, 1, new QTableWidgetItem());
    m_table->editItem(m_table->item(row, 1));
}

void ServerPropertiesPage::applyFilter(const QString& text)
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const bool match = text.isEmpty() || m_table->item(row, 0)->text().contains(text, Qt::CaseInsensitive) ||
                           m_table->item(row, 1)->text().contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(row, !match);
    }
}

bool ServerPropertiesPage::apply()
{
    if (!m_loaded || !m_table->isEnabled()) {
        return true;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        m_properties.set(m_table->item(row, 0)->text(), m_table->item(row, 1)->text());
    }
    if (!m_properties.save(path())) {
        QMessageBox::warning(this, tr("Could not save"), tr("Could not write %1.").arg(path()));
        return false;
    }
    return true;
}
