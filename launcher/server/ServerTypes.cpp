// SPDX-License-Identifier: GPL-3.0-only
#include "ServerTypes.h"

namespace Server {

QList<Software> allSoftware()
{
    return { Software::Vanilla, Software::Paper, Software::Purpur, Software::Fabric };
}

QString softwareKey(Software software)
{
    switch (software) {
        case Software::Vanilla:
            return "vanilla";
        case Software::Paper:
            return "paper";
        case Software::Purpur:
            return "purpur";
        case Software::Fabric:
            return "fabric";
    }
    return "vanilla";
}

QString softwareName(Software software)
{
    switch (software) {
        case Software::Vanilla:
            return "Vanilla";
        case Software::Paper:
            return "Paper";
        case Software::Purpur:
            return "Purpur";
        case Software::Fabric:
            return "Fabric";
    }
    return "Vanilla";
}

Software softwareFromKey(const QString& key)
{
    for (auto software : allSoftware()) {
        if (softwareKey(software) == key) {
            return software;
        }
    }
    return Software::Vanilla;
}

ModPlatform::ModLoaderTypes modLoaders(Software software)
{
    switch (software) {
        case Software::Vanilla:
            return ModPlatform::ModLoaderTypes(ModPlatform::DataPack);
        case Software::Paper:
            return ModPlatform::Paper | ModPlatform::Spigot | ModPlatform::Bukkit | ModPlatform::DataPack;
        case Software::Purpur:
            return ModPlatform::Purpur | ModPlatform::Paper | ModPlatform::Spigot | ModPlatform::Bukkit | ModPlatform::DataPack;
        case Software::Fabric:
            return ModPlatform::Fabric | ModPlatform::DataPack;
    }
    return {};
}

bool supportsAddons(Software software)
{
    return software != Software::Vanilla;
}

}  // namespace Server
