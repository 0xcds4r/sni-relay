# SNI Relay Manager 1.4.0

GUI-приложение на C++/Qt6 (Linux, Windows) и Kotlin/Compose (Android) для развёртывания и управления своим **SNI-релеем** на VDS — обход блокировок и гео-ограничений **без VPN и без прокси-клиента** на домашней машине. Плюс управление **MTProto-прокси (mtg)** для Telegram и **VLESS + Reality (Xray)** для сайтов, заблокированных по SNI.

Клиент резолвит домен в IP твоего VDS, `nginx stream` читает SNI из TLS ClientHello и передаёт соединение настоящему сайту уже с зарубежного IP. TLS остаётся end-to-end — не расшифровывается, сертификаты не подменяются.

```
браузер ──TLS(SNI=claude.ai)──► [VDS: nginx stream] ──TCP──► claude.ai
```

## Что нового в 1.4.0

Главное — **VLESS + Reality (Xray)**: обход блокировки **по SNI**, где SNI-релей бессилен.

- **Новая вкладка «Reality» (desktop) / экран Reality (Android).** Автоматически: установка **Xray-core** на VDS, генерация ключей (**UUID**, **x25519** private/public, **shortId**), серверный `config.json` (`security: reality`, `dest`/`serverNames` = маскировочный домен, `flow: xtls-rprx-vision`), systemd-сервис `xray`, старт/стоп/рестарт, статус, удаление, готовая ссылка **`vless://`** и QR.
- **Зачем это нужно.** Провайдер режет соединение по SNI ещё до VDS, поэтому прозрачный SNI-релей не помогает для X/Instagram/Facebook. Reality заставляет клиента идти на VDS с SNI **разрешённого** чужого домена (`dl.google.com` и др.), а настоящий адрес едет внутри шифра — DPI видит обычный TLS к легальному сайту.
- **Паритет desktop ↔ Android.** Обе версии импортируют параметры обратно с VDS (sidecar `/etc/sni-relay-reality.json`), строят `vless://` и QR. Клиент — v2rayNG / Nekoray / sing-box.
- **Подключение из приложения.**
  - **Desktop:** кнопки **«Подключиться / Отключиться»** — поднимается **системный TUN-туннель** через **sing-box** (весь трафик идёт через VDS; sing-box скачивается автоматически в `~/.local/share/sni-relay-manager/`, запуск через `systemd-run`/sudo). «Проверить IP» показывает внешний адрес (должен быть IP VDS).
  - **Android:** кнопка **«Подключиться»** открывает ссылку `vless://` в установленном клиенте (v2rayNG/Nekoray/sing-box); «Проверить IP» показывает, идёт ли трафик через VDS. Полноценный встроенный VPN-режим (нативное ядро) — в следующих версиях.
- **Два транспорта VLESS.** В вкладке — выбор транспорта:
  - **Reality (TCP)** — как раньше (маскировочный домен, x25519-ключи).
  - **gRPC + TLS (Cloudflare)** — origin Xray (`network: grpc`, `serviceName`), клиентская ссылка `vless://…type=grpc&security=tls&serviceName=…` и **JSON** (серверный и клиентский). В Cloudflare: A-запись домена → IP VDS, **Proxy ON**, **gRPC = ON**, **SSL/TLS = Flexible**; клиент идёт на домен:443, origin слушает CF-совместимый порт (по умолчанию 2083).
- **CLI:** `--reality-link` и `--reality-qr=PATH`.
- Версия поднята до **1.4.0** (Android `versionCode` 140).

> Reality — отдельный публичный порт (по умолчанию `2053`), не конфликтует с SNI-релеем на 443. Если порт занят — задай свободный.

## Что было ранее

- 1.3.2 — фикс дедупликации доменов в nginx-`map`; wildcard-домены (`*.example.com` → regex-ключ) и поддержка их в Android-VPN; версия Android в «О программе» из `BuildConfig`.

- 1.3.1 — сборка под **Windows (x64)** (MinGW-w64 + Qt6, `ssh.exe`/`curl.exe` в комплекте, hosts через PowerShell при нехватке прав, ключи SSH, QR встроенным `qrcodegen`); Android (универсальный APK); MTProto-поле порта.
- 1.3.0 — вкладки **MTProto** (mtg: установка, сервис, 443 через релей, QR с логотипом, ссылка `t.me/proxy`) и **Интеграции** (AsyncDns, zapret, порты); параллельная проверка доменов; общий редизайн.
- 1.2.0 — вкладки «О программе» и FAQ.
- 1.1.0 — иконка, тёмная палитра (AppImage), фикс SSH-пароля, конфиг в `~/.config/sni-relay-manager/`, фикс `map_hash_bucket_size`.
- 1.0.0 — базовое приложение.

## Возможности

- **VDS** — SSH (ключ/пароль), проверка geo/nginx/stream.
- **Домены** — список для релея + SNI сайта, импорт/экспорт.
- **Релей** — «Развернуть/обновить» (бэкап `/etc/nginx`, перенос сайта на 8443, `nginx -t`, reload, автооткат) и «Откатить».
- **Клиент** — `hosts` (Linux: systemd-run / pkexec / sudo; Windows: напрямую/PowerShell).
- **Проверка** — параллельный прогон доменов с цветным статусом.
- **MTProto** — Telegram-прокси (mtg).
- **Reality** — VLESS + Reality (Xray): обход блокировок по SNI (X/Instagram/Facebook).
- **Интеграции**, **FAQ**, **О программе**.

## Установка

**Arch Linux (pacman):**

```bash
sudo pacman -U sni-relay-manager-1.4.0-1-x86_64.pkg.tar.zst
sni-relay-manager
```

**Debian/Ubuntu (deb):**

```bash
sudo apt install ./sni-relay-manager_1.4.0_amd64.deb
sni-relay-manager
```

**AppImage (любой дистрибутив):**

```bash
chmod +x sni-relay-manager-1.4.0-x86_64.AppImage
./sni-relay-manager-1.4.0-x86_64.AppImage
```

**Просто бинарь:**

```bash
chmod +x sni-relay-manager
./sni-relay-manager    # нужен Qt6 (Widgets + Svg) и libqrencode
```

**Windows (x64):**

```
Распакуй sni-relay-manager-1.4.0-windows-x64.zip и запусти sni-relay-manager.exe
```

**Android (8.0+):** установи `sni-relay-manager-1.4.0-android-universal.apk` (подписан релизным ключом). Разреши VPN-подключение, чтобы включить DNS-подмену relay-доменов без root.

## Требования

- **Linux x86_64** — Qt6 (Widgets + Svg) и `libqrencode` для бинаря/deb/pkg; AppImage несёт их внутри.
- **Android 8.0+** — универсальный APK; VPN требует системного подтверждения, SSH поддерживает ключ и пароль.
- **Windows 10+ x64** — всё в комплекте (Qt6-DLL, `ssh.exe`, `curl.exe`); для правки `hosts` — права администратора.
- `ssh`, `curl`; на Linux для прав root — `systemd-run` / `pkexec` / `sudo`.
- **Desktop TUN-туннель (Reality):** нужен `sudo` (пароль спросит один раз) и `curl`/`tar`; `sing-box` скачивается автоматически в `~/.local/share/sni-relay-manager/`.
- VDS в **поддерживаемой стране** (для Claude/ChatGPT — не РФ) с `nginx` и модулем `stream`.

## Артефакты

Все сборки делаются автоматически (GitHub Actions) для Linux, Windows, Android и Arch. Контрольные суммы всех файлов — в приложенном `SHA256SUMS.txt`.

| Файл | Платформа |
|---|---|
| `sni-relay-manager` | Linux, бинарь (Qt6 + libqrencode) |
| `sni-relay-manager-1.4.0-1-x86_64.pkg.tar.zst` | Arch Linux |
| `sni-relay-manager_1.4.0_amd64.deb` | Debian/Ubuntu |
| `sni-relay-manager-1.4.0-x86_64.AppImage` | любой Linux (Qt6 внутри) |
| `sni-relay-manager-1.4.0-windows-x64.zip` | Windows x64 (Qt6 внутри) |
| `sni-relay-manager-1.4.0-android-universal.apk` | Android 8.0+ |

Проверка: `sha256sum <файл>`.

## Совместимость

- **AppImage** — несёт Qt6 внутри; требует glibc как у целевой системы.
- **deb / pkg** — используют системный Qt6 (`qt6-base`, `qt6-svg`) и `libqrencode`.
- **Windows (x64)** — Qt6-DLL и утилиты идут в zip, ничего доустанавливать не нужно (Windows 10+).

## Быстрый старт

1. Вкладка **VDS**: адрес, пользователь, ключ/пароль → «Проверить подключение».
2. Вкладка **Релей**: «Развернуть / обновить».
3. Вкладка **Клиент**: «Прописать домены в hosts».
4. Вкладка **MTProto**: «Установить и развернуть» → ссылка/QR для Telegram.
5. Вкладка **Reality**: «Установить Xray и развернуть» → ссылка `vless://` / QR (клиент v2rayNG / Nekoray / sing-box) — для сайтов, заблокированных по SNI.
6. Вкладка **Интеграции**: отключить AsyncDns в браузере, проверить zapret.
7. Выключить **Secure DNS (DoH)** в браузере.
8. Вкладка **Проверка**: «Проверить все домены».

## Ссылки

- Исходники и полный гайд: [README](https://github.com/0xcds4r/sni-relay#readme)
- MTProto-прокси на VDS: [MTProto.md](https://github.com/0xcds4r/sni-relay/blob/main/MTProto.md)
- Приложение: [`app/`](https://github.com/0xcds4r/sni-relay/tree/main/app)
