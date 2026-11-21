// SPDX-License-Identifier: GPL-3.0-only
#include "ServerInstallTask.h"

#include <QCryptographicHash>
#include <QDir>

#include "Application.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "net/ChecksumValidator.h"
#include "net/Request.h"
#include "settings/SettingsObject.h"

namespace Server {

InstallTask::InstallTask(MinecraftInstance* instance, Software software, QString mcVersion)
    : m_instance(instance), m_software(software), m_mcVersion(std::move(mcVersion))
{}

void InstallTask::executeTask()
{
    m_resolve.reset(new ResolveTask(m_software, m_mcVersion));
    connect(m_resolve.get(), &Task::succeeded, this, [this] { download(m_resolve->download()); });
    connect(m_resolve.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(reason); });
    connect(m_resolve.get(), &Task::aborted, this, [this] { emitAborted(); });
    connect(m_resolve.get(), &Task::status, this, &Task::setStatus);
    m_resolve->start();
}

void InstallTask::download(const Download& info)
{
    const QString root = m_instance->gameRoot();
    if (!FS::ensureFolderPathExists(root)) {
        emitFailed(tr("Could not create the server folder %1").arg(root));
        return;
    }
    const QString target = QDir(root).absoluteFilePath("server.jar");

    m_job = makeShared<NetJob>(tr("Downloading %1").arg(info.fileName), APPLICATION->network());
    auto request = Net::Request::makeFile(QUrl(info.url), target);
    if (!info.sha256.isEmpty()) {
        request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha256, info.sha256));
    } else if (!info.sha1.isEmpty()) {
        request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, info.sha1));
    }
    m_job->addNetAction(request);

    connect(m_job.get(), &Task::succeeded, this, [this, info] {
        auto* s = m_instance->settings();
        s->set("ServerSoftware", softwareKey(m_software));
        s->set("ServerMcVersion", m_mcVersion);
        s->set("ServerBuild", info.build);
        s->set("ServerJavaMajor", info.javaMajor);
        emitSucceeded();
    });
    connect(m_job.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(reason); });
    connect(m_job.get(), &Task::aborted, this, [this] { emitAborted(); });
    connect(m_job.get(), &Task::progress, this, &Task::setProgress);
    connect(m_job.get(), &Task::status, this, &Task::setStatus);
    setStatus(tr("Downloading %1").arg(info.fileName));
    m_job->start();
}

bool InstallTask::abort()
{
    bool ok = true;
    if (m_resolve && m_resolve->isRunning()) {
        ok = m_resolve->abort() && ok;
    }
    if (m_job && m_job->isRunning()) {
        ok = m_job->abort() && ok;
    }
    return Task::abort() && ok;
}

}  // namespace Server
