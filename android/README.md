# SNI Relay Manager for Android

Мобильный пульт управления SNI-релеем: Jetpack Compose UI, SSH к VDS, проверка доменов, управление MTProto и локальный VPN для подмены DNS relay-доменов без root.

## Возможности

- VDS: SSH по паролю или приватному ключу (включая OpenSSH Ed25519), проверка nginx и доступности сайтов.
- Домены и параметры локального сайта на VDS.
- Развёртывание и откат nginx-релея.
- Клиентский `VpnService`: DNS relay-доменов возвращает IP VDS; остальной трафик не туннелируется.
- Параллельная проверка доменов через VDS с сохранением SNI и TLS-проверки.
- Управление mtg: установка, генерация FakeTLS-секрета, конфигурация, systemd-сервис, QR, ссылка и статус.
- Проверка портов на VDS, FAQ и журнал операций.

Chrome AsyncDns и zapret относятся к десктопным системам и на Android не применяются.

## Сборка

Нужны JDK 21, Android SDK Platform 36 и Android Gradle Plugin/Gradle из wrapper.

```bash
cd android
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk
./gradlew assembleDebug
```

Установка debug APK:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Release APK подписывается локальным keystore через `keystore.properties`; файл и keystore не коммитить. Публичный APK: `app/build/outputs/apk/release/app-release.apk`.

## VPN и DNS

Android не позволяет обычному приложению изменять системный `/etc/hosts`. Локальный VPN реализует эквивалентную подмену DNS без root. VPN перехватывает DNS, для заданных relay-доменов отвечает IP VDS, остальные запросы пересылает в `1.1.1.1`. HTTPS-соединения идут напрямую к VDS, где nginx выбирает backend по исходному SNI.

Отключите Secure DNS/DoH в приложениях, которые должны использовать системный DNS: собственный DoH обходит DNS VPN. VPN-профиль Android отображается как VPN, но не маршрутизирует весь интернет через VDS.

## Безопасность

- SSH host key проверяется отключённым (`StrictHostKeyChecking=no`) как в текущем desktop-приложении; подключайтесь только к доверенному VDS.
- Пароль не сохраняется без явного включения соответствующего флага. SSH-ключ хранится в приватной директории приложения.
- FakeTLS-секрет хранится в локальных настройках приложения; журнал маскирует секрет.
- Не включайте VPN при пустом или неверном IP VDS.
