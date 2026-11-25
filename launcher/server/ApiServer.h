// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QPointer>
#include <QSet>

class QLocalSocket;
class MinecraftInstance;
class Task;

namespace Server {

/// A local JSON API for UIs. One JSON object per line over a local socket.
///
/// Request:  {"id": 1, "method": "servers.list", "params": {...}}
/// Response: {"id": 1, "ok": true, "result": ...}  or  {"id": 1, "ok": false, "error": "text"}
/// Events (no id): {"event": "console.line", ...}, "server.state", "servers.changed", "task"
///
/// Run the application with `--serve` to start it with no window and this API listening.
class ApiServer : public QObject {
    Q_OBJECT
   public:
    explicit ApiServer(QObject* parent = nullptr);
    /// Starts listening. Returns false and fills `error` on failure.
    bool listen(QString* error);
    static QString socketPath();

   private:
    void onNewConnection();
    void onReadyRead(QLocalSocket* socket);
    void handle(QLocalSocket* socket, const QJsonObject& request);
    void dispatch(const QString& method, const QJsonObject& params, const std::function<void(QJsonValue)>& ok,
                  const std::function<void(QString)>& fail);

    void broadcast(const QJsonObject& event);
    void watchInstances();
    void watchTask(Task* task, int number, std::function<void(bool, QString)> done);

    QJsonObject describe(MinecraftInstance* instance) const;
    MinecraftInstance* find(const QJsonObject& params, QString* error) const;

    QLocalServer m_server;
    QList<QLocalSocket*> m_clients;
    QHash<QLocalSocket*, QByteArray> m_buffers;
    QSet<QString> m_watched;
    int m_nextTask = 1;
};

}  // namespace Server
