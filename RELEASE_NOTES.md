# SNI Relay Manager 1.3.2

GUI-приложение на C++/Qt6 (Linux, Windows) и Kotlin/Compose (Android) для развёртывания и управления своим **SNI-релеем** на VDS — обход блокировок и гео-ограничений **без VPN и без прокси-клиента** на домашней машине. Плюс управление **MTProto-прокси (mtg)** для Telegram.

Клиент резолвит домен в IP твоего VDS, `nginx stream` читает SNI из TLS ClientHello и передаёт соединение настоящему сайту уже с зарубежного IP. TLS остаётся end-to-end — не расшифровывается, сертификаты не подменяются.

```
браузер ──TLS(SNI=claude.ai)──► [VDS: nginx stream] ──TCP──► claude.ai
```

## Что нового в 1.3.2

Это релиз-исправление (bugfix).

- **Фикс «Развернуть релей» при повторяющихся доменах.** Генератор писал список доменов в nginx-`map` без дедупликации. Если домен встречался дважды (например `twimg.com`), в `map` появлялся дубликат ключа → `nginx -t` падал с `conflicting parameter "twimg.com"` → деплой откатывался. Теперь оба генератора (**desktop — `app/src/generator.cpp`**, **Android — `Gen.kt`**) пропускают уже встреченные домены, включая пересечения со списком доменов сайта и MTProto-фронтом.
- **Android: верная версия в «О программе».** Раньше строка версии была захардкожена (`1.3.1`). Теперь берётся из `BuildConfig.VERSION_NAME`.
- Версия приложения поднята до **1.3.2** (Android `versionCode` 132).

## Что было ранее

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
- **MTProto**, **Интеграции**, **FAQ**, **О программе**.

## Установка

**Arch Linux (pacman):**

```bash
sudo pacman -U sni-relay-manager-1.3.2-1-x86_64.pkg.tar.zst
sni-relay-manager
```

**Debian/Ubuntu (deb):**

```bash
sudo apt install ./sni-relay-manager_1.3.2_amd64.deb
sni-relay-manager
```

**AppImage (любой дистрибутив):**

```bash
chmod +x sni-relay-manager-1.3.2-x86_64.AppImage
./sni-relay-manager-1.3.2-x86_64.AppImage
```

**Просто бинарь:**

```bash
chmod +x sni-relay-manager
./sni-relay-manager    # нужен Qt6 (Widgets + Svg) и libqrencode
```

**Windows (x64):**

```
Распакуй sni-relay-manager-1.3.2-windows-x64.zip и запусти sni-relay-manager.exe
```

**Android (8.0+):** установи `sni-relay-manager-1.3.2-android-universal.apk`. Разреши VPN-подключение, чтобы включить DNS-подмену relay-доменов без root.

## Требования

- **Linux x86_64** — Qt6 (Widgets + Svg) и `libqrencode` для бинаря/deb/pkg; AppImage несёт их внутри.
- **Android 8.0+** — универсальный APK; VPN требует системного подтверждения, SSH поддерживает ключ и пароль.
- **Windows 10+ x64** — всё в комплекте (Qt6-DLL, `ssh.exe`, `curl.exe`); для правки `hosts` — права администратора.
- `ssh`, `curl`; на Linux для прав root — `systemd-run` / `pkexec` / `sudo`.
- VDS в **поддерживаемой стране** (для Claude/ChatGPT — не РФ) с `nginx` и модулем `stream`.

## Артефакты

Все сборки делаются автоматически (GitHub Actions) для Linux, Windows, Android и Arch. Контрольные суммы всех файлов — в приложенном `SHA256SUMS.txt`.

| Файл | Платформа |
|---|---|
| `sni-relay-manager` | Linux, бинарь (Qt6 + libqrencode) |
| `sni-relay-manager-1.3.2-1-x86_64.pkg.tar.zst` | Arch Linux |
| `sni-relay-manager_1.3.2_amd64.deb` | Debian/Ubuntu |
| `sni-relay-manager-1.3.2-x86_64.AppImage` | любой Linux (Qt6 внутри) |
| `sni-relay-manager-1.3.2-windows-x64.zip` | Windows x64 (Qt6 внутри) |
| `sni-relay-manager-1.3.2-android-universal.apk` | Android 8.0+ |

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
5. Вкладка **Интеграции**: отключить AsyncDns в браузере, проверить zapret.
6. Выключить **Secure DNS (DoH)** в браузере.
7. Вкладка **Проверка**: «Проверить все домены».

## Ссылки

- Исходники и полный гайд: [README](https://github.com/0xcds4r/sni-relay#readme)
- MTProto-прокси на VDS: [MTProto.md](https://github.com/0xcds4r/sni-relay/blob/main/MTProto.md)
- Приложение: [`app/`](https://github.com/0xcds4r/sni-relay/tree/main/app)
