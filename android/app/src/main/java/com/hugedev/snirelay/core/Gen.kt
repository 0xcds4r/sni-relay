package com.hugedev.snirelay.core

import com.hugedev.snirelay.data.Settings

/** Порт generator.cpp: чистые функции генерации конфигов и скриптов. */
object Gen {

    fun qshell(s: String): String = "'" + s.replace("'", "'\\''") + "'"

    fun relayConf(s: Settings): String {
        val l = mutableListOf<String>()
        l += "stream {"
        l += "    map_hash_bucket_size 128;"
        l += "    map_hash_max_size 8192;"
        l += "    map \$ssl_preread_server_name \$relay_backend {"
        for (d in s.domains) {
            val t = d.trim()
            if (t.isEmpty() || t.startsWith("#")) continue
            l += "        $t ${s.relayExit};"
        }
        for (d in s.siteDomains) {
            val t = d.trim()
            if (t.isEmpty() || t.startsWith("#")) continue
            l += "        $t ${s.siteBackend};"
        }
        if (s.mtgVia443 && s.mtgFront.trim().isNotEmpty()) {
            val lp = s.mtgLocalPort.trim().ifEmpty { "9999" }
            l += "        ${s.mtgFront.trim()} 127.0.0.1:$lp;   # MTProto (mtg)"
        }
        l += "        default ${if (s.hasSite) s.siteBackend else "127.0.0.1:1"};"
        l += "    }"
        l += ""
        l += "    server {"
        l += "        listen 443;"
        l += "        listen [::]:443;"
        l += "        ssl_preread on;"
        l += "        proxy_pass \$relay_backend;"
        l += "        proxy_protocol on;"
        l += "    }"
        l += ""
        l += "    server {"
        l += "        listen ${s.relayExit} proxy_protocol;"
        l += "        ssl_preread on;"
        l += "        resolver 1.1.1.1 8.8.8.8 valid=60s ipv6=off;"
        l += "        resolver_timeout 5s;"
        l += "        proxy_pass \$ssl_preread_server_name:443;"
        l += "    }"
        l += "}"
        return l.joinToString("\n") + "\n"
    }

    fun deployScript(s: Settings): String {
        val relay = relayConf(s)
        val sb = StringBuilder()
        sb.append("#!/bin/bash\nset -u\nNGINX=/etc/nginx\n")
        sb.append("command -v nginx >/dev/null || { echo NO_NGINX; exit 1; }\n")
        sb.append("BK=/root/nginx-backup-\$(date +%F-%H%M%S)\n")
        sb.append("mkdir -p \"\$BK\" && cp -a \"\$NGINX\"/. \"\$BK\"/ || { echo BACKUP_FAIL; exit 1; }\n")
        sb.append("echo \"backup: \$BK\"\n")
        sb.append("mkdir -p \"\$NGINX/stream-enabled\"\n")
        sb.append(
            "cat > \"\$NGINX/conf.d/00-realip.conf\" <<'EORIP'\n" +
                "set_real_ip_from 127.0.0.1;\nset_real_ip_from ::1;\n" +
                "real_ip_header proxy_protocol;\nreal_ip_recursive on;\nEORIP\n"
        )
        sb.append("moved=0\n")
        sb.append(
            "while IFS= read -r f; do [ -n \"\$f\" ] || continue; " +
                "sed -i 's#listen 443 ssl http2;#listen 127.0.0.1:8443 ssl http2 proxy_protocol;#' \"\$f\"; " +
                "sed -i 's#listen 443 ssl;#listen 127.0.0.1:8443 ssl proxy_protocol;#' \"\$f\"; " +
                "echo \"site moved: \$f\"; moved=1; " +
                "done < <(grep -rlE 'listen[[:space:]]+443([[:space:]]|;)' \"\$NGINX/sites-enabled\" \"\$NGINX/conf.d\" 2>/dev/null || true)\n"
        )
        sb.append("cat > \"\$NGINX/stream-enabled/relay.conf\" <<'EORELAY'\n" + relay + "EORELAY\n")
        sb.append("grep -q stream-enabled \"\$NGINX/nginx.conf\" || printf '\\ninclude /etc/nginx/stream-enabled/*.conf;\\n' >> \"\$NGINX/nginx.conf\"\n")
        sb.append(
            "if nginx -t; then systemctl reload nginx && echo DEPLOY_OK; " +
                "else echo DEPLOY_FAIL; cp -a \"\$BK\"/. \"\$NGINX\"/; nginx -t && systemctl reload nginx; exit 1; fi\n"
        )
        return sb.toString()
    }

    fun rollbackScript(): String {
        val sb = StringBuilder()
        sb.append("#!/bin/bash\nset -e\n")
        sb.append("BK=\$(ls -d /root/nginx-backup-* 2>/dev/null | sort | tail -1)\n")
        sb.append("[ -n \"\$BK\" ] || { echo NO_BACKUP; exit 1; }\n")
        sb.append("cp -a \"\$BK\"/. /etc/nginx/\n")
        sb.append("nginx -t && systemctl reload nginx && echo \"ROLLBACK_OK from \$BK\"\n")
        return sb.toString()
    }

    // --- mtg ---

    fun mtgInstall(): String =
        "set -e\n" +
            "case \"\$(uname -m)\" in x86_64) A=amd64;; aarch64) A=arm64;; *) A=amd64;; esac\n" +
            "VER=\$(curl -s --max-time 20 https://api.github.com/repos/9seconds/mtg/releases/latest | grep -oE '\"tag_name\": *\"v[0-9.]+\"' | grep -oE 'v[0-9.]+')\n" +
            "[ -n \"\$VER\" ] || { echo NO_VER; exit 1; }\n" +
            "echo \"mtg \$VER (\$A)\"\n" +
            "cd /tmp\n" +
            "curl -sSL --max-time 120 -o mtg.tar.gz \"https://github.com/9seconds/mtg/releases/download/\$VER/mtg-\${VER#v}-linux-\$A.tar.gz\"\n" +
            "D=\"mtg-\${VER#v}-linux-\$A\"\n" +
            "tar xzf mtg.tar.gz\n" +
            "install -m755 \"\$D/mtg\" /usr/local/bin/mtg\n" +
            "rm -rf mtg.tar.gz \"\$D\"\n" +
            "/usr/local/bin/mtg --version\n" +
            "echo MTG_INSTALLED\n"

    fun mtgGenSecret(front: String): String =
        "set -e\ncommand -v mtg >/dev/null || { echo NO_MTG; exit 1; }\nmtg generate-secret ${qshell(front)}\n"

    fun mtgDeploy(s: Settings): String {
        val port = s.mtgPort.trim().ifEmpty { "10443" }
        val via = s.mtgVia443
        val localPort = s.mtgLocalPort.trim().ifEmpty { "9999" }
        val bindPort = if (via) localPort else port
        val ip = s.host.trim()
        val ip4 = Regex("^[0-9.]+$").matches(ip)

        val toml = StringBuilder()
        toml.append("secret = \"${s.mtgSecret.trim()}\"\n")
        toml.append("bind-to = \"${if (via) "127.0.0.1:" else "0.0.0.0:"}$bindPort\"\n")
        toml.append("concurrency = 8192\n")
        toml.append("prefer-ip = \"prefer-ipv4\"\n")
        if (ip4) toml.append("public-ipv4 = \"$ip\"\n")
        toml.append("tolerate-time-skewness = \"30s\"\n")
        if (via) toml.append("proxy-protocol-listener = true\n")

        val unit =
            "[Unit]\nDescription=mtg MTProto proxy\nAfter=network-online.target\nWants=network-online.target\n\n" +
                "[Service]\nExecStart=/usr/local/bin/mtg run /etc/mtg.toml\nRestart=always\nRestartSec=3\n" +
                "LimitNOFILE=65536\nNoNewPrivileges=true\n\n[Install]\nWantedBy=multi-user.target\n"

        val sb = StringBuilder()
        sb.append("set -e\ncommand -v mtg >/dev/null || { echo NO_MTG; exit 1; }\n")
        sb.append("cat > /etc/mtg.toml <<'EOF_TOML'\n" + toml + "EOF_TOML\n")
        sb.append("chmod 600 /etc/mtg.toml\n")
        sb.append("cat > /etc/systemd/system/mtg.service <<'EOF_UNIT'\n" + unit + "EOF_UNIT\n")
        sb.append("systemctl daemon-reload\n")
        sb.append("systemctl enable mtg >/dev/null 2>&1 || true\n")
        sb.append("systemctl restart mtg\n")
        sb.append("sleep 1\n")
        sb.append("systemctl is-active mtg\n")
        sb.append("ss -tlnp | grep ':$bindPort' || true\n")
        sb.append("echo MTG_DEPLOYED\n")
        return sb.toString()
    }

    fun mtgStatus(s: Settings): String {
        val bindPort = if (s.mtgVia443) s.mtgLocalPort.trim().ifEmpty { "9999" }
        else s.mtgPort.trim().ifEmpty { "10443" }
        return (
            "command -v mtg >/dev/null 2>&1 && echo \"VER=\$(mtg --version 2>/dev/null | head -1 | awk '{print \$1}')\" || echo VER=NO\n" +
                "[ -f /etc/systemd/system/mtg.service ] && echo UNIT=yes || echo UNIT=no\n" +
                "echo \"ACTIVE=\$(systemctl is-active mtg 2>/dev/null || true)\"\n" +
                "ss -tlnp 2>/dev/null | grep -q ':$bindPort ' && echo PORT=yes || echo PORT=no\n" +
                "[ -f /etc/mtg.toml ] && echo TOML=yes || echo TOML=no\n" +
                "echo TOML_SECRET=\$(grep -E '^[[:space:]]*secret[[:space:]]*=' /etc/mtg.toml 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' \"')\n" +
                "echo TOML_BIND=\$(grep -E '^[[:space:]]*bind-to[[:space:]]*=' /etc/mtg.toml 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' \"')\n"
            )
    }

    fun mtgRemove(): String = """
        systemctl stop mtg 2>/dev/null || true
        systemctl disable mtg 2>/dev/null || true
        rm -f /etc/systemd/system/mtg.service /etc/mtg.toml /usr/local/bin/mtg
        systemctl daemon-reload
        systemctl reset-failed mtg 2>/dev/null || true
        echo MTG_REMOVED
    """.trimIndent() + "\n"

    fun tgProxyUrl(host: String, port: String, secret: String): String =
        "https://t.me/proxy?server=$host&port=$port&secret=$secret"

    fun tgQrUrl(host: String, port: String, secret: String): String =
        "tg://proxy?server=$host&port=$port&secret=$secret"

    /** Фронт-домен из FakeTLS-секрета mtg: [0xEE][16 байт][домен]. */
    fun mtgFrontFromSecret(secret: String): String {
        val s = secret.trim()
        val bytes = try {
            android.util.Base64.decode(s, android.util.Base64.URL_SAFE or android.util.Base64.NO_PADDING or android.util.Base64.NO_WRAP)
        } catch (_: Exception) {
            return ""
        }
        if (bytes.size < 17) return ""
        if ((bytes[0].toInt() and 0xFF) != 0xEE) return ""
        return String(bytes, 17, bytes.size - 17, Charsets.UTF_8)
    }
}
