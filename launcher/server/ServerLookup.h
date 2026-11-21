// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonDocument>
#include <QUrl>
#include <functional>

#include "ServerTypes.h"
#include "net/NetJob.h"
#include "tasks/Task.h"

namespace Server {

/// Shared plumbing: fetch JSON documents one after another.
class HttpTask : public Task {
    Q_OBJECT
   public:
    using Task::Task;

   protected:
    using JsonHandler = std::function<void(const QJsonDocument&)>;
    void getJson(const QUrl& url, JsonHandler onDone);

   private:
    QList<NetJob::Ptr> m_jobs;
};

/// Lists the Minecraft versions a piece of server software is available for (newest first).
class VersionsTask : public HttpTask {
    Q_OBJECT
   public:
    VersionsTask(Software software, bool includeUnstable) : m_software(software), m_includeUnstable(includeUnstable) {}
    QList<VersionEntry> versions() const { return m_versions; }

   protected:
    void executeTask() override;

   private:
    void finish();

    Software m_software;
    bool m_includeUnstable;
    QList<VersionEntry> m_versions;
};

/// Works out which jar to download for a software + Minecraft version (latest build).
class ResolveTask : public HttpTask {
    Q_OBJECT
   public:
    ResolveTask(Software software, QString mcVersion) : m_software(software), m_mcVersion(std::move(mcVersion)) {}
    Download download() const { return m_download; }

   protected:
    void executeTask() override;

   private:
    void lookupJava();

    Software m_software;
    QString m_mcVersion;
    Download m_download;
};

}  // namespace Server
