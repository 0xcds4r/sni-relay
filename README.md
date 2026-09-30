# SNI Relay

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%C2%B7%20Windows-informational)](#)
[![Qt6](https://img.shields.io/badge/Qt-6-green)](https://www.qt.io/)
[![Release](https://img.shields.io/github/v/release/0xcds4r/sni-relay?label=release)](https://github.com/0xcds4r/sni-relay/releases)

Свой **SNI-релей** на VDS: открывает заблокированные и гео-ограниченные сайты
(Claude, ChatGPT и т.п.) **без VPN и без прокси-клиента** на домашней машине.

В репозитории: **GUI-приложение на C++/Qt6**, скрипты автонастройки и подробный
гайд. TLS остаётся end-to-end — трафик не расшифровывается, сертификаты не
подменяются.

```
браузер ──TLS(SNI=claude.ai)──► [VDS: nginx stream] ──TCP──► claude.ai
   hosts: claude.ai → IP_ТВОЕГО_VDS
```

## Как это работает

1. `/etc/hosts` на клиенте указывает `claude.ai → IP_ТВОЕГО_VDS`.
2. Клиент идёт на твой VDS:443 и в TLS ClientHello передаёт **SNI** (имя сайта).
3. На VDS `nginx stream` (модуль `ssl_preread`) читает SNI и **проксирует TCP
   наружу** к настоящему сайту — уже с зарубежного IP VDS.
4. TLS не прерывается: клиент видит сертификат настоящего сайта.

Сайт видит IP VDS (поддерживаемая страна), а не твой домашний. То же, что делает
`dns.malw.link`, только релей у тебя свой и без чужого DNS.

## Что в репозитории

| Путь | Назначение |
|---|---|
| [`app/`](app/) | **GUI-приложение** SNI Relay Manager (C++/Qt6, Linux/Windows): VDS, домены, релей, hosts, проверка, **MTProto**, интеграции |
| [`setup-vds.sh`](setup-vds.sh) | Автонастройка VDS: бэкап, конфиги, перенос сайта, `nginx -t`, reload (с откатом) |
| [`client-hosts.sh`](client-hosts.sh) | Прописать/убрать домены в `/etc/hosts` на клиенте |
| [`relay.conf.example`](relay.conf.example) | Шаблон `stream`-конфига релея |
| [`00-realip.conf.example`](00-realip.conf.example) | PROXY protocol — реальные IP для локального сайта |
| [`domains.txt`](domains.txt) | Список доменов для релея (Anthropic + OpenAI) |
| [`MTProto.md`](MTProto.md) | Гайд: **MTProto-прокси (mtg)** на VDS — для Telegram-приложения |
| [`app/README.md`](app/README.md) | Документация приложения и CLI |

## Быстрый старт

### Вариант A — GUI-приложение (рекомендуется)

Скачай из [Releases](https://github.com/0xcds4r/sni-relay/releases/tag/sni-relay-manager):

- **AppImage** (любой дистрибутив): `chmod +x *.AppImage && ./*.AppImage`
- **deb** (Debian/Ubuntu): `sudo apt install ./sni-relay-manager_*_amd64.deb`
- **Windows** (x64): распакуй `sni-relay-manager-*-windows-x64.zip` и запусти
  `sni-relay-manager.exe`. `ssh.exe`/`curl.exe` уже внутри; вход — по SSH-ключу,
  для правки `hosts` нужны права администратора.

Или собери сам (Linux):

```bash
cd app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sni-relay-manager
```

Вкладки приложения: **VDS · Домены · Релей · Клиент · Проверка · MTProto ·
Интеграции · FAQ · О программе**.

1. Вкладка **VDS** — адрес VDS, пользователь, ключ/пароль → «Проверить подключение».
2. Вкладка **Релей** — «Развернуть / обновить».
3. Вкладка **Клиент** — «Прописать домены в /etc/hosts».
4. Вкладка **MTProto** — для Telegram-приложения: «Установить и развернуть» (прокси на 443 через релей), ссылка и QR.
5. Вкладка **Интеграции** — отключить AsyncDns в Chrome, пересечения с zapret, порты.
6. Выключи **Secure DNS (DoH)** в браузере.
7. Вкладка **Проверка** — «Проверить все домены».

CLI без GUI: `--print-config`, `--print-deploy`, `--print-rollback`,
`--print-hosts`, `--ssh-test`, `--print-config-path`, `--mtg-link`,
`--mtg-qr=PATH`, `--version`.

### Вариант B — скрипты (без GUI)

На VDS:
```bash
sudo bash setup-vds.sh
```
На клиенте:
```bash
./client-hosts.sh IP_ТВОЕГО_VDS
```

Подробный ручной вариант — в разделе [«Ручная установка»](#ручная-установка).

## Требования

- **VDS в поддерживаемой стране** (для Claude/ChatGPT — не РФ). Проверка:
  ```bash
  curl -s https://ipinfo.io/json          # country
  curl -sI https://claude.ai/ | grep -iE '^HTTP|^location|cf-mitigated'
  ```
  `location: .../app-unavailable-in-region` → регион не подходит.
  `cf-mitigated: challenge` → регион ок (Cloudflare-челлендж, браузер проходит).
- **nginx с модулем `stream`** (`ssl_preread`), root/sudo.
- На клиенте: `ssh`, `curl`; для прав root — `systemd-run` / `pkexec` / `sudo`.
  На Windows `ssh.exe`/`curl.exe` идут в комплекте, `hosts` правится через
  PowerShell (нужны права администратора).

## Ручная установка

Схема на VDS: порт 443 занимает `stream`-роутер, который по SNI решает —
релеенные домены наружу, всё прочее на локальный сайт (`127.0.0.1:8443`,
реальные IP через PROXY protocol).

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

1. **Бэкап**
   ```bash
   BK=/root/nginx-backup-$(date +%F-%H%M); mkdir -p "$BK" && cp -a /etc/nginx/. "$BK"/
   ```
2. **Реальные IP** — `/etc/nginx/conf.d/00-realip.conf` (см. `00-realip.conf.example`).
3. **Релей** — `/etc/nginx/stream-enabled/relay.conf` (см. `relay.conf.example`).
   Динамический `proxy_pass $ssl_preread_server_name:443` нужен потому, что у
   Anthropic один адрес, а у OpenAI их много и они меняются.
4. **Подключить stream** — в конец `/etc/nginx/nginx.conf`:
   ```nginx
   include /etc/nginx/stream-enabled/*.conf;
   ```
5. **Перенести сайт с 443 на 8443**:
   ```bash
   sed -i 's#listen 443 ssl http2;#listen 127.0.0.1:8443 ssl http2 proxy_protocol;#' /etc/nginx/sites-enabled/*
   ```
6. **Проверить и перезагрузить**: `nginx -t && systemctl reload nginx`.
   При ошибке — откат из `$BK`.

Клиент: добавь домены в `/etc/hosts` (проще — `client-hosts.sh`).
**Критично:** выключи в браузере **Secure DNS / DoH**, иначе он резолвит мимо
`/etc/hosts`. Chrome: Приватность → «Безопасный DNS» — выкл. Firefox:
`network.trr.mode = 5`.

## Добавить новые домены

1. В `relay.conf` (или в списке доменов в приложении) добавь `домен 127.0.0.1:9443;`
   **до** `default`.
2. `nginx -t && systemctl reload nginx`.
3. Добавь домен в `/etc/hosts` на клиенте.

## Проверка

```bash
curl -sI https://claude.ai/ | grep -iE '^HTTP|^location|cf-mitigated'
```
Ожидаемо: `claude.ai`/`chatgpt.com` → `403 cf-mitigated: challenge` (без
`app-unavailable-in-region`); `platform.claude.com` → 200;
`api.openai.com/v1/models` → 401 (без ключа — норма).

Реальные IP сайта (не `127.0.0.1`) видны в `/var/log/nginx/access.log`.

## Частые проблемы

| Симптом | Решение |
|---|---|
| `location: .../app-unavailable-in-region` | VDS в неподдерживаемой стране — нужен другой VDS. |
| `403 cf-mitigated: challenge` не проходит | Датацентровый IP VDS; нужен резидентный IP или браузер-капча. |
| Релей не сработал | В браузере включён DoH/VPN; выключи DoH, `resolvectl flush-caches`. |
| Сайт видит всех как `127.0.0.1` | Забыт `proxy_protocol` в `listen` сайта или `real_ip_header`. |
| `"map" directive is not allowed here` | `map` должен быть внутри `stream {}`. |
| `bind() ... Address already in use` | Старый http-сервер всё ещё слушает 443 — перенеси на 8443. |
| Домены OpenAI дают `8.47.69.0` | DNS-хайджек на VDS — смени `resolver` в `stream`. |

## Безопасность

- `default` ведёт на сайт или в отбой — релей отвечает только на SNI из списка.
  Любой, кто знает IP и SNI, всё равно может воспользоваться → ограничь по
  домашнему IP:
  ```bash
  ufw allow from ТВОЙ_IP to any port 443 proto tcp && ufw deny 443/tcp
  ```
- Не делай `default` → «сам себя» с любым SNI (получится открытый релей).
- TLS не расшифровывается, сертификаты не подменяются.

## Откат

```bash
# nginx на VDS
cp -a /root/nginx-backup-XXXX/. /etc/nginx/ && nginx -t && systemctl reload nginx
# hosts на клиенте
cp -a /etc/hosts.bak-XXXX /etc/hosts && resolvectl flush-caches
```

## Совместимость сборок

Артефакты в Releases собраны на Arch Linux (свежий glibc):

- **AppImage** — Qt6 внутри, требует glibc как у Arch / Fedora latest / Ubuntu 24.04+.
- **deb** — нужен Qt6 (`libqt6widgets6`); на старых (Ubuntu 22.04) может не запуститься.
- **Windows (x64)** — Qt6-DLL идут в zip, ничего доустанавливать не нужно
  (Windows 10+; собран кросс-компилятором MinGW-w64).

## Альтернативы nginx-stream

`sniproxy`, `HAProxy` (`req_ssl_sni`), `gost`, `xray/sing-box`. nginx удобен тем,
что уже стоит почти везде и умеет одновременно и сайт, и релей.

## Лицензия

[MIT](LICENSE) © 0xcds4r
