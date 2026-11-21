// SPDX-License-Identifier: GPL-3.0-only
#include "ServerProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "Commandline.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

namespace Server {

namespace {
constexpr int STOP_GRACE_MS = 45 * 1000;
}

Process::Process(MinecraftInstance* instance, QObject* parent) : QObject(parent), m_instance(instance)
{
    m_log.reset(new LogModel());
    m_log->setMaxLines(5000);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &Process::onReadyRead);
    connect(&m_process, &QProcess::finished, this, &Process::onFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &Process::onError);
    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        log(tr("The server did not stop in time, killing it."), MessageLevel::Warning);
        kill();
    });
}

Process::~Process()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}

bool Process::eulaAccepted(const QString& serverRoot)
{
    QFile f(QDir(serverRoot).absoluteFilePath("eula.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    static const QRegularExpression accepted("^\\s*eula\\s*=\\s*true\\s*$", QRegularExpression::MultilineOption);
    return accepted.match(QString::fromUtf8(f.readAll())).hasMatch();
}

bool Process::acceptEula(const QString& serverRoot)
{
    if (!FS::ensureFolderPathExists(serverRoot)) {
        return false;
    }
    QFile f(QDir(serverRoot).absoluteFilePath("eula.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    f.write("# Accepted in TurboMC Server (https://aka.ms/MinecraftEULA)\neula=true\n");
    return true;
}

QString Process::javaPath() const
{
    const QString configured = m_instance->settings()->get("JavaPath").toString();
    return configured.isEmpty() ? QStringLiteral("java") : configured;
}

QStringList Process::javaArguments() const
{
    QStringList args;
    const int minMem = m_instance->settings()->get("MinMemAlloc").toInt();
    const int maxMem = m_instance->settings()->get("MaxMemAlloc").toInt();
    if (minMem > 0) {
        args << QString("-Xms%1M").arg(minMem);
    }
    if (maxMem > 0) {
        args << QString("-Xmx%1M").arg(maxMem);
    }
    args << Commandline::splitArgs(m_instance->settings()->get("JvmArgs").toString());
    args << "-jar" << "server.jar" << "nogui";
    return args;
}

bool Process::start(QString* error)
{
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (m_state != State::Stopped) {
        return fail(tr("The server is already running."));
    }
    const QString root = m_instance->gameRoot();
    if (!QFileInfo::exists(QDir(root).absoluteFilePath("server.jar"))) {
        return fail(tr("There is no server.jar yet. Install a server version first."));
    }
    if (!eulaAccepted(root)) {
        return fail("eula");
    }

    m_stopRequested = false;
    m_buffer.clear();
    m_process.setWorkingDirectory(root);
    m_process.setProgram(javaPath());
    m_process.setArguments(javaArguments());
    m_process.setProcessEnvironment(m_instance->createEnvironment());

    log(tr("> %1 %2").arg(javaPath(), javaArguments().join(' ')), MessageLevel::Launcher);
    setState(State::Starting);
    m_process.start();
    return true;
}

void Process::stop()
{
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }
    m_stopRequested = true;
    setState(State::Stopping);
    sendCommand("stop");
    m_killTimer.start(STOP_GRACE_MS);
}

void Process::kill()
{
    m_stopRequested = true;
    m_process.kill();
}

void Process::sendCommand(const QString& command)
{
    if (m_process.state() != QProcess::Running) {
        return;
    }
    m_process.write(command.toUtf8() + '\n');
}

void Process::onReadyRead()
{
    m_buffer += m_process.readAll();
    qsizetype nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QString text = QString::fromUtf8(m_buffer.left(nl));
        m_buffer.remove(0, nl + 1);
        if (text.endsWith('\r')) {
            text.chop(1);
        }
        handleLine(std::move(text));
    }
}

void Process::handleLine(QString text)
{
    MessageLevel level = MessageLevel::Info;
    if (text.contains("/WARN]") || text.contains("WARNING")) {
        level = MessageLevel::Warning;
    } else if (text.contains("/ERROR]") || text.contains("/FATAL]") || text.startsWith("Exception") || text.startsWith("\tat ")) {
        level = MessageLevel::Error;
    }
    if (m_state == State::Starting && text.contains(QRegularExpression("Done \\([0-9.,]+s\\)!"))) {
        setState(State::Running);
    }
    log(text, level);
}

void Process::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_killTimer.stop();
    if (!m_buffer.isEmpty()) {
        handleLine(QString::fromUtf8(m_buffer));
        m_buffer.clear();
    }
    const bool crashed = !m_stopRequested && (exitCode != 0 || status == QProcess::CrashExit);
    log(crashed ? tr("The server stopped unexpectedly (exit code %1).").arg(exitCode)
                      : tr("The server stopped (exit code %1).").arg(exitCode),
              crashed ? MessageLevel::Fatal : MessageLevel::Launcher);
    setState(State::Stopped);
    emit exited(exitCode, crashed);
}

void Process::onError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        log(tr("Could not start Java (%1). Check the Java path in the settings.").arg(javaPath()), MessageLevel::Fatal);
        setState(State::Stopped);
        emit exited(-1, true);
    }
}

void Process::log(const QString& text, MessageLevel level)
{
    m_log->append(level, text);
    emit line(text, level);
}

void Process::setState(State state)
{
    if (state == m_state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

}  // namespace Server
