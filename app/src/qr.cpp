#include "qr.h"

#include <QPainter>
#include <QPixmap>
#include <QCoreApplication>

#include <qrencode.h>

QImage makeQrImage(const QString& text, int target) {
    const QByteArray data = text.toUtf8();
    // Уровень H (высокая избыточность) — чтобы логотип в центре не мешал сканированию.
    QRcode* qr = QRcode_encodeString(data.constData(), 0, QR_ECLEVEL_H, QR_MODE_8, 1);
    if (!qr) return QImage();

    const int n = qr->width;
    const int quiet = 4;
    int scale = target / (n + 2 * quiet);
    if (scale < 2) scale = 2;
    const int dim = (n + 2 * quiet) * scale;

    QImage img(dim, dim, QImage::Format_RGB32);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (qr->data[y * n + x] & 1)
                p.drawRect((x + quiet) * scale, (y + quiet) * scale, scale, scale);
    QRcode_free(qr);

    // Логотип по центру (в белом боксе-«quiet zone»).
    QPixmap logo(":/sni-relay-manager.svg");
    if (logo.isNull()) logo = QPixmap("/usr/share/icons/hicolor/scalable/apps/sni-relay-manager.svg");
    if (logo.isNull()) logo = QPixmap(qApp->applicationDirPath() + "/../assets/sni-relay-manager.svg");
    if (!logo.isNull()) {
        const int box = dim * 24 / 100;          // 24% — безопасно при уровне H
        const int pad = qMax(4, box / 7);
        const QPixmap sc = logo.scaled(box - 2 * pad, box - 2 * pad,
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int x = (dim - box) / 2;
        const int y = (dim - box) / 2;
        p.setBrush(Qt::white);
        p.drawRoundedRect(x, y, box, box, box / 6, box / 6);
        p.drawPixmap(x + (box - sc.width()) / 2, y + (box - sc.height()) / 2, sc);
    }
    p.end();
    return img;
}
