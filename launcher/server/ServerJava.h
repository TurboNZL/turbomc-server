// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>

namespace Server::Java {

struct Install {
    QString path;  ///< the `java` executable
    int major = 0;
};

/// Java runtimes found on this computer (looked up once, then remembered).
const QList<Install>& installed();
void forget();

/// Picks the runtime for a server that needs `required` (0 = unknown): the exact version if present, otherwise the next
/// newer one. Returns an empty string when none fits.
QString choose(int required, int* chosenMajor = nullptr);

}  // namespace Server::Java
