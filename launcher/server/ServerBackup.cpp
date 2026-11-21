// SPDX-License-Identifier: GPL-3.0-only
#include "ServerBackup.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

#include "FileSystem.h"
#include "MMCZip.h"
#include "archive/ExportToZipTask.h"
#include "archive/ExtractZipTask.h"
#include "minecraft/MinecraftInstance.h"
#include "server/ServerProperties.h"

namespace Server::Backups {

QString folder(const MinecraftInstance* instance)
{
    return QDir(instance->instanceRoot()).absoluteFilePath("backups");
}

QString levelName(const MinecraftInstance* instance)
{
    Properties props;
    props.load(QDir(instance->gameRoot()).absoluteFilePath("server.properties"));
    const QString name = props.value("level-name").trimmed();
    return name.isEmpty() ? QStringLiteral("world") : name;
}

QStringList worldFolders(const MinecraftInstance* instance)
{
    const QDir root(instance->gameRoot());
    const QString name = levelName(instance);
    QStringList out;
    for (const auto& candidate : { name, name + "_nether", name + "_the_end" }) {
        if (QFileInfo(root.absoluteFilePath(candidate)).isDir()) {
            out << root.absoluteFilePath(candidate);
        }
    }
    return out;
}

bool hasWorld(const MinecraftInstance* instance)
{
    return !worldFolders(instance).isEmpty();
}

QList<Entry> list(const MinecraftInstance* instance)
{
    QList<Entry> out;
    const QDir dir(folder(instance));
    for (const auto& info : dir.entryInfoList({ "*.zip" }, QDir::Files)) {
        Entry e;
        e.path = info.absoluteFilePath();
        e.name = info.completeBaseName();
        e.time = info.lastModified();
        e.size = info.size();
        out.append(e);
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.time > b.time; });
    return out;
}

Task::Ptr create(const MinecraftInstance* instance, const QString& label, QString* error)
{
    const QStringList worlds = worldFolders(instance);
    if (worlds.isEmpty()) {
        if (error) {
            *error = QObject::tr("There is no world to back up yet. Start the server once to create it.");
        }
        return nullptr;
    }
    const QString root = instance->gameRoot();
    if (!FS::ensureFolderPathExists(folder(instance))) {
        if (error) {
            *error = QObject::tr("Could not create the backups folder.");
        }
        return nullptr;
    }

    QFileInfoList files;
    for (const auto& world : worlds) {
        // Never skip anything: a world with a missing file is a broken backup.
        MMCZip::collectFileListRecursively(root, world, &files, nullptr);
    }
    for (const auto& small : { "server.properties", "ops.json", "whitelist.json", "banned-players.json", "banned-ips.json" }) {
        const QFileInfo info(QDir(root).absoluteFilePath(small));
        if (info.isFile()) {
            files.append(info);
        }
    }

    QString name = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    if (!label.isEmpty()) {
        QString safe = label;
        safe.replace(QRegularExpression("[^A-Za-z0-9._-]+"), "-");
        name += "_" + safe;
    }
    const QString target = QDir(folder(instance)).absoluteFilePath(name + ".zip");
    return makeShared<MMCZip::ExportToZipTask>(target, root, files);
}

Task::Ptr restore(const MinecraftInstance* instance, const QString& backupPath)
{
    return makeShared<MMCZip::ExtractZipTask>(backupPath, QDir(instance->gameRoot()));
}

bool removeWorlds(const MinecraftInstance* instance)
{
    bool ok = true;
    for (const auto& world : worldFolders(instance)) {
        ok = FS::deletePath(world) && ok;
    }
    return ok;
}

}  // namespace Server::Backups
