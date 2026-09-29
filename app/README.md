# SNI Relay Manager

GUI-приложение на C++/Qt6 для управления своим SNI-релеем на VDS:
развёртывание, домены, `/etc/hosts` на клиенте и проверка — в одном окне.

Обёртка над гайдом из родительского каталога: те же действия, без ручного
редактирования конфигов.

## Возможности

- **VDS** — хост, порт, пользователь, ключ или пароль (SSH), кнопка
  «Проверить подключение» (geo, nginx, stream-модуль, доступность `claude.ai`).
- **Домены** — редактируемый список доменов для релея + SNI локального сайта;
  импорт/экспорт `.txt`, сброс к списку по умолчанию.
- **Релей** — «Развернуть/обновить» (бэкап `/etc/nginx`, конфиг, перенос сайта
  на 8443, `nginx -t`, reload, автооткат при ошибке) и «Откатить».
- **Клиент** — прописать/убрать домены в `/etc/hosts` (через
  systemd-run/pkexec/sudo), показать текущие записи.
- **Проверка** — прогон всех доменов через релей с цветной таблицей:
  `OK` / `challenge` / `REGION BLOCK` / `нет ответа`.
- **Лог** — весь вывод выполняемых команд.

Пароли и настройки хранятся в `~/.config/sni-relay-manager/config.json`
(пароль — только если включена галка «Сохранить»).

## Зависимости

- Qt6 (Widgets), CMake ≥ 3.16, компилятор C++17, Ninja/Make
- Системные утилиты: `ssh`, `curl`, `setsid`; для прав root —
  `systemd-run` / `pkexec` / `sudo`

Arch/CachyOS: `sudo pacman -S qt6-base cmake ninja gcc`
Ubuntu/Debian: `sudo apt install qt6-base-dev cmake ninja-build g++`

## Сборка и запуск

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sni-relay-manager
```

Установка в систему (опционально):

```bash
sudo cmake --install build          # /usr/local/bin/sni-relay-manager
```

## CLI (без GUI)

Удобно для скриптов и проверки:

```bash
sni-relay-manager --print-config     # показать сгенерированный relay.conf
sni-relay-manager --print-deploy     # показать скрипт развёртывания
sni-relay-manager --print-rollback   # показать скрипт отката
sni-relay-manager --print-hosts      # показать новый /etc/hosts
sni-relay-manager --print-config --host=1.2.3.4
```

## Как пользоваться

1. Вкладка **VDS**: адрес, пользователь, ключ (или пароль) → «Проверить подключение».
2. Вкладка **Домены**: при необходимости отредактируй список.
3. Вкладка **Релей**: «Развернуть / обновить».
4. Вкладка **Клиент**: «Прописать домены в /etc/hosts».
5. Выключи Secure DNS (DoH) в браузере.
6. Вкладка **Проверка**: «Проверить все домены».

## Структура

```
src/settings.h       настройки + JSON-персистенция
src/generator.{h,cpp} генерация relay.conf / скриптов / hosts (чистые функции)
src/mainwindow.*     UI и запуск процессов (ssh, curl, elevation)
src/main.cpp         точка входа + CLI-режимы
```

## Ограничения

- SSH-пароль передаётся через `SSH_ASKPASS` (временный helper), ключ
  предпочтительнее.
- Для прав root используется первый доступный способ; `systemd-run --system`
  на многих системах работает без запроса пароля.
