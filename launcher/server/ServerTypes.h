// SPDX-License-Identifier: GPL-3.0-only
/*
 *  TurboMC Server - Minecraft server manager
 *  Based on Prism Launcher (Copyright (C) 2022-2026 Prism Launcher Contributors)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include "modplatform/ModIndex.h"

namespace Server {

/// Server software we know how to install and run.
enum class Software { Vanilla, Paper, Purpur, Fabric };

QList<Software> allSoftware();
QString softwareKey(Software software);   ///< stable id stored in instance.cfg ("vanilla", "paper", ...)
QString softwareName(Software software);  ///< display name
Software softwareFromKey(const QString& key);

/// Mod/plugin loaders the software accepts; used to filter Modrinth/CurseForge searches.
ModPlatform::ModLoaderTypes modLoaders(Software software);

/// True for software that loads plugins/mods at all (everything but vanilla).
bool supportsAddons(Software software);

struct VersionEntry {
    QString id;
    bool stable = true;
    QDateTime releaseTime;
};

struct Download {
    QString url;
    QString fileName;
    QString sha1;
    QString sha256;
    QString build;      ///< software build / loader version, may be empty
    int javaMajor = 0;  ///< 0 = unknown
};

}  // namespace Server
