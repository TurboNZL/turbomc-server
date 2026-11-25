// SPDX-License-Identifier: GPL-3.0-only
#include "ServerJava.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

#include "Application.h"

namespace Server::Java {

namespace {

QList<Install> s_installed;
bool s_scanned = false;

/// "1.8.0_392" -> 8, "17.0.9" -> 17, "21" -> 21
int majorFromVersion(const QString& version)
{
    const auto parts = version.split(QRegularExpression("[._+-]"), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return 0;
    }
    int first = parts[0].toInt();
    if (first == 1 && parts.size() > 1) {
        return parts[1].toInt();
    }
    return first;
}

int detectMajor(const QString& javaExe)
{
    // the `release` file next to bin/ is the quick way
    QFileInfo real(QFileInfo(javaExe).canonicalFilePath());
    const QDir home = QDir(real.absolutePath()).filePath("..");
    QFile release(QDir::cleanPath(home.absolutePath() + "/release"));
    if (release.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression re("^JAVA_VERSION=\"?([^\"\\n]+)\"?", QRegularExpression::MultilineOption);
        const auto match = re.match(QString::fromUtf8(release.readAll()));
        if (match.hasMatch()) {
            return majorFromVersion(match.captured(1));
        }
    }
    QProcess p;
    p.start(javaExe, { "-version" });
    if (!p.waitForFinished(5000)) {
        p.kill();
        return 0;
    }
    static const QRegularExpression re("version \"([^\"]+)\"");
    const auto match = re.match(QString::fromUtf8(p.readAllStandardError()));
    return match.hasMatch() ? majorFromVersion(match.captured(1)) : 0;
}

void scan()
{
    QStringList roots;
    const QStringList patterns = { "/usr/lib/jvm", "/usr/lib64/jvm", "/usr/java", "/opt/java", "/opt", QDir::homePath() + "/.jdks",
                                   QDir::homePath() + "/.sdkman/candidates/java", APPLICATION->dataRoot() + "/java" };
    for (const QString& pattern : patterns) {
        QDir dir(pattern);
        for (const auto& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            roots << dir.filePath(entry);
        }
    }
    const QByteArray javaHome = qgetenv("JAVA_HOME");
    if (!javaHome.isEmpty()) {
        roots << QString::fromLocal8Bit(javaHome);
    }
    QStringList exes;
    for (const auto& root : roots) {
        const QString exe = QDir(root).filePath("bin/java");
        if (QFileInfo(exe).isExecutable()) {
            exes << exe;
        }
    }
    const QString onPath = QStandardPaths::findExecutable("java");
    if (!onPath.isEmpty()) {
        exes << onPath;
    }

    QSet<QString> seen;
    s_installed.clear();
    for (const auto& exe : exes) {
        const QString real = QFileInfo(exe).canonicalFilePath();
        if (real.isEmpty() || seen.contains(real)) {
            continue;
        }
        seen.insert(real);
        const int major = detectMajor(exe);
        if (major > 0) {
            s_installed.append({ exe, major });
        }
    }
    std::sort(s_installed.begin(), s_installed.end(), [](const Install& a, const Install& b) { return a.major < b.major; });
    s_scanned = true;
}

}  // namespace

const QList<Install>& installed()
{
    if (!s_scanned) {
        scan();
    }
    return s_installed;
}

void forget()
{
    s_scanned = false;
}

QString choose(int required, int* chosenMajor)
{
    const auto& all = installed();
    if (all.isEmpty()) {
        return QString();
    }
    if (required <= 0) {
        // unknown: the newest runtime is the safest guess
        if (chosenMajor) {
            *chosenMajor = all.last().major;
        }
        return all.last().path;
    }
    for (const auto& java : all) {
        if (java.major >= required) {
            if (chosenMajor) {
                *chosenMajor = java.major;
            }
            return java.path;
        }
    }
    return QString();
}

}  // namespace Server::Java
