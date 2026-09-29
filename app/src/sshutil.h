#pragma once

#include <QString>
#include <QStringList>
#include <QProcessEnvironment>
#include "settings.h"

// Общие хелперы для запуска ssh (используются и GUI, и CLI).
namespace sshutil {

// Создаёт (если нужно) скрипт-askpass и возвращает путь к нему.
QString askpassPath();

// Окружение для ssh: при пароле — SRM_PASS + SSH_ASKPASS*.
QProcessEnvironment env(const Settings& s);

// Базовые аргументы ssh (до команды, включая user@host).
QStringList baseArgs(const Settings& s);

// Готовые program+args для запуска команды на VDS (stdin = скрипт).
void prepare(const Settings& s, QString& program, QStringList& args);

} // namespace sshutil
