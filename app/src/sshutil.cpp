#include "sshutil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>

namespace sshutil {

QString tool(const QString& name) {
#ifdef Q_OS_WIN
    const QString exe = name + ".exe";
    const QString local = QCoreApplication::applicationDirPath() + "/" + exe;
    if (QFileInfo::exists(local)) return local;
    return exe;
#else
    return name;
#endif
}

QString hostsPath() {
#ifdef Q_OS_WIN
    QString root = qEnvironmentVariable("SystemRoot");
    if (root.isEmpty()) root = QStringLiteral("C:/Windows");
    return root + "/System32/drivers/etc/hosts";
#else
    return QStringLiteral("/etc/hosts");
#endif
}

QString askpassPath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    const QString p = dir + "/srm-askpass.sh";
    QFile f(p);
    if (!f.exists()) {
        if (f.open(QIODevice::WriteOnly)) {
            f.write("#!/bin/sh\nprintf '%s\\n' \"$SRM_PASS\"\n");
            f.close();
            f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
    }
    return p;
}

QProcessEnvironment env(const Settings& s) {
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    // Windows OpenSSH не использует SSH_ASKPASS — авторизация по ключу.
    Q_UNUSED(s);
#else
    if (s.usePassword && !s.password.isEmpty()) {
        e.insert("SRM_PASS", s.password);
        e.insert("SSH_ASKPASS", askpassPath());
        e.insert("SSH_ASKPASS_REQUIRE", "force");
        if (!e.contains("DISPLAY")) e.insert("DISPLAY", ":0");
    }
#endif
    return e;
}

QStringList baseArgs(const Settings& s) {
    QStringList a;
    a << "-o" << "StrictHostKeyChecking=accept-new"
      << "-o" << "ConnectTimeout=15"
      << "-o" << "ServerAliveInterval=15";
    a << "-p" << (s.port.trimmed().isEmpty() ? QString("22") : s.port.trimmed());
    if (!s.keyPath.trimmed().isEmpty()) a << "-i" << s.keyPath.trimmed();
    if (s.usePassword && s.keyPath.trimmed().isEmpty())
        a << "-o" << "PreferredAuthentications=password" << "-o" << "PubkeyAuthentication=no";
    QString target = s.host.trimmed();
    if (!target.contains('@')) target = s.user.trimmed() + "@" + target;
    a << target;
    return a;
}

void prepare(const Settings& s, QString& program, QStringList& args) {
    args.clear();
#ifdef Q_OS_WIN
    program = tool("ssh");
#else
    const bool pw = s.usePassword && !s.password.isEmpty();
    if (pw) { program = "setsid"; args << "-w" << "ssh"; }
    else    { program = "ssh"; }
#endif
    args += baseArgs(s);
    args << "bash" << "-s";
}

} // namespace sshutil
