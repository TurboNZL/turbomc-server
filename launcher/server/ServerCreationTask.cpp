// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCreationTask.h"

#include "FileSystem.h"
#include "settings/INISettingsObject.h"

namespace Server {

void CreationTask::executeTask()
{
    setStatus(tr("Creating %1 server %2").arg(softwareName(m_software), m_mcVersion));

    m_instance = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);
    m_instance->reloadSettings();
    {
        const SettingsObject::Lock lock(m_instance->settings());
        m_instance->setName(name());
        m_instance->setIconKey(m_instIcon);
    }

    m_install.reset(new InstallTask(m_instance.get(), m_software, m_mcVersion));
    connect(m_install.get(), &Task::succeeded, this, [this] { emitSucceeded(); });
    connect(m_install.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(reason); });
    connect(m_install.get(), &Task::aborted, this, [this] { emitAborted(); });
    connect(m_install.get(), &Task::status, this, &Task::setStatus);
    connect(m_install.get(), &Task::progress, this, &Task::setProgress);
    setAbortable(true);
    m_install->start();
}

bool CreationTask::abort()
{
    if (m_install && m_install->isRunning()) {
        m_install->abort();
    }
    return InstanceTask::abort();
}

}  // namespace Server
