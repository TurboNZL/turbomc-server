// SPDX-License-Identifier: GPL-3.0-only
#include "ApiServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>

#include "Application.h"
#include "BuildConfig.h"
#include "InstanceList.h"
#include "launch/LogModel.h"
#include "minecraft/MinecraftInstance.h"
#include "server/ServerBackup.h"
#include "server/ServerCreationTask.h"
#include "server/ServerLookup.h"
#include "server/ServerProcess.h"
#include "server/ServerProperties.h"
#include "server/ServerTypes.h"
#include "tasks/Task.h"

namespace Server {

namespace {

QString stateName(Process::State state)
{
    switch (state) {
        case Process::State::Starting:
            return "starting";
        case Process::State::Running:
            return "running";
        case Process::State::Stopping:
            return "stopping";
        default:
            return "stopped";
    }
}

QString levelName(MessageLevel level)
{
    switch (level.value()) {
        case MessageLevelValue::Error:
        case MessageLevelValue::Fatal:
        case MessageLevelValue::StdErr:
            return "error";
        case MessageLevelValue::Warning:
            return "warn";
        case MessageLevelValue::Debug:
        case MessageLevelValue::Trace:
            return "debug";
        case MessageLevelValue::Launcher:
            return "manager";
        default:
            return "info";
    }
}

QString propertiesPath(MinecraftInstance* instance)
{
    return QDir(instance->gameRoot()).filePath("server.properties");
}

}  // namespace

ApiServer::ApiServer(QObject* parent) : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &ApiServer::onNewConnection);
}

QString ApiServer::socketPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) {
        dir = QDir::tempPath();
    }
    return QDir(dir).filePath("turbomc-server.sock");
}

bool ApiServer::listen(QString* error)
{
    QLocalServer::removeServer(socketPath());
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(socketPath())) {
        if (error) {
            *error = m_server.errorString();
        }
        return false;
    }
    watchInstances();
    connect(APPLICATION->instances(), &InstanceList::instancesChanged, this, [this] {
        watchInstances();
        broadcast({ { "event", "servers.changed" } });
    });
    return true;
}

void ApiServer::onNewConnection()
{
    while (auto* socket = m_server.nextPendingConnection()) {
        m_clients.append(socket);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_clients.removeAll(socket);
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void ApiServer::onReadyRead(QLocalSocket* socket)
{
    auto& buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray lineData = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (lineData.trimmed().isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(lineData, &parseError);
        if (!doc.isObject()) {
            QJsonObject reply{ { "ok", false }, { "error", "request is not a JSON object" } };
            socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + "\n");
            continue;
        }
        handle(socket, doc.object());
    }
}

void ApiServer::handle(QLocalSocket* socket, const QJsonObject& request)
{
    const QJsonValue id = request.value("id");
    QPointer<QLocalSocket> guard(socket);
    auto send = [guard](QJsonObject reply) {
        if (guard) {
            guard->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + "\n");
        }
    };
    dispatch(
        request.value("method").toString(), request.value("params").toObject(),
        [id, send](QJsonValue result) { send({ { "id", id }, { "ok", true }, { "result", result } }); },
        [id, send](QString message) { send({ { "id", id }, { "ok", false }, { "error", message } }); });
}

void ApiServer::broadcast(const QJsonObject& event)
{
    const QByteArray data = QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n";
    for (auto* client : m_clients) {
        client->write(data);
    }
}

QJsonObject ApiServer::describe(MinecraftInstance* instance) const
{
    auto* process = instance->serverProcess();
    const auto software = softwareFromKey(instance->settings()->get("ServerSoftware").toString());
    return {
        { "id", instance->id() },
        { "name", instance->name() },
        { "software", softwareKey(software) },
        { "softwareName", softwareName(software) },
        { "version", instance->settings()->get("ServerMcVersion").toString() },
        { "state", stateName(process->state()) },
        { "group", APPLICATION->instances()->getInstanceGroup(instance->id()) },
        { "path", instance->gameRoot() },
        { "eula", Process::eulaAccepted(instance->gameRoot()) },
        { "addons", supportsAddons(software) },
    };
}

MinecraftInstance* ApiServer::find(const QJsonObject& params, QString* error) const
{
    const QString id = params.value("id").toString();
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance && error) {
        *error = QString("no server with id \"%1\"").arg(id);
    }
    return instance;
}

void ApiServer::watchInstances()
{
    auto list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        auto* instance = list->at(i);
        if (!instance || m_watched.contains(instance->id())) {
            continue;
        }
        m_watched.insert(instance->id());
        const QString id = instance->id();
        auto* process = instance->serverProcess();
        connect(process, &Process::line, this, [this, id](const QString& text, MessageLevel level) {
            broadcast({ { "event", "console.line" }, { "server", id }, { "text", text }, { "level", levelName(level) } });
        });
        connect(process, &Process::stateChanged, this, [this, id](Process::State state) {
            broadcast({ { "event", "server.state" }, { "server", id }, { "state", stateName(state) } });
        });
        connect(process, &Process::exited, this, [this, id](int code, bool crashed) {
            broadcast({ { "event", "server.exited" }, { "server", id }, { "code", code }, { "crashed", crashed } });
        });
    }
}

void ApiServer::watchTask(Task* task, int number, std::function<void(bool, QString)> done)
{
    connect(task, &Task::progress, this, [this, number, task](qint64 current, qint64 total) {
        broadcast(QJsonObject{ { "event", "task" }, { "task", number }, { "state", "running" },
                               { "progress", total > 0 ? double(current) / double(total) : 0.0 }, { "status", task->getStatus() } });
    });
    connect(task, &Task::status, this, [this, number](const QString& status) {
        broadcast({ { "event", "task" }, { "task", number }, { "state", "running" }, { "status", status } });
    });
    connect(task, &Task::succeeded, this, [this, number, task, done] {
        broadcast({ { "event", "task" }, { "task", number }, { "state", "done" } });
        if (done) {
            done(true, QString());
        }
        task->deleteLater();
    });
    connect(task, &Task::failed, this, [this, number, task, done](const QString& reason) {
        broadcast({ { "event", "task" }, { "task", number }, { "state", "failed" }, { "error", reason } });
        if (done) {
            done(false, reason);
        }
        task->deleteLater();
    });
    connect(task, &Task::aborted, this, [this, number, task, done] {
        broadcast({ { "event", "task" }, { "task", number }, { "state", "failed" }, { "error", "cancelled" } });
        if (done) {
            done(false, "cancelled");
        }
        task->deleteLater();
    });
}

void ApiServer::dispatch(const QString& method, const QJsonObject& params, const std::function<void(QJsonValue)>& ok,
                         const std::function<void(QString)>& fail)
{
    QString error;

    if (method == "hello") {
        ok(QJsonObject{ { "name", "turbomc-server" }, { "protocol", 1 }, { "dataDir", APPLICATION->dataRoot() } });
        return;
    }

    if (method == "software.list") {
        QJsonArray out;
        for (auto software : allSoftware()) {
            out.append(QJsonObject{ { "key", softwareKey(software) }, { "name", softwareName(software) } });
        }
        ok(out);
        return;
    }

    if (method == "software.versions") {
        auto* task = new VersionsTask(softwareFromKey(params.value("software").toString()), params.value("unstable").toBool());
        connect(task, &Task::succeeded, this, [task, ok] {
            QJsonArray out;
            for (const auto& version : task->versions()) {
                out.append(QJsonObject{ { "id", version.id }, { "stable", version.stable }, { "time", version.releaseTime.toString(Qt::ISODate) } });
            }
            ok(out);
            task->deleteLater();
        });
        connect(task, &Task::failed, this, [task, fail](const QString& reason) {
            fail(reason);
            task->deleteLater();
        });
        task->start();
        return;
    }

    if (method == "servers.list") {
        QJsonArray out;
        auto list = APPLICATION->instances();
        for (int i = 0; i < list->count(); ++i) {
            if (auto* instance = list->at(i)) {
                out.append(describe(instance));
            }
        }
        ok(out);
        return;
    }

    if (method == "servers.create") {
        const QString name = params.value("name").toString().trimmed();
        const QString version = params.value("version").toString();
        if (name.isEmpty() || version.isEmpty()) {
            fail("name and version are required");
            return;
        }
        auto* creation = new CreationTask(softwareFromKey(params.value("software").toString("vanilla")), version);
        creation->setName(name);
        creation->setGroup(params.value("group").toString());
        creation->setIcon("default");
        Task* task = APPLICATION->instances()->wrapInstanceTask(creation);
        const int number = m_nextTask++;
        watchTask(task, number, nullptr);
        ok(QJsonObject{ { "task", number } });
        task->start();
        return;
    }

    // everything below needs an existing server
    auto* instance = find(params, &error);
    if (!instance) {
        fail(error);
        return;
    }
    auto* process = instance->serverProcess();

    if (method == "servers.get") {
        ok(describe(instance));
    } else if (method == "servers.start") {
        QString why;
        if (process->state() != Process::State::Stopped) {
            fail("the server is already running");
        } else if (process->start(&why)) {
            ok(QJsonObject{});
        } else {
            fail(why);  // "eula" means the UI should ask the user and call servers.acceptEula
        }
    } else if (method == "servers.acceptEula") {
        if (Process::acceptEula(instance->gameRoot())) {
            ok(QJsonObject{});
        } else {
            fail("could not write eula.txt");
        }
    } else if (method == "servers.stop") {
        process->stop();
        ok(QJsonObject{});
    } else if (method == "servers.kill") {
        process->kill();
        ok(QJsonObject{});
    } else if (method == "servers.command") {
        process->sendCommand(params.value("text").toString());
        ok(QJsonObject{});
    } else if (method == "servers.console") {
        auto model = process->logModel();
        const int rows = model->rowCount();
        const int first = qMax(0, rows - params.value("max").toInt(1000));
        QJsonArray out;
        for (int i = first; i < rows; ++i) {
            const auto index = model->index(i);
            out.append(QJsonObject{ { "text", model->data(index, Qt::DisplayRole).toString() },
                                    { "level", levelName(MessageLevel(static_cast<MessageLevelValue>(model->data(index, LogModel::LevelRole).toInt()))) } });
        }
        ok(out);
    } else if (method == "servers.rename") {
        instance->setName(params.value("name").toString());
        broadcast({ { "event", "servers.changed" } });
        ok(QJsonObject{});
    } else if (method == "servers.delete") {
        if (process->state() != Process::State::Stopped) {
            fail("stop the server first");
        } else {
            APPLICATION->instances()->deleteInstance(instance->id());
            ok(QJsonObject{});
        }
    } else if (method == "servers.properties.get") {
        Properties props;
        props.load(propertiesPath(instance));
        QJsonArray out;
        for (const auto& entry : props.entries()) {
            out.append(QJsonObject{ { "key", entry.first }, { "value", entry.second }, { "description", Properties::describe(entry.first) } });
        }
        ok(out);
    } else if (method == "servers.properties.set") {
        Properties props;
        props.load(propertiesPath(instance));
        const auto values = params.value("values").toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            props.set(it.key(), it.value().toString());
        }
        if (props.save(propertiesPath(instance))) {
            ok(QJsonObject{});
        } else {
            fail("could not save server.properties");
        }
    } else if (method == "servers.backups.list") {
        QJsonArray out;
        for (const auto& entry : Backups::list(instance)) {
            out.append(QJsonObject{ { "path", entry.path }, { "name", entry.name }, { "time", entry.time.toString(Qt::ISODate) }, { "size", double(entry.size) } });
        }
        ok(out);
    } else if (method == "servers.backups.create") {
        auto task = Backups::create(instance, params.value("label").toString(), &error);
        if (!task) {
            fail(error);
            return;
        }
        // the task is shared; keep it alive until it finishes
        connect(task.get(), &Task::succeeded, this, [task, ok]() mutable { ok(QJsonObject{}); task.reset(); });
        connect(task.get(), &Task::failed, this, [task, fail](const QString& reason) mutable { fail(reason); task.reset(); });
        task->start();
    } else if (method == "servers.backups.restore") {
        const QString path = params.value("path").toString();
        if (process->state() != Process::State::Stopped) {
            fail("stop the server first");
            return;
        }
        if (Backups::hasWorld(instance)) {
            auto safety = Backups::create(instance, "before-restore", &error);
            if (!safety) {
                fail(error);
                return;
            }
            connect(safety.get(), &Task::succeeded, this, [this, safety, instance, path, ok, fail]() mutable {
                safety.reset();
                if (!Backups::removeWorlds(instance)) {
                    fail("could not remove the current world");
                    return;
                }
                auto restore = Backups::restore(instance, path);
                connect(restore.get(), &Task::succeeded, this, [restore, ok]() mutable { ok(QJsonObject{}); restore.reset(); });
                connect(restore.get(), &Task::failed, this, [restore, fail](const QString& r) mutable { fail(r); restore.reset(); });
                restore->start();
            });
            connect(safety.get(), &Task::failed, this, [safety, fail](const QString& r) mutable { fail("could not back up the current world first: " + r); safety.reset(); });
            safety->start();
        } else {
            auto restore = Backups::restore(instance, path);
            connect(restore.get(), &Task::succeeded, this, [restore, ok]() mutable { ok(QJsonObject{}); restore.reset(); });
            connect(restore.get(), &Task::failed, this, [restore, fail](const QString& r) mutable { fail(r); restore.reset(); });
            restore->start();
        }
    } else if (method == "servers.backups.delete") {
        if (QFile::remove(params.value("path").toString())) {
            ok(QJsonObject{});
        } else {
            fail("could not delete the backup");
        }
    } else {
        fail(QString("unknown method \"%1\"").arg(method));
    }
}

}  // namespace Server
