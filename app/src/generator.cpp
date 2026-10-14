#include "generator.h"

#include <QStringList>
#include <QSet>
#include <QRegularExpression>

namespace gen {

QString relayConf(const Settings& s) {
    QStringList l;
    l << "stream {";
    l << "    map_hash_bucket_size 128;";
    l << "    map_hash_max_size 8192;";
    l << "    map $ssl_preread_server_name $relay_backend {";
    QSet<QString> seen;
    for (const auto& d : s.domains) {
        const QString t = d.trimmed();
        if (t.isEmpty() || t.startsWith('#') || seen.contains(t)) continue;
        seen.insert(t);
        if (t.startsWith("*.")) {
            // wildcard -> regex-ключ для nginx map
            QString rest = t.mid(2);
            rest.replace(".", "\\.");
            l << QString("        ~*\\.%1$ %2;").arg(rest, s.relayExit);
        } else {
            l << QString("        %1 %2;").arg(t, s.relayExit);
        }
    }
    for (const auto& d : s.siteDomains) {
        const QString t = d.trimmed();
        if (t.isEmpty() || t.startsWith('#') || seen.contains(t)) continue;
        seen.insert(t);
        l << QString("        %1 %2;").arg(t, s.siteBackend);
    }
    if (s.mtgVia443 && !s.mtgFront.trimmed().isEmpty()) {
        const QString lp = s.mtgLocalPort.trimmed().isEmpty() ? QString("9999") : s.mtgLocalPort.trimmed();
        l << QString("        %1 127.0.0.1:%2;   # MTProto (mtg)").arg(s.mtgFront.trimmed(), lp);
    }
    l << QString("        default %1;").arg(s.hasSite ? s.siteBackend : QString("127.0.0.1:1"));
    l << "    }";
    l << "";
    l << "    server {";
    l << "        listen 443;";
    l << "        listen [::]:443;";
    l << "        ssl_preread on;";
    l << "        proxy_pass $relay_backend;";
    l << "        proxy_protocol on;";
    l << "    }";
    l << "";
    l << "    server {";
    l << QString("        listen %1 proxy_protocol;").arg(s.relayExit);
    l << "        ssl_preread on;";
    l << "        resolver 1.1.1.1 8.8.8.8 valid=60s ipv6=off;";
    l << "        resolver_timeout 5s;";
    l << "        proxy_pass $ssl_preread_server_name:443;";
    l << "    }";
    l << "}";
    return l.join('\n') + "\n";
}

QString deployScript(const Settings& s) {
    const QString relay = relayConf(s);
    QString out;
    out += "#!/bin/bash\nset -u\nNGINX=/etc/nginx\n";
    out += "command -v nginx >/dev/null || { echo NO_NGINX; exit 1; }\n";
    out += "BK=/root/nginx-backup-$(date +%F-%H%M%S)\n";
    out += "mkdir -p \"$BK\" && cp -a \"$NGINX\"/. \"$BK\"/ || { echo BACKUP_FAIL; exit 1; }\n";
    out += "echo \"backup: $BK\"\n";
    out += "mkdir -p \"$NGINX/stream-enabled\"\n";
    out += "cat > \"$NGINX/conf.d/00-realip.conf\" <<'EORIP'\n"
           "set_real_ip_from 127.0.0.1;\nset_real_ip_from ::1;\n"
           "real_ip_header proxy_protocol;\nreal_ip_recursive on;\nEORIP\n";
    out += "moved=0\n";
    out += "while IFS= read -r f; do [ -n \"$f\" ] || continue; "
           "sed -i 's#listen 443 ssl http2;#listen 127.0.0.1:8443 ssl http2 proxy_protocol;#' \"$f\"; "
           "sed -i 's#listen 443 ssl;#listen 127.0.0.1:8443 ssl proxy_protocol;#' \"$f\"; "
           "echo \"site moved: $f\"; moved=1; "
           "done < <(grep -rlE 'listen[[:space:]]+443([[:space:]]|;)' \"$NGINX/sites-enabled\" \"$NGINX/conf.d\" 2>/dev/null || true)\n";
    out += "cat > \"$NGINX/stream-enabled/relay.conf\" <<'EORELAY'\n" + relay + "EORELAY\n";
    out += "grep -q stream-enabled \"$NGINX/nginx.conf\" || printf '\\ninclude /etc/nginx/stream-enabled/*.conf;\\n' >> \"$NGINX/nginx.conf\"\n";
    out += "if nginx -t; then systemctl reload nginx && echo DEPLOY_OK; "
           "else echo DEPLOY_FAIL; cp -a \"$BK\"/. \"$NGINX\"/; nginx -t && systemctl reload nginx; exit 1; fi\n";
    return out;
}

QString rollbackScript(const Settings&) {
    QString out;
    out += "#!/bin/bash\nset -e\n";
    out += "BK=$(ls -d /root/nginx-backup-* 2>/dev/null | sort | tail -1)\n";
    out += "[ -n \"$BK\" ] || { echo NO_BACKUP; exit 1; }\n";
    out += "cp -a \"$BK\"/. /etc/nginx/\n";
    out += "nginx -t && systemctl reload nginx && echo \"ROLLBACK_OK from $BK\"\n";
    return out;
}

// ---------- VLESS + Reality (Xray) ----------

static QString realityPortOf(const Settings& s) {
    return s.realityPort.trimmed().isEmpty() ? QString("2053") : s.realityPort.trimmed();
}
static QString realityDestOf(const Settings& s) {
    return s.realityDest.trimmed().isEmpty() ? QString("dl.google.com") : s.realityDest.trimmed();
}
static QString realityFlowOf(const Settings& s) {
    return s.realityFlow.trimmed().isEmpty() ? QString("xtls-rprx-vision") : s.realityFlow.trimmed();
}
static QString realityFpOf(const Settings& s) {
    return s.realityFingerprint.trimmed().isEmpty() ? QString("chrome") : s.realityFingerprint.trimmed();
}
static bool realityIsGrpc(const Settings& s) {
    return s.realityTransport.trimmed() == "grpc";
}
static QString realityEffPort(const Settings& s) {
    if (realityIsGrpc(s))
        return s.realityGrpcPort.trimmed().isEmpty() ? QString("2083") : s.realityGrpcPort.trimmed();
    return realityPortOf(s);
}
static QString realityServiceOf(const Settings& s) {
    return s.realityServiceName.trimmed().isEmpty() ? QString("grpc") : s.realityServiceName.trimmed();
}
static QString realityDomainOf(const Settings& s) {
    return s.realityDomain.trimmed().isEmpty() ? s.host.trimmed() : s.realityDomain.trimmed();
}

QString realityInstall() {
    QString o;
    o += "set -e\n";
    o += "export DEBIAN_FRONTEND=noninteractive\n";
    o += "curl -fsSL -o /tmp/xray-install.sh https://github.com/XTLS/Xray-install/raw/main/install-release.sh\n";
    o += "bash /tmp/xray-install.sh install\n";
    o += "rm -f /tmp/xray-install.sh\n";
    o += "command -v xray >/dev/null || { echo NO_XRAY; exit 1; }\n";
    o += "xray version | head -1\n";
    o += "echo XRAY_INSTALLED\n";
    return o;
}

QString realityGenKeys() {
    QString o;
    o += "set -e\n";
    o += "command -v xray >/dev/null || { echo NO_XRAY; exit 1; }\n";
    o += "echo \"UUID=$(xray uuid)\"\n";
    o += "K=$(xray x25519 | grep -oE '[A-Za-z0-9_-]{43}=?')\n";
    o += "echo \"PRIV=$(printf '%s\\n' \"$K\" | sed -n 1p)\"\n";
    o += "echo \"PUB=$(printf '%s\\n' \"$K\" | sed -n 2p)\"\n";
    o += "echo \"SHORTID=$(openssl rand -hex 8)\"\n";
    o += "echo REALITY_KEYS_OK\n";
    return o;
}

QString realityServerConfig(const Settings& s) {
    const QString port = realityEffPort(s);
    if (realityIsGrpc(s)) {
        const QString svc = realityServiceOf(s);
        QString o;
        o += "{\n";
        o += "  \"log\": { \"loglevel\": \"warning\" },\n";
        o += "  \"inbounds\": [\n";
        o += "    {\n";
        o += "      \"tag\": \"vless-grpc\",\n";
        o += "      \"listen\": \"0.0.0.0\",\n";
        o += "      \"port\": " + port + ",\n";
        o += "      \"protocol\": \"vless\",\n";
        o += "      \"settings\": {\n";
        o += "        \"clients\": [ { \"id\": \"" + s.realityUuid.trimmed() + "\" } ],\n";
        o += "        \"decryption\": \"none\"\n";
        o += "      },\n";
        o += "      \"streamSettings\": {\n";
        o += "        \"network\": \"grpc\",\n";
        o += "        \"grpcSettings\": { \"serviceName\": \"" + svc + "\", \"multiMode\": false }\n";
        o += "      },\n";
        o += "      \"sniffing\": { \"enabled\": true, \"destOverride\": [\"http\",\"tls\",\"quic\"] }\n";
        o += "    }\n";
        o += "  ],\n";
        o += "  \"outbounds\": [ { \"protocol\": \"freedom\", \"tag\": \"direct\" } ]\n";
        o += "}\n";
        return o;
    }
    const QString dest = realityDestOf(s);
    const QString flow = realityFlowOf(s);
    QString o;
    o += "{\n";
    o += "  \"log\": { \"loglevel\": \"warning\" },\n";
    o += "  \"inbounds\": [\n";
    o += "    {\n";
    o += "      \"tag\": \"vless-reality\",\n";
    o += "      \"listen\": \"0.0.0.0\",\n";
    o += "      \"port\": " + port + ",\n";
    o += "      \"protocol\": \"vless\",\n";
    o += "      \"settings\": {\n";
    o += "        \"clients\": [ { \"id\": \"" + s.realityUuid.trimmed() + "\", \"flow\": \"" + flow + "\" } ],\n";
    o += "        \"decryption\": \"none\"\n";
    o += "      },\n";
    o += "      \"streamSettings\": {\n";
    o += "        \"network\": \"tcp\",\n";
    o += "        \"security\": \"reality\",\n";
    o += "        \"realitySettings\": {\n";
    o += "          \"show\": false,\n";
    o += "          \"dest\": \"" + dest + ":443\",\n";
    o += "          \"xver\": 0,\n";
    o += "          \"serverNames\": [ \"" + dest + "\" ],\n";
    o += "          \"privateKey\": \"" + s.realityPrivateKey.trimmed() + "\",\n";
    o += "          \"shortIds\": [ \"" + s.realityShortId.trimmed() + "\" ]\n";
    o += "        }\n";
    o += "      },\n";
    o += "      \"sniffing\": { \"enabled\": true, \"destOverride\": [\"http\",\"tls\",\"quic\"] }\n";
    o += "    }\n";
    o += "  ],\n";
    o += "  \"outbounds\": [ { \"protocol\": \"freedom\", \"tag\": \"direct\" } ]\n";
    o += "}\n";
    return o;
}

QString realitySidecar(const Settings& s) {
    QString o;
    o += "{\n";
    o += "  \"host\": \"" + s.host.trimmed() + "\",\n";
    o += "  \"transport\": \"" + (realityIsGrpc(s) ? QString("grpc") : QString("reality")) + "\",\n";
    o += "  \"port\": \"" + realityEffPort(s) + "\",\n";
    o += "  \"domain\": \"" + realityDomainOf(s) + "\",\n";
    o += "  \"serviceName\": \"" + realityServiceOf(s) + "\",\n";
    o += "  \"grpcPort\": \"" + (s.realityGrpcPort.trimmed().isEmpty() ? QString("2083") : s.realityGrpcPort.trimmed()) + "\",\n";
    o += "  \"dest\": \"" + realityDestOf(s) + "\",\n";
    o += "  \"flow\": \"" + realityFlowOf(s) + "\",\n";
    o += "  \"fingerprint\": \"" + realityFpOf(s) + "\",\n";
    o += "  \"uuid\": \"" + s.realityUuid.trimmed() + "\",\n";
    o += "  \"publicKey\": \"" + s.realityPublicKey.trimmed() + "\",\n";
    o += "  \"privateKey\": \"" + s.realityPrivateKey.trimmed() + "\",\n";
    o += "  \"shortId\": \"" + s.realityShortId.trimmed() + "\"\n";
    o += "}\n";
    return o;
}

QString realityDeployScript(const Settings& s) {
    const QString port = realityEffPort(s);
    const QString cfg = realityServerConfig(s);
    const QString side = realitySidecar(s);
    QString out;
    out += "set -e\n";
    out += "command -v xray >/dev/null || { echo NO_XRAY; exit 1; }\n";
    out += "mkdir -p /usr/local/etc/xray\n";
    out += "cat > /usr/local/etc/xray/config.json <<'EOF_CFG'\n" + cfg + "EOF_CFG\n";
    out += "cat > /etc/sni-relay-reality.json <<'EOF_SIDE'\n" + side + "EOF_SIDE\n";
    out += "chmod 600 /etc/sni-relay-reality.json\n";
    out += "cat > /etc/systemd/system/xray.service <<'EOF_UNIT'\n";
    out += "[Unit]\nDescription=Xray Service\nAfter=network.target nss-lookup.target\nWants=network-online.target\n\n";
    out += "[Service]\nUser=root\nExecStart=/usr/local/bin/xray run -config /usr/local/etc/xray/config.json\n";
    out += "Restart=on-failure\nRestartSec=3\nLimitNOFILE=1000000\nNoNewPrivileges=true\n\n";
    out += "[Install]\nWantedBy=multi-user.target\nEOF_UNIT\n";
    out += "systemctl daemon-reload\n";
    out += "systemctl enable xray >/dev/null 2>&1 || true\n";
    out += "systemctl restart xray\n";
    out += "sleep 1\n";
    out += "systemctl is-active xray\n";
    out += "ss -tlnp | grep ':" + port + "' || true\n";
    out += "echo REALITY_DEPLOYED\n";
    return out;
}

QString realityStatus(const Settings& s) {
    const QString port = realityEffPort(s);
    QString o;
    o += "command -v xray >/dev/null 2>&1 && echo \"VER=$(xray version 2>/dev/null | head -1 | awk '{print $2}')\" || echo VER=NO\n";
    o += "[ -f /etc/systemd/system/xray.service ] && echo UNIT=yes || echo UNIT=no\n";
    o += "echo \"ACTIVE=$(systemctl is-active xray 2>/dev/null || true)\"\n";
    o += "ss -tlnp 2>/dev/null | grep -q ':" + port + " ' && echo PORT=yes || echo PORT=no\n";
    o += "[ -f /usr/local/etc/xray/config.json ] && echo CFG=yes || echo CFG=no\n";
    o += "[ -f /etc/sni-relay-reality.json ] && echo SIDE=yes || echo SIDE=no\n";
    o += "echo \"SIDE_B64=$(base64 -w0 /etc/sni-relay-reality.json 2>/dev/null)\"\n";
    return o;
}

QString realityRemove() {
    QString o;
    o += "systemctl stop xray 2>/dev/null || true\n";
    o += "systemctl disable xray 2>/dev/null || true\n";
    o += "rm -f /etc/systemd/system/xray.service /etc/sni-relay-reality.json /usr/local/bin/xray\n";
    o += "rm -rf /usr/local/etc/xray\n";
    o += "systemctl daemon-reload\n";
    o += "systemctl reset-failed xray 2>/dev/null || true\n";
    o += "echo REALITY_REMOVED\n";
    return o;
}

QString realityClientLink(const Settings& s) {
    const QString uuid = s.realityUuid.trimmed();
    const QString fp = realityFpOf(s);
    if (realityIsGrpc(s)) {
        const QString domain = realityDomainOf(s);
        const QString svc = realityServiceOf(s);
        return QString("vless://%1@%2:443?encryption=none&security=tls&sni=%3&fp=%4&type=grpc&serviceName=%5&mode=gun#%6")
            .arg(uuid, domain, domain, fp, svc, QString("HugeDev-gRPC"));
    }
    const QString host = s.host.trimmed();
    const QString port = realityEffPort(s);
    const QString dest = realityDestOf(s);
    const QString flow = realityFlowOf(s);
    const QString pub = s.realityPublicKey.trimmed();
    const QString sid = s.realityShortId.trimmed();
    return QString("vless://%1@%2:%3?encryption=none&flow=%4&security=reality&sni=%5&fp=%6&pbk=%7&sid=%8&type=tcp&headerType=none#%9")
        .arg(uuid, host, port, flow, dest, fp, pub, sid, QString("HugeDev-Reality"));
}

QString realityClientConfig(const Settings& s) {
    const QString socks = s.realitySocksPort.trimmed().isEmpty() ? QString("10808") : s.realitySocksPort.trimmed();
    const QString http  = s.realityHttpPort.trimmed().isEmpty()  ? QString("10809") : s.realityHttpPort.trimmed();
    const QString uuid = s.realityUuid.trimmed();
    const QString fp = realityFpOf(s);
    QString o;
    o += "{\n";
    o += "  \"log\": { \"loglevel\": \"warning\" },\n";
    o += "  \"inbounds\": [\n";
    o += "    { \"listen\": \"127.0.0.1\", \"port\": " + socks + ", \"protocol\": \"socks\", \"settings\": { \"udp\": true } },\n";
    o += "    { \"listen\": \"127.0.0.1\", \"port\": " + http + ", \"protocol\": \"http\" }\n";
    o += "  ],\n";
    o += "  \"outbounds\": [\n";
    o += "    {\n";
    o += "      \"protocol\": \"vless\",\n";
    if (realityIsGrpc(s)) {
        const QString domain = realityDomainOf(s);
        const QString svc = realityServiceOf(s);
        o += "      \"settings\": { \"vnext\": [ { \"address\": \"" + domain + "\", \"port\": 443,\n";
        o += "        \"users\": [ { \"id\": \"" + uuid + "\", \"encryption\": \"none\" } ] } ] },\n";
        o += "      \"streamSettings\": {\n";
        o += "        \"network\": \"grpc\",\n";
        o += "        \"grpcSettings\": { \"serviceName\": \"" + svc + "\" },\n";
        o += "        \"security\": \"tls\",\n";
        o += "        \"tlsSettings\": { \"serverName\": \"" + domain + "\", \"fingerprint\": \"" + fp + "\" }\n";
        o += "      }\n";
    } else {
        const QString host = s.host.trimmed();
        const QString port = realityEffPort(s);
        const QString dest = realityDestOf(s);
        const QString flow = realityFlowOf(s);
        const QString pub = s.realityPublicKey.trimmed();
        const QString sid = s.realityShortId.trimmed();
        o += "      \"settings\": { \"vnext\": [ { \"address\": \"" + host + "\", \"port\": " + port + ",\n";
        o += "        \"users\": [ { \"id\": \"" + uuid + "\", \"encryption\": \"none\", \"flow\": \"" + flow + "\" } ] } ] },\n";
        o += "      \"streamSettings\": {\n";
        o += "        \"network\": \"tcp\",\n";
        o += "        \"security\": \"reality\",\n";
        o += "        \"realitySettings\": { \"serverName\": \"" + dest + "\", \"fingerprint\": \"" + fp + "\",\n";
        o += "          \"publicKey\": \"" + pub + "\", \"shortId\": \"" + sid + "\" }\n";
        o += "      }\n";
    }
    o += "    }\n";
    o += "  ]\n";
    o += "}\n";
    return o;
}

QString realityTunConfig(const Settings& s) {
    const QString uuid = s.realityUuid.trimmed();
    const QString fp = realityFpOf(s);
    QString ob;
    if (realityIsGrpc(s)) {
        const QString domain = realityDomainOf(s);
        const QString svc = realityServiceOf(s);
        ob += "    { \"type\": \"vless\", \"tag\": \"proxy\", \"server\": \"" + domain + "\", \"server_port\": 443,\n";
        ob += "      \"uuid\": \"" + uuid + "\",\n";
        ob += "      \"tls\": { \"enabled\": true, \"server_name\": \"" + domain + "\", \"utls\": { \"enabled\": true, \"fingerprint\": \"" + fp + "\" } },\n";
        ob += "      \"transport\": { \"type\": \"grpc\", \"service_name\": \"" + svc + "\" } }";
    } else {
        const QString host = s.host.trimmed();
        const QString port = realityEffPort(s);
        const QString dest = realityDestOf(s);
        const QString flow = realityFlowOf(s);
        const QString pub = s.realityPublicKey.trimmed();
        const QString sid = s.realityShortId.trimmed();
        ob += "    { \"type\": \"vless\", \"tag\": \"proxy\", \"server\": \"" + host + "\", \"server_port\": " + port + ",\n";
        ob += "      \"uuid\": \"" + uuid + "\", \"flow\": \"" + flow + "\",\n";
        ob += "      \"tls\": { \"enabled\": true, \"server_name\": \"" + dest + "\", \"utls\": { \"enabled\": true, \"fingerprint\": \"" + fp + "\" },\n";
        ob += "        \"reality\": { \"enabled\": true, \"public_key\": \"" + pub + "\", \"short_id\": \"" + sid + "\" } } }";
    }
    QString o;
    o += "{\n";
    o += "  \"log\": { \"level\": \"warn\" },\n";
    o += "  \"dns\": { \"servers\": [ { \"type\": \"udp\", \"server\": \"1.1.1.1\", \"detour\": \"proxy\" } ] },\n";
    o += "  \"inbounds\": [\n";
    o += "    { \"type\": \"tun\", \"tag\": \"tun-in\", \"interface_name\": \"srm0\", \"address\": [ \"172.19.0.1/30\" ],\n";
    o += "      \"mtu\": 1500, \"auto_route\": true, \"strict_route\": false, \"stack\": \"system\" }\n";
    o += "  ],\n";
    o += "  \"outbounds\": [\n";
    o += ob + ",\n";
    o += "    { \"type\": \"direct\", \"tag\": \"direct\" }\n";
    o += "  ],\n";
    o += "  \"route\": { \"auto_detect_interface\": true, \"rules\": [ { \"action\": \"sniff\" }, { \"protocol\": \"dns\", \"action\": \"hijack-dns\" } ] }\n";
    o += "}\n";
    return o;
}

QString hostsFile(const Settings& s, bool addRelay, const QString& currentHosts) {
    QSet<QString> want;
    for (const auto& d : s.domains) { const QString t = d.trimmed(); if (!t.isEmpty() && !t.contains('*')) want.insert(t); }

    QStringList out;
    for (const QString& line : currentHosts.split('\n')) {
        if (line.trimmed().startsWith('#')) { out << line; continue; }
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        bool drop = false;
        for (int i = 1; i < parts.size(); ++i) if (want.contains(parts[i])) { drop = true; break; }
        if (!drop) out << line;
    }
    QString res = out.join('\n');
    if (!res.endsWith('\n')) res += '\n';
    if (addRelay) {
        res += "\n# relay via " + s.host + "\n";
        for (const auto& d : s.domains) { const QString t = d.trimmed(); if (!t.isEmpty() && !t.contains('*')) res += s.host + " " + t + "\n"; }
    }
    return res;
}

} // namespace gen
