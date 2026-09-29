#include "generator.h"

#include <QStringList>
#include <QSet>
#include <QRegularExpression>

namespace gen {

QString relayConf(const Settings& s) {
    QStringList l;
    l << "stream {";
    l << "    map $ssl_preread_server_name $relay_backend {";
    for (const auto& d : s.domains) {
        const QString t = d.trimmed();
        if (t.isEmpty() || t.startsWith('#')) continue;
        l << QString("        %1 %2;").arg(t, s.relayExit);
    }
    for (const auto& d : s.siteDomains) {
        const QString t = d.trimmed();
        if (t.isEmpty() || t.startsWith('#')) continue;
        l << QString("        %1 %2;").arg(t, s.siteBackend);
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

QString hostsFile(const Settings& s, bool addRelay, const QString& currentHosts) {
    QSet<QString> want;
    for (const auto& d : s.domains) { const QString t = d.trimmed(); if (!t.isEmpty()) want.insert(t); }

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
        for (const auto& d : s.domains) { const QString t = d.trimmed(); if (!t.isEmpty()) res += s.host + " " + t + "\n"; }
    }
    return res;
}

} // namespace gen
