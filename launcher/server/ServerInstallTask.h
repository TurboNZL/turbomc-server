// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ServerLookup.h"
#include "ServerTypes.h"
#include "tasks/Task.h"

class MinecraftInstance;

namespace Server {

/// Downloads the server jar for an instance and records what was installed in its settings.
/// Used both when creating a server and when switching its software/version.
class InstallTask : public Task {
    Q_OBJECT
   public:
    InstallTask(MinecraftInstance* instance, Software software, QString mcVersion);

   protected:
    void executeTask() override;
    bool abort() override;

   private:
    void download(const Download& info);

    MinecraftInstance* m_instance;
    Software m_software;
    QString m_mcVersion;
    shared_qobject_ptr<ResolveTask> m_resolve;
    NetJob::Ptr m_job;
};

}  // namespace Server
