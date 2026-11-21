// SPDX-License-Identifier: GPL-3.0-only
#include "ServerProperties.h"

#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QSaveFile>
#include <QTextStream>

namespace Server {

bool Properties::load(const QString& path)
{
    m_lines.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString text = in.readLine();
        Line line;
        const QString trimmed = text.trimmed();
        const auto eq = text.indexOf('=');
        if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith('!') || eq < 0) {
            line.raw = text;
        } else {
            line.key = text.left(eq).trimmed();
            line.value = text.mid(eq + 1);
        }
        m_lines.append(line);
    }
    return true;
}

bool Properties::save(const QString& path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    for (const auto& line : m_lines) {
        if (line.key.isEmpty()) {
            out << line.raw << '\n';
        } else {
            out << line.key << '=' << line.value << '\n';
        }
    }
    out.flush();
    return file.commit();
}

bool Properties::contains(const QString& key) const
{
    for (const auto& line : m_lines) {
        if (line.key == key) {
            return true;
        }
    }
    return false;
}

QString Properties::value(const QString& key, const QString& fallback) const
{
    for (const auto& line : m_lines) {
        if (line.key == key) {
            return line.value;
        }
    }
    return fallback;
}

void Properties::set(const QString& key, const QString& value)
{
    for (auto& line : m_lines) {
        if (line.key == key) {
            line.value = value;
            return;
        }
    }
    Line line;
    line.key = key;
    line.value = value;
    m_lines.append(line);
}

void Properties::remove(const QString& key)
{
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].key == key) {
            m_lines.removeAt(i);
            return;
        }
    }
}

QList<QPair<QString, QString>> Properties::entries() const
{
    QList<QPair<QString, QString>> out;
    for (const auto& line : m_lines) {
        if (!line.key.isEmpty()) {
            out.append({ line.key, line.value });
        }
    }
    return out;
}

QString Properties::describe(const QString& key)
{
    static const QMap<QString, const char*> known = {
        { "motd", QT_TRANSLATE_NOOP("Server::Properties", "The message shown under the server name in the multiplayer list.") },
        { "server-port", QT_TRANSLATE_NOOP("Server::Properties", "The TCP port players connect to (default 25565). Must be unique on this computer.") },
        { "max-players", QT_TRANSLATE_NOOP("Server::Properties", "How many players can be online at once.") },
        { "difficulty", QT_TRANSLATE_NOOP("Server::Properties", "peaceful, easy, normal or hard.") },
        { "gamemode", QT_TRANSLATE_NOOP("Server::Properties", "Default game mode: survival, creative, adventure or spectator.") },
        { "online-mode", QT_TRANSLATE_NOOP("Server::Properties", "true checks players' Minecraft accounts. Turn off only behind a proxy or for offline play (needed for some crossplay setups).") },
        { "white-list", QT_TRANSLATE_NOOP("Server::Properties", "true lets only whitelisted players join.") },
        { "enforce-whitelist", QT_TRANSLATE_NOOP("Server::Properties", "true kicks players who are not whitelisted when the whitelist is turned on.") },
        { "pvp", QT_TRANSLATE_NOOP("Server::Properties", "true lets players damage each other.") },
        { "view-distance", QT_TRANSLATE_NOOP("Server::Properties", "How many chunks around each player the server sends. Lower values save CPU and bandwidth.") },
        { "simulation-distance", QT_TRANSLATE_NOOP("Server::Properties", "How many chunks around each player are simulated (mobs, crops).") },
        { "level-name", QT_TRANSLATE_NOOP("Server::Properties", "The folder name of the world. Changing it makes the server create or load another world.") },
        { "level-seed", QT_TRANSLATE_NOOP("Server::Properties", "Seed used when a new world is generated.") },
        { "allow-flight", QT_TRANSLATE_NOOP("Server::Properties", "Whether players are allowed to fly (needed by some mods).") },
        { "spawn-protection", QT_TRANSLATE_NOOP("Server::Properties", "Radius around spawn that non-operators cannot build in.") },
        { "enable-command-block", QT_TRANSLATE_NOOP("Server::Properties", "true allows command blocks.") },
        { "hardcore", QT_TRANSLATE_NOOP("Server::Properties", "true bans players who die.") },
        { "server-ip", QT_TRANSLATE_NOOP("Server::Properties", "Leave empty unless you know you need to bind to one network address.") },
        { "enable-rcon", QT_TRANSLATE_NOOP("Server::Properties", "Enables the remote console. Use a strong password if you turn it on.") },
        { "enable-query", QT_TRANSLATE_NOOP("Server::Properties", "Enables the GameSpy4 query protocol (server info for list sites).") },
        { "max-world-size", QT_TRANSLATE_NOOP("Server::Properties", "Maximum world radius in blocks.") },
    };
    const auto it = known.constFind(key);
    return it == known.constEnd() ? QString() : QCoreApplication::translate("Server::Properties", it.value());
}

}  // namespace Server
