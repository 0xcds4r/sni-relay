#pragma once

#include <QString>
#include "settings.h"

// Чистые функции генерации конфигов/файлов — без UI, чтобы их можно было
// использовать и из GUI, и из CLI (--print-config).
namespace gen {

QString relayConf(const Settings& s);
QString deployScript(const Settings& s);
QString rollbackScript(const Settings& s);
// currentHosts — текущее содержимое /etc/hosts; addRelay=true добавить блок релея.
QString hostsFile(const Settings& s, bool addRelay, const QString& currentHosts);

// --- VLESS + Reality (Xray) ---
QString realityInstall();                       // установка xray на VDS
QString realityGenKeys();                        // uuid + x25519 + shortId
QString realityServerConfig(const Settings& s);  // config.json для VDS
QString realitySidecar(const Settings& s);       // /etc/sni-relay-reality.json
QString realityDeployScript(const Settings& s);  // запись конфига + сервис
QString realityStatus(const Settings& s);        // маркеры VER/UNIT/ACTIVE/PORT/CFG/SIDE_B64
QString realityRemove();                         // удаление с VDS
QString realityClientLink(const Settings& s);    // vless:// ссылка для клиента
QString realityClientConfig(const Settings& s);  // config.json локального Xray-клиента (SOCKS+HTTP)
QString realityTunConfig(const Settings& s);     // конфиг sing-box (TUN + VLESS) для системного туннеля

} // namespace gen
