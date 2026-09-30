#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <QFile>
#include <QProcess>
#include <QIcon>

#include "mainwindow.h"
#include "generator.h"
#include "settings.h"
#include "sshutil.h"
#include "version.h"

int main(int argc, char** argv) {
    // CLI-режимы (без GUI): печать генерируемых файлов для проверки/скриптов.
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--version" || a == "-v") {
            QCoreApplication app(argc, argv);
            QTextStream out(stdout);
            out << "sni-relay-manager " << SRM_VERSION << "\n";
            return 0;
        }
        if (a == "--print-config-path") {
            QCoreApplication app(argc, argv);
            QCoreApplication::setApplicationName("sni-relay-manager");
            QTextStream out(stdout);
            out << Settings::configPath() << "\n";
            return 0;
        }
        if (a == "--ssh-test") {
            QCoreApplication app(argc, argv);
            QCoreApplication::setApplicationName("sni-relay-manager");
            Settings s = Settings::load();
            QString program; QStringList args;
            sshutil::prepare(s, program, args);
            QProcess p;
            p.setProcessEnvironment(sshutil::env(s));
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(program, args);
            p.write("echo CONNECTED; hostname; nginx -v 2>&1\n");
            p.closeWriteChannel();
            p.waitForFinished(30000);
            QTextStream out(stdout);
            out << QString::fromLocal8Bit(p.readAll());
            return p.exitCode();
        }
        if (a == "--print-config" || a == "--print-deploy" ||
            a == "--print-rollback" || a == "--print-hosts") {
            QCoreApplication app(argc, argv);
            QCoreApplication::setApplicationName("sni-relay-manager");
            Settings s = Settings::load();
            if (s.domains.isEmpty()) s.domains = Settings::defaultDomains();
            for (int j = 1; j < argc; ++j) {
                const QString b = QString::fromLocal8Bit(argv[j]);
                if (b.startsWith("--host=")) s.host = b.mid(7);
            }
            QTextStream out(stdout);
            if (a == "--print-config") out << gen::relayConf(s);
            else if (a == "--print-deploy") out << gen::deployScript(s);
            else if (a == "--print-rollback") out << gen::rollbackScript(s);
            else { // --print-hosts
                QString cur;
                QFile f("/etc/hosts");
                if (f.open(QIODevice::ReadOnly)) cur = QString::fromLocal8Bit(f.readAll());
                out << gen::hostsFile(s, true, cur);
            }
            return 0;
        }
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("sni-relay-manager");
    QCoreApplication::setApplicationVersion(SRM_VERSION);
    // Связь окна с .desktop и иконкой (Wayland/Hyprland, панели задач).
    QGuiApplication::setDesktopFileName("sni-relay-manager");
    QIcon ic = QIcon::fromTheme("sni-relay-manager");
    if (ic.isNull()) ic = QIcon("/usr/share/icons/hicolor/scalable/apps/sni-relay-manager.svg");
    if (ic.isNull()) ic = QIcon(":/sni-relay-manager.svg");
    app.setWindowIcon(ic);
    MainWindow w;
    w.show();
    return app.exec();
}
