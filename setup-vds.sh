#!/bin/bash
# setup-vds.sh — настройка SNI-реле на VDS. Запускать НА VDS от root:
#   sudo bash setup-vds.sh [domains.txt]
#
# Что делает:
#   1. бэкап /etc/nginx
#   2. включает PROXY protocol (реальные IP для сайта)
#   3. переносит локальный сайт с 443 на 127.0.0.1:8443 (если он есть)
#   4. генерирует stream-релей из domains.txt
#   5. подключает stream в nginx.conf
#   6. nginx -t, reload; при ошибке — откат из бэкапа
set -euo pipefail

DIR="$(dirname "$(readlink -f "$0")")"
DOMAINS_FILE="${1:-$DIR/domains.txt}"

SITE_BACKEND="127.0.0.1:8443"
RELAY_EXIT="127.0.0.1:9443"
NGINX=/etc/nginx

if [ "$(id -u)" != 0 ]; then echo "Запусти от root (sudo)."; exit 1; fi
if [ ! -f "$DOMAINS_FILE" ]; then echo "Нет файла доменов: $DOMAINS_FILE"; exit 1; fi
if ! command -v nginx >/dev/null; then echo "nginx не установлен."; exit 1; fi
if ! ls "$NGINX"/modules-enabled/ 2>/dev/null | grep -q stream; then
  echo "[warn] модуль stream, похоже, не включён — проверь /etc/nginx/modules-enabled/"
fi

# --- 1. бэкап ---
BK="/root/nginx-backup-$(date +%F-%H%M%S)"
mkdir -p "$BK" && cp -a "$NGINX"/. "$BK"/
echo "backup: $BK"

# --- 2. реальные IP ---
cat > "$NGINX/conf.d/00-realip.conf" <<'EOF'
set_real_ip_from 127.0.0.1;
set_real_ip_from ::1;
real_ip_header proxy_protocol;
real_ip_recursive on;
EOF

# --- 3. перенос сайта с 443 ---
moved=0
while IFS= read -r f; do
  [ -n "$f" ] || continue
  sed -i 's#listen 443 ssl http2;#listen 127.0.0.1:8443 ssl http2 proxy_protocol;#' "$f"
  sed -i 's#listen 443 ssl;#listen 127.0.0.1:8443 ssl proxy_protocol;#' "$f"
  echo "перенесён сайт: $f"
  moved=1
done < <(grep -rlE 'listen[[:space:]]+443([[:space:]]|;)' "$NGINX/sites-enabled" "$NGINX/conf.d" 2>/dev/null || true)
if [ "$moved" = 1 ]; then
  echo "сайт перенесён на $SITE_BACKEND"
else
  echo "сайта на 443 нет — default будет закрыт (127.0.0.1:1)"
fi

# --- 4. stream-релей ---
mkdir -p "$NGINX/stream-enabled"
{
  echo "stream {"
  echo "    map_hash_bucket_size 128;"
  echo "    map_hash_max_size 8192;"
  echo "    map \$ssl_preread_server_name \$relay_backend {"
  while IFS= read -r line; do
    d="$(printf '%s' "$line" | sed 's/#.*//; s/^[[:space:]]*//; s/[[:space:]]*$//')"
    [ -n "$d" ] || continue
    printf '        %-40s %s;\n' "$d" "$RELAY_EXIT"
  done < "$DOMAINS_FILE"
  if [ "$moved" = 1 ]; then
    echo "        default                 $SITE_BACKEND;"
  else
    echo "        default                 127.0.0.1:1;"
  fi
  echo "    }"
  cat <<EOF

    server {
        listen 443;
        listen [::]:443;
        ssl_preread on;
        proxy_pass \$relay_backend;
        proxy_protocol on;
    }

    server {
        listen 127.0.0.1:9443 proxy_protocol;
        ssl_preread on;
        resolver 1.1.1.1 8.8.8.8 valid=60s ipv6=off;
        resolver_timeout 5s;
        proxy_pass \$ssl_preread_server_name:443;
    }
}
EOF
} > "$NGINX/stream-enabled/relay.conf"

# --- 5. include stream ---
if ! grep -q 'stream-enabled' "$NGINX/nginx.conf"; then
  printf '\ninclude /etc/nginx/stream-enabled/*.conf;\n' >> "$NGINX/nginx.conf"
fi

# --- 6. test / reload / rollback ---
if nginx -t; then
  systemctl reload nginx && echo "OK: nginx перезагружен"
else
  echo "nginx -t FAILED -> откат из $BK"
  cp -a "$BK"/. "$NGINX"/
  nginx -t && systemctl reload nginx
  exit 1
fi

echo
echo "Готово. Проверь с клиента:  curl -sI https://claude.ai/ | grep -iE '^HTTP|^location|cf-mitigated'"
