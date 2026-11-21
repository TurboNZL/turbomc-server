// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Server {

/// A `server.properties` file. Comments, blank lines and the order of keys survive a load/save round trip.
class Properties {
   public:
    bool load(const QString& path);
    bool save(const QString& path) const;

    bool contains(const QString& key) const;
    QString value(const QString& key, const QString& fallback = QString()) const;
    void set(const QString& key, const QString& value);
    void remove(const QString& key);

    /// All key/value pairs in file order.
    QList<QPair<QString, QString>> entries() const;

    /// Short human description for well-known keys (empty when unknown).
    static QString describe(const QString& key);

   private:
    struct Line {
        QString raw;  // used when key is empty (comment / blank)
        QString key;
        QString value;
    };
    QList<Line> m_lines;
};

}  // namespace Server
