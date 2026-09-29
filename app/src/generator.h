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

} // namespace gen
