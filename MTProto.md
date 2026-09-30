# MTProto-прокси на своём VDS (mtg)

Гайд по поднятию личного MTProto-прокси для Telegram на своём VDS.
Решает то, что SNI-релей не может: Telegram-приложение ходит к дата-центрам по
«голым» IP, без SNI, поэтому маршрутизация по имени не работает — нужен прокси.

- Прокси: **mtg v2** (Go, FakeTLS, анти-реплей, маскировка под TLS).
- Проверено на Ubuntu 22.04.
- Один бинарник + systemd, без сборки из исходников.

---

## 0. Когда это нужно

- Telegram Desktop/мобильный не подключается (провайдер режет IP DC `149.154.160.0/20`,
  `91.108.4.0/22`, `91.108.56.0/22`).
- SNI-релей ([`README.md`](README.md)) помогает только для веб-версии и HTTPS-доменов,
  но не для MTProto.

Отличие от SNI-релея: клиент коннектится к **порту прокси** и говорит по
obfuscated MTProto (FakeTLS), а VDS сам ходит к дата-центрам Telegram.

---

## 1. Требования

- VDS с root, доступный по SSH.
- **Свободный порт** (443 обычно занят сайтом/релеем). Смотри занятые:
  ```bash
  ss -tlnp
  ```
  Если 443/80/8443 заняты — возьми, например, `10443`.
- Исходящий доступ к Telegram (обычно есть). Проверка:
  ```bash
  timeout 5 bash -c 'echo > /dev/tcp/149.154.167.50/443' && echo OK || echo FAIL
  ```

> ⚠️ Если прокси на том же IP, что и сайт — блокировка IP из-за прокси уронит и сайт.
> Лучше отдельный VDS.

---

## 2. Установка mtg

```bash
cd /tmp
curl -sSL -o mtg.tar.gz \
  https://github.com/9seconds/mtg/releases/download/v2.2.8/mtg-2.2.8-linux-amd64.tar.gz
tar xzf mtg.tar.gz
install -m755 mtg-2.2.8-linux-amd64/mtg /usr/local/bin/mtg
rm -rf mtg.tar.gz mtg-2.2.8-linux-amd64
mtg --version
```

> Для свежей версии замени `v2.2.8` на актуальную с
> https://github.com/9seconds/mtg/releases (архив `mtg-<ver>-linux-amd64.tar.gz`,
> внутри подкаталог с бинарём `mtg`).

---

## 3. Секрет (FakeTLS + domain fronting)

```bash
mtg generate-secret www.google.com
```

Выведет строку — это **секрет**. Формат: байт `0xee` + 32 hex + hex(fronting-домен),
в base64. Секрет = пароль к прокси, не публикуй.

- `www.google.com` — домен маскировки (в TLS ClientHello уйдёт его SNI). Бери
  популярный, незаблокированный, с TLS 1.3.
- hex-вариант (`-x`) — если нужен `ee...` явно.

---

## 4. Конфиг `/etc/mtg.toml`

```toml
secret = "<СЕКРЕТ_ИЗ_ШАГА_3>"
bind-to = "0.0.0.0:10443"

concurrency = 8192
prefer-ip = "prefer-ipv4"
public-ipv4 = "<IP_ТВОЕГО_VDS>"

# Допуск рассинхрона часов клиента/сервера (по умолчанию 5s — часто мало!)
tolerate-time-skewness = "30s"

# debug = true   # включать только для отладки, очень болтливо

# Разрешить приём PROXY protocol (нужно, если прокси стоит ЗА релеем/балансером)
# proxy-protocol-listener = true
```

```bash
chmod 600 /etc/mtg.toml
```

### Полезные опции (по умолчанию ок)

| Опция | Зачем |
|---|---|
| `concurrency` | лимит одновременных соединений |
| `prefer-ip` | `prefer-ipv6`/`prefer-ipv4`/`only-ipv4` — выбор транспорта к DC |
| `public-ipv4/6` | чтобы `mtg access` не ходил во внешний сервис за IP |
| `allow-fallback-on-unknown-dc` | если клиент просит неизвестный DC |
| `[network] dns` | свой DoH-резолвер, напр. `"https://1.1.1.1"` |
| `[defense.anti-replay]` | защита от active probing (вкл. по умолчанию) |
| `[defense.doppelganger]` | имитация таймингов трафика реального сайта |
| `[domain-fronting]` | `ip`/`port`/`proxy-protocol` для связи с фронт-доменом |

---

## 5. systemd-сервис

`/etc/systemd/system/mtg.service`:

```ini
[Unit]
Description=mtg MTProto proxy
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/mtg run /etc/mtg.toml
Restart=always
RestartSec=3
LimitNOFILE=65536
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

```bash
systemctl daemon-reload
systemctl enable --now mtg
systemctl status mtg --no-pager
ss -tlnp | grep 10443
```

---

## 6. Ссылка для клиента

```bash
mtg access /etc/mtg.toml
```

Выведет JSON с `tg://proxy?...` и `t.me/proxy?...`, плюс secret в hex/base64.

**Подключение в Telegram:** на телефоне открыть `t.me/proxy?...` (Telegram сам
предложит «Подключить прокси») или вручную:
Настройки → Данные и память → Прокси → Добавить прокси:
- Сервер: `IP_VDS`
- Порт: `10443`
- Secret: `<секрет>`

Проверка: в Telegram → Прокси статус **Connected** и виден ping.

---

## 7. Диагностика

Логи:

```bash
journalctl -u mtg -n 50 --no-pager
```

Временно включить подробный лог: `debug = true` в `/etc/mtg.toml` → `systemctl restart mtg`.

Частые случаи:

| Лог / симптом | Причина / решение |
|---|---|
| `timestamp ... is too old ~Ns` → `cannot read client hello` | рассинхрон часов клиента. Увеличь `tolerate-time-skewness = "30s"` (и включи авто-время на телефоне). |
| `cannot read client hello ... i/o timeout` | это проба/сканер или оборванный коннект — норма. |
| `connecting...` бесконечно / статус «недоступен» | оператор режет нестандартный порт — выведи прокси на 443 (см. раздел «Вывести прокси на порт 443»). Либо рассинхрон часов клиента → авто-дата/время. |
| `address already in use` | порт занят (`ss -tlnp`), выбери другой. |
| `Validate SNI-DNS match ❌` в `mtg doctor` | это норма для domain fronting (фронт-домен резолвится не в твой IP). Не ошибка. |
| не подключается с телефона, а с ПК ок | мобильный оператор может резать нестандартный порт — попробуй 443. |

`mtg doctor /etc/mtg.toml` — проверка тайм-дрифта, доступности DC и фронт-домена.

---

## 8. Вывести прокси на порт 443 (если занят сайтом/релеем)

443 лучший для маскировки (TLS на 443 — норма). Если 443 занят nginx-релеем,
можно раздать его через SNI-роутинг: клиент FakeTLS шлёт SNI = фронт-домен,
релей маршрутизирует его на mtg.

1. mtg слушает локально:
   ```toml
   bind-to = "127.0.0.1:9999"
   proxy-protocol-listener = true
   ```
2. В `map` релея (`/etc/nginx/stream-enabled/relay.conf`) добавить **до** `default`:
   ```nginx
   www.google.com 127.0.0.1:9999;
   ```
3. `nginx -t && systemctl reload nginx`.
4. Порт в ссылке = `443`.

> Учти: если релей-конфиг генерируется автоматически (например, GUI-приложением
> из этого репозитория), ручная строка в `relay.conf` затрётся при следующем
> деплое — добавь фронт-домен в источник правды (список доменов приложения/скрипт).

---

## 9. Безопасность и риск-заметки

- **Секрет = пароль.** Не шарить; периодически менять (`mtg generate-secret` →
  новый секрет в конфиг → restart → новый link).
- **SNI ≠ адресат** (фронт-домен указывает на Google, а IP — VDS) — детектируемый
  признак доменного фронтинга для продвинутого DPI.
- **Нестандартный порт** повышает заметность; 443 — лучше.
- **Отдельный IP** для прокси, чтобы блок IP не уронил сайт.
- `prefer-ipv4` — если у VDS нет рабочего IPv6.
- Ротация фронт-домена: `mtg generate-secret <другой-домен>`.

---

## 10. Обновление / удаление

Обновление: скачать новый архив, заменить `/usr/local/bin/mtg`, `systemctl restart mtg`.

Удаление:

```bash
systemctl disable --now mtg
rm /etc/systemd/system/mtg.service /etc/mtg.toml /usr/local/bin/mtg
systemctl daemon-reload
```

---

## 11. Альтернативы

- **Официальный MTProxy** (TelegramMessenger/MTProxy) — C, собирается из исходников,
  без FakeTLS/анти-реплея из коробки.
- **mtg** (этот гайд) — проще, FakeTLS, anti-replay, doppelganger.
- **xray/sing-box + MTProto** — если нужен и обычный прокси.
