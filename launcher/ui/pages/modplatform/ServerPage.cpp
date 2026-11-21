// SPDX-License-Identifier: GPL-3.0-only
#include "ServerPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "server/ServerCreationTask.h"
#include "ui/dialogs/NewInstanceDialog.h"

ServerPage::ServerPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Server software:"), this));
    m_software = new QComboBox(this);
    for (auto software : Server::allSoftware()) {
        m_software->addItem(Server::softwareName(software), Server::softwareKey(software));
    }
    top->addWidget(m_software, 1);
    m_unstable = new QCheckBox(tr("Show snapshots and release candidates"), this);
    top->addWidget(m_unstable);
    m_refresh = new QPushButton(tr("Refresh"), this);
    top->addWidget(m_refresh);
    layout->addLayout(top);

    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(m_software, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(m_unstable, &QCheckBox::toggled, this, [this] { reload(); });
    connect(m_refresh, &QPushButton::clicked, this, [this] { reload(true); });
    connect(m_list, &QListWidget::currentItemChanged, this, &ServerPage::selectionChanged);
}

Server::Software ServerPage::software() const
{
    return Server::softwareFromKey(m_software->currentData().toString());
}

void ServerPage::retranslate() {}

void ServerPage::openedImpl()
{
    if (!m_initialized) {
        m_initialized = true;
        reload();
    } else {
        suggestCurrent();
    }
}

void ServerPage::reload(bool force)
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
        if (m_list->count() > 0) {
            m_list->setCurrentRow(0);
        } else {
            m_dialog->setSuggestedPack();
        }
        return;
    }

    m_list->clear();
    m_dialog->setSuggestedPack();
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

void ServerPage::selectionChanged()
{
    suggestCurrent();
}

void ServerPage::suggestCurrent()
{
    if (!isOpened) {
        return;
    }
    auto* item = m_list->currentItem();
    if (!item) {
        m_dialog->setSuggestedPack();
        return;
    }
    const QString version = item->data(Qt::UserRole).toString();
    const auto sw = software();
    m_dialog->setSuggestedPack(QString("%1 %2").arg(Server::softwareName(sw), version), new Server::CreationTask(sw, version));
    m_dialog->setSuggestedIcon("default");
}
