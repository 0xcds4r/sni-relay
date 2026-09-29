#!/bin/bash
# client-hosts.sh IP_VDS [domains.txt] — прописать домены релея в /etc/hosts
# Запускать НА КЛИЕНТЕ (домашней машине).
#
# Умеет повышать права через root / sudo -n / systemd-run.
# Делает бэкап /etc/hosts и вырезает старые записи для этих доменов.
set -euo pipefail

VDS_IP="${1:-}"
DIR="$(dirname "$(readlink -f "$0")")"
DOMAINS_FILE="${2:-$DIR/domains.txt}"

if [ -z "$VDS_IP" ]; then echo "Usage: $0 IP_VDS [domains.txt]"; exit 2; fi
if ! printf '%s' "$VDS_IP" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then
  echo "Не похоже на IPv4: $VDS_IP"; exit 2
fi
if [ ! -f "$DOMAINS_FILE" ]; then echo "Нет файла доменов: $DOMAINS_FILE"; exit 2; fi

mapfile -t doms < <(sed 's/#.*//; s/^[[:space:]]*//; s/[[:space:]]*$//' "$DOMAINS_FILE" | grep -v '^$')
if [ "${#doms[@]}" -eq 0 ]; then echo "Список доменов пуст."; exit 2; fi

# --- собрать новый hosts ---
work="$(mktemp /tmp/hosts.new.XXXXXX)"
awk -v list="$(printf '%s\n' "${doms[@]}")" '
BEGIN{ n=split(list,D,"\n"); for(i=1;i<=n;i++) want[D[i]]=1 }
{
  if ($0 ~ /^[[:space:]]*#/) { print; next }
  k=split($0,a,/[[:space:]]+/); drop=0
  for (j=2;j<=k;j++) if (a[j] in want) drop=1
  if (!drop) print
}
' /etc/hosts > "$work"
{
  echo
  echo "# relay via $VDS_IP ($(date +%F))"
  for d in "${doms[@]}"; do echo "$VDS_IP $d"; done
} >> "$work"
chmod 644 "$work"

# --- скрипт установки (выполняется от root) ---
apply="$(mktemp /tmp/apply-hosts.XXXXXX)"
cat > "$apply" <<EOF
#!/bin/bash
set -e
cp -a /etc/hosts "/etc/hosts.bak-\$(date +%F-%H%M%S)"
cat "$work" > /etc/hosts
resolvectl flush-caches 2>/dev/null || true
echo "hosts обновлён (бэкап рядом: /etc/hosts.bak-*)"
EOF
chmod 755 "$apply"

if [ "$(id -u)" = 0 ]; then
  bash "$apply"
elif sudo -n true 2>/dev/null; then
  sudo bash "$apply"
elif command -v systemd-run >/dev/null 2>&1; then
  systemd-run --system --wait --pipe --collect /bin/bash "$apply"
else
  echo "Нет способа получить root. Новый hosts лежит в: $work"
  echo "Установи вручную:  sudo cp /etc/hosts /etc/hosts.bak && sudo cp $work /etc/hosts"
  exit 1
fi

rm -f "$work" "$apply"

echo "Проверка:"
for d in "${doms[@]}"; do printf '  %-42s ' "$d"; getent ahostsv4 "$d" | awk 'NR==1{print $1}'; done
echo
echo "Не забудь выключить Secure DNS (DoH) в браузере!"
