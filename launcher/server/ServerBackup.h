// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include "tasks/Task.h"

class MinecraftInstance;

namespace Server::Backups {

struct Entry {
    QString path;
    QString name;
    QDateTime time;
    qint64 size = 0;
};

/// `<instance>/backups`
QString folder(const MinecraftInstance* instance);
/// level-name from server.properties ("world" when not set).
QString levelName(const MinecraftInstance* instance);
/// The world folders that exist right now (overworld and the old-style nether/end folders).
QStringList worldFolders(const MinecraftInstance* instance);
bool hasWorld(const MinecraftInstance* instance);

/// Newest first.
QList<Entry> list(const MinecraftInstance* instance);

/// Zips the world(s) plus the small config files (server.properties, ops, whitelist, bans).
/// Returns null (and fills `error`) when there is nothing to back up.
Task::Ptr create(const MinecraftInstance* instance, const QString& label, QString* error);

/// Unpacks a backup into the server folder. Call `removeWorlds` first for an exact restore.
Task::Ptr restore(const MinecraftInstance* instance, const QString& backupPath);

/// Deletes the current world folders (used right before restoring).
bool removeWorlds(const MinecraftInstance* instance);

}  // namespace Server::Backups
