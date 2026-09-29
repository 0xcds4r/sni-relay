#pragma once

// Настройки приложения. Хранятся в JSON (~/.config/sni-relay-manager/config.json).

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

struct Settings {
    // --- VDS / SSH ---
    QString host;
    QString port = "22";
    QString user = "root";
    QString keyPath;                 // путь к приватному ключу (опционально)
    bool usePassword = false;        // использовать пароль
    bool savePassword = false;       // сохранять пароль в конфиг (небезопасно)
    QString password;                // в памяти / при savePassword

    // --- релей ---
    QString relayExit = "127.0.0.1:9443";   // куда уходит релеенный трафик
    QString siteBackend = "127.0.0.1:8443"; // куда идёт локальный сайт
    bool hasSite = true;                    // на VDS есть сайт на 443
    QStringList siteDomains;                // SNI локального сайта
    QStringList domains;                    // домены, которые релеим

    static QString configPath() {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (dir.isEmpty()) dir = QDir::homePath() + "/.config/sni-relay-manager";
        return dir + "/config.json";
    }

    static QStringList defaultDomains() {
        return {
            "claude.ai", "a.claude.ai", "console.anthropic.com",
            "api.anthropic.com", "platform.claude.com",
            "chatgpt.com", "ab.chatgpt.com", "auth.openai.com", "auth0.openai.com",
            "platform.openai.com", "cdn.oaistatic.com", "files.oaiusercontent.com",
            "tcr9i.chat.openai.com", "webrtc.chatgpt.com", "android.chat.openai.com",
            "api.openai.com", "operator.chatgpt.com", "sora.chatgpt.com",
            "videos.openai.com", "ios.chat.openai.com", "cdn.platform.openai.com",
            "developers.openai.com",
            "sdmntprukwest.oaiusercontent.com", "sdmntpritalynorth.oaiusercontent.com",
            "sdmntprpolandcentral.oaiusercontent.com", "sdmntprnortheu.oaiusercontent.com"
        };
    }

    static Settings load() {
        Settings s;
        s.domains = defaultDomains();
        QFile f(configPath());
        if (!f.open(QIODevice::ReadOnly)) return s;
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        auto str = [&](const char* k, const QString& def) {
            return o.contains(k) ? o.value(k).toString(def) : def;
        };
        auto lstr = [&](const char* k) {
            QStringList l;
            for (const auto& v : o.value(k).toArray()) l << v.toString();
            return l;
        };
        s.host = str("host", s.host);
        s.port = str("port", s.port);
        s.user = str("user", s.user);
        s.keyPath = str("keyPath", s.keyPath);
        s.usePassword = o.value("usePassword").toBool(s.usePassword);
        s.savePassword = o.value("savePassword").toBool(s.savePassword);
        if (s.savePassword) s.password = str("password", s.password);
        s.relayExit = str("relayExit", s.relayExit);
        s.siteBackend = str("siteBackend", s.siteBackend);
        s.hasSite = o.value("hasSite").toBool(s.hasSite);
        s.siteDomains = lstr("siteDomains");
        if (o.contains("domains")) s.domains = lstr("domains");
        return s;
    }

    bool save() const {
        QDir().mkpath(QFileInfo(configPath()).absolutePath());
        QJsonObject o;
        o["host"] = host;
        o["port"] = port;
        o["user"] = user;
        o["keyPath"] = keyPath;
        o["usePassword"] = usePassword;
        o["savePassword"] = savePassword;
        o["password"] = savePassword ? password : QString();
        o["relayExit"] = relayExit;
        o["siteBackend"] = siteBackend;
        o["hasSite"] = hasSite;
        o["siteDomains"] = QJsonArray::fromStringList(siteDomains);
        o["domains"] = QJsonArray::fromStringList(domains);
        QSaveFile f(configPath());
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        return f.commit();
    }
};
