// SPDX-License-Identifier: GPL-3.0-only
#include "ServerLookup.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

#include "Application.h"
#include "Version.h"
#include "net/Request.h"

namespace Server {

namespace {
const char* const MOJANG_MANIFEST = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
const char* const PAPER_API = "https://fill.papermc.io/v3/projects/paper";
const char* const PURPUR_API = "https://api.purpurmc.org/v2/purpur";
const char* const FABRIC_META = "https://meta.fabricmc.net/v2/versions";

bool looksStable(const QString& version)
{
    static const QRegularExpression unstable("(-pre|-rc|snapshot|-beta|-alpha)", QRegularExpression::CaseInsensitiveOption);
    return !unstable.match(version).hasMatch();
}

void sortNewestFirst(QList<VersionEntry>& list)
{
    std::stable_sort(list.begin(), list.end(), [](const VersionEntry& a, const VersionEntry& b) { return Version(a.id) > Version(b.id); });
}
}  // namespace

void HttpTask::getJson(const QUrl& url, JsonHandler onDone)
{
    auto job = makeShared<NetJob>(QString("Server lookup"), APPLICATION->network());
    auto [request, response] = Net::Request::makeByteArray(url);
    job->addNetAction(request);
    connect(job.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(reason); });
    connect(job.get(), &Task::aborted, this, [this] { emitAborted(); });
    connect(job.get(), &Task::succeeded, this, [this, response, url, onDone = std::move(onDone)] {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(*response, &error);
        if (error.error != QJsonParseError::NoError) {
            emitFailed(tr("Could not read the answer from %1: %2").arg(url.host(), error.errorString()));
            return;
        }
        onDone(doc);
    });
    m_jobs.append(job);
    job->start();
}

// ---------------------------------------------------------------- versions

void VersionsTask::executeTask()
{
    setStatus(tr("Looking up %1 versions").arg(softwareName(m_software)));
    switch (m_software) {
        case Software::Vanilla:
            getJson(QUrl(MOJANG_MANIFEST), [this](const QJsonDocument& doc) {
                for (const auto& v : doc.object().value("versions").toArray()) {
                    const auto o = v.toObject();
                    VersionEntry e;
                    e.id = o.value("id").toString();
                    e.stable = o.value("type").toString() == "release";
                    e.releaseTime = QDateTime::fromString(o.value("releaseTime").toString(), Qt::ISODate);
                    // Only versions that have a server jar; the very old ones do not, and the manifest is newest first.
                    if (!e.id.isEmpty() && (m_includeUnstable || e.stable)) {
                        m_versions.append(e);
                    }
                }
                finish();
            });
            return;
        case Software::Paper:
            getJson(QUrl(PAPER_API), [this](const QJsonDocument& doc) {
                const auto groups = doc.object().value("versions").toObject();
                for (auto it = groups.begin(); it != groups.end(); ++it) {
                    for (const auto& v : it.value().toArray()) {
                        VersionEntry e;
                        e.id = v.toString();
                        e.stable = looksStable(e.id);
                        if (!e.id.isEmpty() && (m_includeUnstable || e.stable)) {
                            m_versions.append(e);
                        }
                    }
                }
                sortNewestFirst(m_versions);
                finish();
            });
            return;
        case Software::Purpur:
            getJson(QUrl(PURPUR_API), [this](const QJsonDocument& doc) {
                for (const auto& v : doc.object().value("versions").toArray()) {
                    VersionEntry e;
                    e.id = v.toString();
                    e.stable = looksStable(e.id);
                    if (!e.id.isEmpty() && (m_includeUnstable || e.stable)) {
                        m_versions.append(e);
                    }
                }
                sortNewestFirst(m_versions);
                finish();
            });
            return;
        case Software::Fabric:
            getJson(QUrl(QString(FABRIC_META) + "/game"), [this](const QJsonDocument& doc) {
                for (const auto& v : doc.array()) {
                    const auto o = v.toObject();
                    VersionEntry e;
                    e.id = o.value("version").toString();
                    e.stable = o.value("stable").toBool();
                    if (!e.id.isEmpty() && (m_includeUnstable || e.stable)) {
                        m_versions.append(e);
                    }
                }
                finish();
            });
            return;
    }
    emitFailed(tr("Unknown server software"));
}

void VersionsTask::finish()
{
    if (m_versions.isEmpty()) {
        emitFailed(tr("No versions were found for %1").arg(softwareName(m_software)));
        return;
    }
    emitSucceeded();
}

// ----------------------------------------------------------------- resolve

void ResolveTask::executeTask()
{
    setStatus(tr("Finding the latest %1 build for %2").arg(softwareName(m_software), m_mcVersion));
    switch (m_software) {
        case Software::Vanilla:
            getJson(QUrl(MOJANG_MANIFEST), [this](const QJsonDocument& manifest) {
                QString versionUrl;
                for (const auto& v : manifest.object().value("versions").toArray()) {
                    if (v.toObject().value("id").toString() == m_mcVersion) {
                        versionUrl = v.toObject().value("url").toString();
                        break;
                    }
                }
                if (versionUrl.isEmpty()) {
                    emitFailed(tr("Minecraft %1 was not found").arg(m_mcVersion));
                    return;
                }
                getJson(QUrl(versionUrl), [this](const QJsonDocument& doc) {
                    const auto server = doc.object().value("downloads").toObject().value("server").toObject();
                    if (server.value("url").toString().isEmpty()) {
                        emitFailed(tr("Minecraft %1 has no server download").arg(m_mcVersion));
                        return;
                    }
                    m_download.url = server.value("url").toString();
                    m_download.sha1 = server.value("sha1").toString();
                    m_download.fileName = QString("minecraft_server.%1.jar").arg(m_mcVersion);
                    m_download.javaMajor = doc.object().value("javaVersion").toObject().value("majorVersion").toInt(0);
                    emitSucceeded();
                });
            });
            return;
        case Software::Paper:
            getJson(QUrl(QString("%1/versions/%2/builds/latest").arg(PAPER_API, m_mcVersion)), [this](const QJsonDocument& doc) {
                const auto o = doc.object();
                const auto jar = o.value("downloads").toObject().value("server:default").toObject();
                if (jar.value("url").toString().isEmpty()) {
                    emitFailed(tr("Paper has no build for %1").arg(m_mcVersion));
                    return;
                }
                m_download.url = jar.value("url").toString();
                m_download.fileName = jar.value("name").toString();
                m_download.sha256 = jar.value("checksums").toObject().value("sha256").toString();
                m_download.build = QString::number(o.value("id").toInt());
                lookupJava();
            });
            return;
        case Software::Purpur:
            getJson(QUrl(QString("%1/%2").arg(PURPUR_API, m_mcVersion)), [this](const QJsonDocument& doc) {
                const auto build = doc.object().value("builds").toObject().value("latest").toString();
                if (build.isEmpty()) {
                    emitFailed(tr("Purpur has no build for %1").arg(m_mcVersion));
                    return;
                }
                m_download.build = build;
                m_download.url = QString("%1/%2/%3/download").arg(PURPUR_API, m_mcVersion, build);
                m_download.fileName = QString("purpur-%1-%2.jar").arg(m_mcVersion, build);
                lookupJava();
            });
            return;
        case Software::Fabric:
            getJson(QUrl(QString("%1/loader/%2").arg(FABRIC_META, m_mcVersion)), [this](const QJsonDocument& loaders) {
                QString loader;
                for (const auto& entry : loaders.array()) {
                    const auto l = entry.toObject().value("loader").toObject();
                    if (l.value("stable").toBool()) {
                        loader = l.value("version").toString();
                        break;
                    }
                }
                if (loader.isEmpty() && !loaders.array().isEmpty()) {
                    loader = loaders.array().first().toObject().value("loader").toObject().value("version").toString();
                }
                if (loader.isEmpty()) {
                    emitFailed(tr("Fabric does not support Minecraft %1").arg(m_mcVersion));
                    return;
                }
                getJson(QUrl(QString("%1/installer").arg(FABRIC_META)), [this, loader](const QJsonDocument& installers) {
                    QString installer;
                    for (const auto& entry : installers.array()) {
                        if (entry.toObject().value("stable").toBool()) {
                            installer = entry.toObject().value("version").toString();
                            break;
                        }
                    }
                    if (installer.isEmpty()) {
                        emitFailed(tr("Could not find a Fabric installer version"));
                        return;
                    }
                    m_download.build = loader;
                    m_download.url = QString("%1/loader/%2/%3/%4/server/jar").arg(FABRIC_META, m_mcVersion, loader, installer);
                    m_download.fileName = QString("fabric-server-mc.%1-loader.%2-launcher.%3.jar").arg(m_mcVersion, loader, installer);
                    lookupJava();
                });
            });
            return;
    }
    emitFailed(tr("Unknown server software"));
}

/// Only Mojang knows which Java a release needs; ask its version file.
void ResolveTask::lookupJava()
{
    getJson(QUrl(MOJANG_MANIFEST), [this](const QJsonDocument& manifest) {
        QString versionUrl;
        for (const auto& v : manifest.object().value("versions").toArray()) {
            if (v.toObject().value("id").toString() == m_mcVersion) {
                versionUrl = v.toObject().value("url").toString();
                break;
            }
        }
        if (versionUrl.isEmpty()) {
            emitSucceeded();  // not fatal, Java just stays "unknown"
            return;
        }
        getJson(QUrl(versionUrl), [this](const QJsonDocument& doc) {
            m_download.javaMajor = doc.object().value("javaVersion").toObject().value("majorVersion").toInt(0);
            emitSucceeded();
        });
    });
}

}  // namespace Server
