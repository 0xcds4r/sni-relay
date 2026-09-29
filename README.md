# Свой SNI-релей на VDS

Гайд по созданию прозрачного релея на своём VDS, который открывает
заблокированные/гео-ограниченные сайты (Claude, ChatGPT и т.п.) **без VPN и
без прокси-клиента** на домашней машине.

Проверено на: Ubuntu 22.04, nginx 1.18 (Ubuntu), VDS в Финляндии.

---

## 0. Идея

Обычный VPN/прокси прячет весь трафик. Здесь иначе:

1. DNS/`/etc/hosts` на клиенте говорит: `claude.ai → IP_ТВОЕГО_VDS`.
2. Клиент коннектится к твоему VDS на 443 и в TLS ClientHello передаёт **SNI**
   (имя сайта, например `claude.ai`).
3. На VDS nginx в режиме `stream` читает SNI и **проксирует TCP наружу** уже к
   настоящему сайту — с зарубежного IP VDS.
4. TLS остаётся end-to-end: сертификат настоящего сайта, ничего не подменяется.

Итог: сайт видит IP твоего VDS (поддерживаемая страна), а не твой домашний.
Со стороны выглядит как прямое соединение. Это ровно то, что делает
`dns.malw.link` — только релей у тебя свой.

```
[браузер] --TLS(SNI=claude.ai)--> [VDS nginx:443 / stream] --TCP--> claude.ai
   hosts: claude.ai -> IP_VDS
```

Важно: релей **не расшифровывает** трафик (TLS пасс-сквозь), но пропускает его
через себя — считай трафик VDS.

---

## 1. Требования

- **VDS в поддерживаемой стране.** Для Claude/ChatGPT — не РФ. Проверь:
  ```bash
  curl -s https://ipinfo.io/json
  ```
  Нужен `country`, который сервис поддерживает (FI/DE/NL/US/JP/...).
- **nginx с модулем `stream` и `ssl_preread`.** Проверка:
  ```bash
  ls /usr/lib/nginx/modules/ | grep stream
  ls /etc/nginx/modules-enabled/ | grep stream
  ```
  На Ubuntu это пакет `nginx` (stream идёт динамическим модулем).
- **root** (или sudo) на VDS.
- Знание, что на VDS уже крутится (сайт?), чтобы не сломать.

Проверить, что регион VDS подходит именно для нужного сайта:
```bash
curl -sI https://claude.ai/ | grep -iE '^HTTP|^location|cf-mitigated'
```
Если видишь `location: .../app-unavailable-in-region` — регион не подходит,
релей не поможет. Если `cf-mitigated: challenge` — регион ок, будет челлендж
Cloudflare (браузер проходит).

---

## 2. Схема на VDS

- **443** занимает `stream`-роутер (nginx, режим TCP-прокси).
- Он по SNI решает:
  - домены из списка → **выход наружу** (`127.0.0.1:9443`, оттуда без PROXY
    protocol к настоящему сайту);
  - всё остальное → **локальный сайт** (`127.0.0.1:8443`).
- Локальный сайт (если он есть) переносится с `443` на `127.0.0.1:8443`, и
  получает реальные IP клиентов через **PROXY protocol** (иначе сайт видел бы
  всех как `127.0.0.1`).

Цепочка нужна, чтобы релей наружу шёл **без** PROXY protocol (его понимают не
все сайты), а локальный сайт — **с** ним (сохранить реальные IP).

```
                   ┌──────────────── nginx ────────────────┐
 client:443 ──SNI──► stream server 443 (ssl_preread)          │
                   │   map: relay-домены ─► 127.0.0.1:9443    │
                   │        все прочие    ─► 127.0.0.1:8443    │
                   │ stream server 9443 (proxy_protocol)       │
                   │   proxy_pass $ssl_preread_server_name:443 │──► настоящий сайт
                   │ http server 8443 (сайт, proxy_protocol)   │
                   └──────────────────────────────────────────┘
```

Если сайта на VDS нет — упрощённый вариант: `default` ведёт в отбой, а не на
сайт (см. раздел «Вариант без сайта»).

---

## 3. Установка: пошагово

> Файлы-шаблоны лежат рядом: `relay.conf.example`, `00-realip.conf.example`,
> `domains.txt`, `setup-vds.sh`, `client-hosts.sh`.

### 3.1. Бэкап

```bash
BK=/root/nginx-backup-$(date +%F-%H%M)
mkdir -p "$BK" && cp -a /etc/nginx/. "$BK"/
echo "$BK"
```

### 3.2. Реальные IP клиентов (PROXY protocol)

`/etc/nginx/conf.d/00-realip.conf`:

```nginx
set_real_ip_from 127.0.0.1;
set_real_ip_from ::1;
real_ip_header proxy_protocol;
real_ip_recursive on;
```

### 3.3. Релей

`/etc/nginx/stream-enabled/relay.conf` (создай каталог):

```nginx
stream {
    map $ssl_preread_server_name $relay_backend {
        # --- домены, которые релеим наружу ---
        claude.ai               127.0.0.1:9443;
        a.claude.ai             127.0.0.1:9443;
        console.anthropic.com   127.0.0.1:9443;
        api.anthropic.com       127.0.0.1:9443;
        platform.claude.com     127.0.0.1:9443;
        chatgpt.com             127.0.0.1:9443;
        ab.chatgpt.com          127.0.0.1:9443;
        api.openai.com          127.0.0.1:9443;
        auth.openai.com         127.0.0.1:9443;
        auth0.openai.com        127.0.0.1:9443;
        platform.openai.com     127.0.0.1:9443;
        cdn.oaistatic.com       127.0.0.1:9443;
        files.oaiusercontent.com 127.0.0.1:9443;
        tcr9i.chat.openai.com   127.0.0.1:9443;
        webrtc.chatgpt.com      127.0.0.1:9443;
        android.chat.openai.com 127.0.0.1:9443;
        operator.chatgpt.com    127.0.0.1:9443;
        sora.chatgpt.com        127.0.0.1:9443;
        videos.openai.com       127.0.0.1:9443;
        ios.chat.openai.com     127.0.0.1:9443;
        cdn.platform.openai.com 127.0.0.1:9443;
        developers.openai.com   127.0.0.1:9443;
        sdmntprukwest.oaiusercontent.com        127.0.0.1:9443;
        sdmntpritalynorth.oaiusercontent.com    127.0.0.1:9443;
        sdmntprpolandcentral.oaiusercontent.com 127.0.0.1:9443;
        sdmntprnortheu.oaiusercontent.com       127.0.0.1:9443;
        # --- всё остальное — на локальный сайт ---
        default                 127.0.0.1:8443;
    }

    # приём снаружи
    server {
        listen 443;
        listen [::]:443;
        ssl_preread on;
        proxy_pass $relay_backend;
        proxy_protocol on;          # отдаём PROXY локальным бэкендам
    }

    # выход наружу: принимает PROXY, резолвит SNI и идёт к сайту без PROXY
    server {
        listen 127.0.0.1:9443 proxy_protocol;
        ssl_preread on;
        resolver 1.1.1.1 8.8.8.8 valid=60s ipv6=off;
        resolver_timeout 5s;
        proxy_pass $ssl_preread_server_name:443;
    }
}
```

> Почему динамический `proxy_pass $ssl_preread_server_name:443`, а не фиксированный
> IP: у Anthropic один адрес (`160.79.104.10`), у OpenAI их много и они меняются.
> `resolver` заставляет nginx резолвить имя на лету.

### 3.4. Подключить stream-конфиг в nginx

nginx может держать только один `stream {}`, поэтому это отдельный include на
верхнем уровне. Добавь в конец `/etc/nginx/nginx.conf`:

```nginx
include /etc/nginx/stream-enabled/*.conf;
```

(`map`/`server` внутри `stream {}` — потому что на верхнем уровне `map` запрещён.)

### 3.5. Перенести локальный сайт с 443 на 8443

Найди конфиги сайта:

```bash
grep -rn "listen .*443" /etc/nginx/sites-enabled/
```

Замени в них строку `listen 443 ssl http2;` на:

```nginx
listen 127.0.0.1:8443 ssl http2 proxy_protocol;
```

Массово:

```bash
sed -i 's#listen 443 ssl http2;#listen 127.0.0.1:8443 ssl http2 proxy_protocol;#' \
  /etc/nginx/sites-enabled/*
```

Порт `80` не трогаем (там nginx остаётся напрямую). Проверь, что не осталось
других `listen 443` в `http`-контексте:

```bash
grep -rn "listen .*443" /etc/nginx/sites-enabled/ /etc/nginx/conf.d/
```

### 3.6. Проверка и перезагрузка

```bash
nginx -t
```

Если OK:

```bash
systemctl reload nginx
ss -tlnp | grep -E ':(443|8443|9443)\b'
```

Если `nginx -t` упал — откат:

```bash
cp -a "$BK"/. /etc/nginx/
nginx -t
```

---

## 4. Клиент: прописать домены

На домашней машине добавь в `/etc/hosts` (нужен root):

```
IP_ТВОЕГО_VDS claude.ai
IP_ТВОЕГО_VDS a.claude.ai
IP_ТВОЕГО_VDS console.anthropic.com
IP_ТВОЕГО_VDS api.anthropic.com
IP_ТВОЕГО_VDS platform.claude.com
IP_ТВОЕГО_VDS chatgpt.com
...
```

Готовый скрипт: `client-hosts.sh`. Он:
- вырезает старые записи для этих доменов (любые IP),
- добавляет блок `IP_VDS <домен>`,
- сбрасывает кэш systemd-resolved,
- делает бэкап `/etc/hosts`.

> Если `sudo` без пароля нет, скрипт умеет через `systemd-run --system --wait --pipe`.

**Критично:** выключи в браузере **Secure DNS / DNS-over-HTTPS**, иначе браузер
резолвит мимо `/etc/hosts` и релей не сработает. Chrome: Настройки → Приватность
→ «Использовать безопасный DNS» — выкл. Firefox: `network.trr.mode = 5`.

---

## 5. Проверка

С домашней машины:

```bash
# сайт на VDS цел
curl -sI --resolve hugedev.ru:443:IP_VDS https://hugedev.ru/ | head -1

# релей
curl -sI https://claude.ai/ | grep -iE '^HTTP|^location|cf-mitigated'
```

Ожидаемо:
- `claude.ai` → `403 cf-mitigated: challenge` (или 200) — **без**
  `app-unavailable-in-region`;
- `platform.claude.com` → 200;
- `chatgpt.com` → `403 cf-mitigated: challenge`;
- `api.openai.com/v1/models` → 401 (без ключа — норма);
- `api.anthropic.com` → 404 / 401.

Проверить, что сайт видит реальные IP (а не 127.0.0.1):

```bash
tail -n 3 /var/log/nginx/access.log     # на VDS
```

---

## 6. Добавить новые домены

1. На VDS допиши строки `домен 127.0.0.1:9443;` в `map` файла
   `/etc/nginx/stream-enabled/relay.conf`.
2. `nginx -t && systemctl reload nginx`.
3. На клиенте добавь домен в `/etc/hosts` (через `client-hosts.sh` или вручную).

Домены, которые надо релеить, должны быть **до** `default` в map.

---

## 7. Вариант без сайта на VDS

Если 443 свободен (сайта нет), переносить ничего не надо. Поменяй в `map`:

```nginx
default 127.0.0.1:1;   # отбой для всех прочих SNI (не открытый прокси)
```

И убери из `relay.conf` блок `output`-сервера `9443` не нужно? Нет: он нужен.
Схема та же, просто `default` не идёт на сайт, а закрывается. PROXY protocol
для сайта тоже не нужен (нет сайта) — но оставь как есть, не мешает.

---

## 8. Безопасность

- `default 127.0.0.1:8443` (или отбой) — релей отвечает **только** на SNI из
  списка. Но любой, кто знает IP и SNI, может этим пользоваться.
- Ограничить по домашнему IP (если он статический):
  ```bash
  ufw allow from ТВОЙ_IP to any port 443 proto tcp
  ufw deny 443/tcp
  ```
- Не делай `default -> сам себя` с любым SNI: получится открытый релей.
- TLS не расшифровывается; сертификаты не подменяются.

---

## 9. Частые проблемы

| Симптом | Причина / решение |
|---|---|
| `location: .../app-unavailable-in-region` | VDS в неподдерживаемой стране. Нужен VDS в поддерживаемой. |
| `403 cf-mitigated: challenge`, браузер не проходит | Датацентровый IP VDS. Нужен резидентный IP, либо браузер-капча. |
| Релей не сработал, вернулся старый IP | В браузере включён DoH / включён VPN / кэш DNS. Выключи DoH, `resolvectl flush-caches`. |
| Сайт видит всех как `127.0.0.1` | Забыт `proxy_protocol` в `listen` сайта или `real_ip_header` в `http`. |
| `nginx -t`: `"map" directive is not allowed here` | `map` вынесен на верхний уровень. Он должен быть внутри `stream {}`. |
| `nginx -t`: `bind() ... Address already in use` | Старый `http`-сервер всё ещё слушает 443. Перенеси его на 8443. |
| `certbot renew` сломался | Если использовался плагин с проверкой на 443 (tls-alpn). Переключи на webroot/http-01 (порт 80 не трогали). |
| Домены OpenAI, но IP `8.47.69.0` | На VDS DNS-хайджек. Используй другой resolver в `resolver` (или DoH на стороне клиента не поможет — надо на VDS). |

---

## 10. Откат

```bash
# вернуть nginx
cp -a /root/nginx-backup-XXXX/. /etc/nginx/ && nginx -t && systemctl reload nginx

# вернуть hosts (на клиенте)
grep -E "claude|openai|chatgpt" /etc/hosts    # найти блок
cp -a /etc/hosts.bak-XXXX /etc/hosts
resolvectl flush-caches
```

---

## 11. Альтернативы nginx-stream

- **sniproxy** — умеет SNI-роутинг «из коробки», конфиг проще, но без allowlist
  легко получить открытый прокси.
- **HAProxy** — мощнее, PROXY protocol и SNI-роутинг через `req_ssl_sni`.
- **gost** — один бинарник, `gost -L tls://:443` / SNI-форвардинг.
- **xray/sing-box** — если нужен ещё и обычный прокси-клиент.

nginx удобен тем, что уже стоит почти везде и умеет и сайт, и релей одновременно.

---

## Файлы в этом каталоге

| Файл | Назначение |
|---|---|
| `relay.conf.example` | Шаблон `stream`-конфига |
| `00-realip.conf.example` | Шаблон PROXY protocol для реальных IP |
| `domains.txt` | Список доменов для релея |
| `setup-vds.sh` | Автонастройка VDS (бэкап, конфиги, перенос сайта, reload) |
| `client-hosts.sh` | Прописать домены в `/etc/hosts` на клиенте |
