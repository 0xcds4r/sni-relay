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

    // --- MTProto-прокси (mtg) ---
    QString mtgPort = "10443";
    QString mtgFront = "www.google.com";
    QString mtgSecret;
    bool mtgVia443 = true;              // слушать mtg на 443 через релей (SNI фронт-домена)
    QString mtgLocalPort = "9999";      // loopback-порт mtg, когда via443

    // --- VLESS + Reality (Xray) ---
    QString realityPort = "2053";       // публичный порт Reality (SNI = realityDest)
    QString realityDest = "dl.google.com"; // чужой домен, под чей TLS маскируется
    QString realityFlow = "xtls-rprx-vision";
    QString realityFingerprint = "chrome";
    QString realityUuid;                // id клиента
    QString realityPublicKey;           // pbk (в клиентской ссылке)
    QString realityPrivateKey;          // privateKey (в серверном конфиге)
    QString realityShortId;             // sid
    QString realitySocksPort = "10808"; // локальный SOCKS-порт клиента
    QString realityHttpPort = "10809";  // локальный HTTP-порт клиента
    bool realitySetProxy = true;        // выставлять системный прокси при подключении
    QString realityTransport = "reality"; // "reality" | "grpc" (gRPC+TLS за Cloudflare)
    QString realityDomain;                // домен за Cloudflare (для gRPC)
    QString realityServiceName = "grpc";  // gRPC serviceName
    QString realityGrpcPort = "2083";     // порт origin для gRPC (CF-совместимый)

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
            "sdmntprpolandcentral.oaiusercontent.com", "sdmntprnortheu.oaiusercontent.com",
            "*.googlevideo.com"
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
        s.mtgPort = str("mtgPort", s.mtgPort);
        s.mtgFront = str("mtgFront", s.mtgFront);
        s.mtgSecret = str("mtgSecret", s.mtgSecret);
        s.mtgVia443 = o.value("mtgVia443").toBool(s.mtgVia443);
        s.mtgLocalPort = str("mtgLocalPort", s.mtgLocalPort);
        s.realityPort = str("realityPort", s.realityPort);
        s.realityDest = str("realityDest", s.realityDest);
        s.realityFlow = str("realityFlow", s.realityFlow);
        s.realityFingerprint = str("realityFingerprint", s.realityFingerprint);
        s.realityUuid = str("realityUuid", s.realityUuid);
        s.realityPublicKey = str("realityPublicKey", s.realityPublicKey);
        s.realityPrivateKey = str("realityPrivateKey", s.realityPrivateKey);
        s.realityShortId = str("realityShortId", s.realityShortId);
        s.realitySocksPort = str("realitySocksPort", s.realitySocksPort);
        s.realityHttpPort = str("realityHttpPort", s.realityHttpPort);
        s.realitySetProxy = o.value("realitySetProxy").toBool(s.realitySetProxy);
        s.realityTransport = str("realityTransport", s.realityTransport);
        s.realityDomain = str("realityDomain", s.realityDomain);
        s.realityServiceName = str("realityServiceName", s.realityServiceName);
        s.realityGrpcPort = str("realityGrpcPort", s.realityGrpcPort);
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
        o["mtgPort"] = mtgPort;
        o["mtgFront"] = mtgFront;
        o["mtgSecret"] = mtgSecret;
        o["mtgVia443"] = mtgVia443;
        o["mtgLocalPort"] = mtgLocalPort;
        o["realityPort"] = realityPort;
        o["realityDest"] = realityDest;
        o["realityFlow"] = realityFlow;
        o["realityFingerprint"] = realityFingerprint;
        o["realityUuid"] = realityUuid;
        o["realityPublicKey"] = realityPublicKey;
        o["realityPrivateKey"] = realityPrivateKey;
        o["realityShortId"] = realityShortId;
        o["realitySocksPort"] = realitySocksPort;
        o["realityHttpPort"] = realityHttpPort;
        o["realitySetProxy"] = realitySetProxy;
        o["realityTransport"] = realityTransport;
        o["realityDomain"] = realityDomain;
        o["realityServiceName"] = realityServiceName;
        o["realityGrpcPort"] = realityGrpcPort;
        QSaveFile f(configPath());
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        return f.commit();
    }
};
