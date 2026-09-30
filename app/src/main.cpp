#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <QFile>
#include <QProcess>
#include <QIcon>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>

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

    // Если системная тема не подхватилась (напр. AppImage без platform-theme
    // плагина) и палитра светлая — включаем тёмную (Fusion).
    if (app.palette().color(QPalette::Window).lightness() > 128) {
        app.setStyle(QStyleFactory::create("Fusion"));
        QPalette dp;
        dp.setColor(QPalette::Window, QColor(53, 53, 53));
        dp.setColor(QPalette::WindowText, Qt::white);
        dp.setColor(QPalette::Base, QColor(35, 35, 35));
        dp.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        dp.setColor(QPalette::ToolTipBase, QColor(35, 35, 35));
        dp.setColor(QPalette::ToolTipText, Qt::white);
        dp.setColor(QPalette::Text, Qt::white);
        dp.setColor(QPalette::Button, QColor(53, 53, 53));
        dp.setColor(QPalette::ButtonText, Qt::white);
        dp.setColor(QPalette::BrightText, Qt::red);
        dp.setColor(QPalette::Link, QColor(42, 130, 218));
        dp.setColor(QPalette::Highlight, QColor(42, 130, 218));
        dp.setColor(QPalette::HighlightedText, Qt::black);
        dp.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
        dp.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
        app.setPalette(dp);
    }

    MainWindow w;
    w.show();
    return app.exec();
}
