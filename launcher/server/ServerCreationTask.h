// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <memory>

#include "InstanceTask.h"
#include "minecraft/MinecraftInstance.h"
#include "server/ServerInstallTask.h"
#include "server/ServerTypes.h"

namespace Server {

/// Creates a new server instance in the staging folder and installs the chosen software into it.
class CreationTask final : public InstanceTask {
    Q_OBJECT
   public:
    CreationTask(Software software, QString mcVersion) : m_software(software), m_mcVersion(std::move(mcVersion)) {}

    void executeTask() override;
    bool abort() override;

   private:
    Software m_software;
    QString m_mcVersion;
    std::unique_ptr<MinecraftInstance> m_instance;
    shared_qobject_ptr<InstallTask> m_install;
};

}  // namespace Server
