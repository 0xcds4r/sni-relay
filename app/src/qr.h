#pragma once

#include <QImage>
#include <QString>

// QR-код из текста (libqrencode) с логотипом приложения в центре.
QImage makeQrImage(const QString& text, int target = 300);
