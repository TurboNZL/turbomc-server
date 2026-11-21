// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>

#include "MessageLevel.h"
#include "QObjectPtr.h"
#include "launch/LogModel.h"

class MinecraftInstance;

namespace Server {

/// Runs one Minecraft server as a child process and exposes its console.
class Process : public QObject {
    Q_OBJECT
   public:
    enum class State { Stopped, Starting, Running, Stopping };
    Q_ENUM(State)

    explicit Process(MinecraftInstance* instance, QObject* parent = nullptr);
    ~Process() override;

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Stopped; }

    /// Whether `eula.txt` in the server folder says eula=true.
    static bool eulaAccepted(const QString& serverRoot);
    static bool acceptEula(const QString& serverRoot);

    /// Starts the server. Returns false and fills `error` when it cannot (no jar, EULA not accepted, no Java ...).
    bool start(QString* error);
    /// Asks the server to stop (sends "stop"); kills it if it does not exit in time.
    void stop();
    void kill();
    void sendCommand(const QString& command);

    /// Everything the server (and this manager) printed; survives restarts of the server.
    shared_qobject_ptr<LogModel> logModel() const { return m_log; }
    /// Appends a line to the log and notifies listeners.
    void log(const QString& text, MessageLevel level);

    QStringList javaArguments() const;
    QString javaPath() const;

   signals:
    void line(const QString& text, MessageLevel level);
    void stateChanged(Process::State state);
    /// Emitted once the process has exited. `crashed` is true when it exited without being asked to.
    void exited(int exitCode, bool crashed);

   private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);

   private:
    void setState(State state);
    void handleLine(QString text);

    MinecraftInstance* m_instance;
    QProcess m_process;
    shared_qobject_ptr<LogModel> m_log;
    QTimer m_killTimer;
    State m_state = State::Stopped;
    QByteArray m_buffer;
    bool m_stopRequested = false;
};

}  // namespace Server
