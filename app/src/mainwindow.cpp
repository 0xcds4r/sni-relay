#include "mainwindow.h"
#include "generator.h"
#include "sshutil.h"
#include "version.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QScrollArea>
#include <QFrame>
#include <QProgressBar>
#include <QResizeEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QFont>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QClipboard>
#include <QSplitter>

#include <qrencode.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QScrollArea>
#include <QVector>
#include <memory>

// ---------- вспомогательные ----------

static QString trim(const QString& s) { return s.trimmed(); }

// Латентность до VDS: TCP-connect до 443 (fallback 80). ICMP часто заблокирован.
static QString tcpPing(const QString& host) {
    if (host.isEmpty()) return QString();
    auto measure = [&](const QString& port) -> QString {
        QProcess p;
        const QString sc = QString(
            "t0=$(date +%s%N); if exec 3<>/dev/tcp/%1/%2 2>/dev/null; then t1=$(date +%s%N); "
            "echo $(( (t1-t0)/1000000 )); else echo FAIL; fi").arg(host, port);
        p.start("bash", { "-c", sc });
        if (!p.waitForFinished(4000)) { p.kill(); return QString(); }
        const QString o = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
        if (o.isEmpty() || o == "FAIL") return QString();
        return o + " ms";
    };
    QString r = measure("443");
    if (r.isEmpty()) r = measure("80");
    return r;
}

// QR-код из текста (libqrencode) → QImage.
static QImage makeQrImage(const QString& text, int target = 300) {
    const QByteArray data = text.toUtf8();
    QRcode* qr = QRcode_encodeString(data.constData(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!qr) return QImage();
    const int n = qr->width;
    const int quiet = 4;
    int scale = target / (n + 2 * quiet);
    if (scale < 2) scale = 2;
    const int dim = (n + 2 * quiet) * scale;
    QImage img(dim, dim, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (qr->data[y * n + x] & 1)
                p.drawRect((x + quiet) * scale, (y + quiet) * scale, scale, scale);
    p.end();
    QRcode_free(qr);
    return img;
}

static QString tgProxyUrl(const QString& host, const QString& port, const QString& secret) {
    return QString("https://t.me/proxy?server=%1&port=%2&secret=%3").arg(host, port, secret);
}

// Общий тёмный стиль приложения: карточки, скругления, синий акцент.
static QString appStyleSheet() {
    return QStringLiteral(R"QSS(
        QWidget { color: #e8e8ea; }
        QFrame#card { background: #23242c; border: 1px solid #33353f; border-radius: 14px; }
        QLabel#cardTitle { color: #9aa0ac; font-weight: 600; letter-spacing: 1px; }
        QLabel#mtgStatus, QLabel#statusBox { color: #e8e8ea; background: #1a1b21; border: 1px solid #33353f; border-radius: 12px; padding: 14px; font-size: 11pt; }
        QLabel#hint { color: #8b8f99; }
        QPushButton { background: #2c2e38; color: #e8e8ea; border: 1px solid #3a3c48; border-radius: 10px; padding: 8px 14px; }
        QPushButton:hover { background: #353846; }
        QPushButton:pressed { background: #292b34; }
        QPushButton:disabled { color: #6a6d77; background: #24252b; border-color: #2c2e37; }
        QPushButton#primary { background: #2f6fed; color: #ffffff; border: none; font-weight: 600; }
        QPushButton#primary:hover { background: #3d7cf0; }
        QPushButton#primary:disabled { background: #2a3550; color: #8ea0c8; }
        QPushButton#danger { background: #3a2226; color: #ff9a9a; border: 1px solid #5c3238; }
        QPushButton#danger:hover { background: #472a2f; }
        QPushButton#danger:disabled { background: #2a2327; color: #7a6a6c; border-color: #3a3237; }
        QLineEdit, QSpinBox, QComboBox { background: #1a1b21; color: #e8e8ea; border: 1px solid #3a3c48; border-radius: 10px; padding: 7px 10px; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #2f6fed; }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView { background: #23242c; color: #e8e8ea; selection-background-color: #2f6fed; border: 1px solid #3a3c48; }
        QListWidget { background: #1a1b21; border: 1px solid #3a3c48; border-radius: 10px; padding: 4px; }
        QListWidget::item { padding: 4px; border-radius: 6px; }
        QListWidget::item:selected { background: #2f6fed; color: #ffffff; }
        QTableWidget { background: #1a1b21; border: 1px solid #3a3c48; border-radius: 10px; gridline-color: #33353f; }
        QHeaderView::section { background: #23242c; color: #9aa0ac; border: none; padding: 6px; }
        QPlainTextEdit, QTextBrowser { background: #1a1b21; border: 1px solid #3a3c48; border-radius: 10px; }
        QTabWidget::pane { border: 1px solid #33353f; border-radius: 12px; top: -1px; }
        QTabBar::tab { background: transparent; color: #9aa0ac; padding: 8px 14px; margin: 2px; border-radius: 8px; }
        QTabBar::tab:selected { background: #2c2e38; color: #ffffff; }
        QTabBar::tab:hover { color: #ffffff; }
        QProgressBar { border: none; background: #1a1b21; border-radius: 6px; height: 10px; text-align: center; color: #e8e8ea; }
        QProgressBar::chunk { background: #2f6fed; border-radius: 6px; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #3a3c48; border-radius: 5px; min-height: 24px; }
        QScrollBar::handle:vertical:hover { background: #4a4d5a; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
        QFrame#busyPanel { background: #23242c; border: 1px solid #3a3c48; border-radius: 16px; }
    )QSS");
}

// Извлечь фронт-домен из секрета mtg: [0xEE][16 байт][домен].
static QString mtgFrontFromSecret(const QString& secret) {
    const QString s = secret.trimmed();
    if (s.isEmpty()) return QString();
    QByteArray b;
    static const QRegularExpression hexRe("^[0-9a-fA-F]+$");
    if (s.startsWith("ee") && hexRe.match(s).hasMatch())
        b = QByteArray::fromHex(s.toLatin1());
    else
        b = QByteArray::fromBase64(s.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (b.size() > 17) {
        QString d;
        for (char c : b.mid(17)) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u >= 0x20 && u < 0x7f) d += QChar(u); else break;
        }
        return d.trimmed();
    }
    return QString();
}

void MainWindow::log(const QString& s) {
    if (!m_log) return;
    m_log->appendPlainText(s);
}
void MainWindow::logOk(const QString& s)  { log("[OK] " + s); }
void MainWindow::logErr(const QString& s) { log("[!!] " + s); }

void MainWindow::showBusy(const QString& text) {
    if (!m_busy) {
        m_busy = new QWidget(this);
        m_busy->setObjectName("busy");
        m_busy->setAttribute(Qt::WA_StyledBackground, true);
        m_busy->setStyleSheet("#busy { background: rgba(8,9,12,205); }");
        auto* l = new QVBoxLayout(m_busy);
        l->addStretch();

        auto* panel = new QFrame;
        panel->setObjectName("busyPanel");
        auto* pl = new QVBoxLayout(panel);
        pl->setContentsMargins(30, 26, 30, 26);
        pl->setSpacing(16);

        m_busyLabel = new QLabel;
        m_busyLabel->setAlignment(Qt::AlignCenter);
        m_busyLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 13pt; background: transparent; }");
        m_busyBar = new QProgressBar;
        m_busyBar->setRange(0, 0);          // indeterminate
        m_busyBar->setTextVisible(false);
        m_busyBar->setFixedWidth(340);
        pl->addWidget(m_busyLabel, 0, Qt::AlignHCenter);
        pl->addWidget(m_busyBar, 0, Qt::AlignHCenter);

        auto* center = new QWidget;
        center->setStyleSheet("background: transparent;");
        auto* cwl = new QHBoxLayout(center);
        cwl->setContentsMargins(0, 0, 0, 0);
        cwl->addStretch();
        cwl->addWidget(panel);
        cwl->addStretch();
        l->addWidget(center);
        l->addStretch();
    }
    m_busyLabel->setText(text);
    if (auto* cw = centralWidget()) m_busy->setGeometry(cw->geometry());
    if (!m_busy->isVisible()) {
        m_busy->show();
        if (!m_busyCursor) { QApplication::setOverrideCursor(Qt::BusyCursor); m_busyCursor = true; }
    }
    m_busy->raise();
}

void MainWindow::hideBusy() {
    if (m_busy && m_busy->isVisible()) m_busy->hide();
    if (m_busyCursor) { QApplication::restoreOverrideCursor(); m_busyCursor = false; }
}

void MainWindow::showTextDialog(const QString& title, const QString& content) {
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.resize(780, 580);
    auto* lay = new QVBoxLayout(&dlg);
    auto* te = new QPlainTextEdit;
    te->setReadOnly(true);
    te->setLineWrapMode(QPlainTextEdit::NoWrap);
    { QFont f = te->font(); f.setFamily("monospace"); te->setFont(f); }
    te->setPlainText(content);
    lay->addWidget(te);

    auto* row = new QWidget;
    auto* rl = new QHBoxLayout(row); rl->setContentsMargins(0, 0, 0, 0);
    auto* copy = new QPushButton("Копировать");
    auto* close = new QPushButton("Закрыть");
    close->setObjectName("primary");
    rl->addStretch(); rl->addWidget(copy); rl->addWidget(close);
    lay->addWidget(row);

    connect(copy, &QPushButton::clicked, &dlg, [this, te] {
        QGuiApplication::clipboard()->setText(te->toPlainText());
        logOk("Скопировано в буфер обмена");
    });
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (m_busy && m_busy->isVisible() && centralWidget())
        m_busy->setGeometry(centralWidget()->geometry());
}

bool MainWindow::commandExists(const QString& name) const {
    return !QStandardPaths::findExecutable(name).isEmpty();
}

QString MainWindow::qshell(const QString& s) const {
    QString r = s; r.replace("'", "'\\''");
    return "'" + r + "'";
}

// ---------- запуск процессов ----------

void MainWindow::runProcess(const QString& program, const QStringList& args,
                            const QProcessEnvironment& env, const QString& desc,
                            const QByteArray& stdinData, DoneFn done, bool streamLog) {
    log("→ " + program + " " + args.join(' '));
    auto* p = new QProcess(this);
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::MergedChannels);
    auto buf = std::make_shared<QByteArray>();
    auto once = std::make_shared<bool>(false);

    connect(p, &QProcess::readyReadStandardOutput, this, [this, p, buf, streamLog] {
        QByteArray d = p->readAllStandardOutput();
        buf->append(d);
        if (streamLog) {
            QString t = QString::fromLocal8Bit(d);
            while (t.endsWith('\n') || t.endsWith('\r')) t.chop(1);
            if (!t.isEmpty()) log(t);
        }
    });
    connect(p, &QProcess::finished, this, [this, p, buf, desc, done, once](int code, QProcess::ExitStatus st) {
        if (*once) return;
        *once = true;
        bool ok = (st == QProcess::NormalExit && code == 0);
        if (ok) logOk(desc); else logErr(QString("%1 (код %2)").arg(desc).arg(code));
        if (done) done(ok, QString::fromLocal8Bit(*buf));
        p->deleteLater();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, desc, done, once](QProcess::ProcessError) {
        if (*once) return;
        *once = true;
        logErr(QString("%1: %2").arg(desc, p->errorString()));
        if (done) done(false, p->errorString());
        p->deleteLater();
    });
    p->start(program, args);
    if (!stdinData.isEmpty()) { p->write(stdinData); p->closeWriteChannel(); }
}

void MainWindow::runSsh(const QString& script, const QString& desc, DoneFn done, bool streamLog) {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Не задан хост VDS"); if (done) done(false, QString()); return; }
    QString program; QStringList args;
    sshutil::prepare(m_set, program, args);
    runProcess(program, args, sshutil::env(m_set), desc, script.toUtf8(), done, streamLog);
}

void MainWindow::runElevated(const QString& script, const QString& desc, DoneFn done, bool streamLog) {
    QString tmp = QDir::tempPath() + QString("/srm-elev-%1.sh").arg(QCoreApplication::applicationPid());
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly)) { logErr("не удалось создать временный скрипт"); if (done) done(false, QString()); return; }
        f.write(script.toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                         QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::ExeOwner);
    }
    QString prog; QStringList args;
    if (commandExists("systemd-run")) {
        prog = "systemd-run";
        args = { "--system", "--wait", "--pipe", "--collect", "/bin/bash", tmp };
    } else if (commandExists("pkexec")) {
        prog = "pkexec"; args = { "/bin/bash", tmp };
    } else if (commandExists("sudo")) {
        prog = "sudo"; args = { "-n", "/bin/bash", tmp };
    } else {
        logErr("Нет systemd-run/pkexec/sudo — не могу получить root");
        if (done) done(false, QString());
        return;
    }
    runProcess(prog, args, QProcessEnvironment::systemEnvironment(), desc, {}, done, streamLog);
}

// ---------- генерация конфигов ----------

QString MainWindow::generateRelayConf() const { return gen::relayConf(m_set); }
QString MainWindow::remoteDeployScript() const { return gen::deployScript(m_set); }
QString MainWindow::remoteRollbackScript() const { return gen::rollbackScript(m_set); }

QString MainWindow::buildHosts(bool addRelay) const {
    QString current;
    QFile f("/etc/hosts");
    if (f.open(QIODevice::ReadOnly)) current = QString::fromLocal8Bit(f.readAll());
    return gen::hostsFile(m_set, addRelay, current);
}

// ---------- UI ----------

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_set = Settings::load();
    if (m_set.domains.isEmpty()) m_set.domains = Settings::defaultDomains();
    buildUi();
    toWidgets();
    log(QString("SNI Relay Manager %1. Настрой VDS на вкладке «VDS», затем «Развернуть релей» и «Прописать hosts».").arg(SRM_VERSION));
}

void MainWindow::buildUi() {
    setWindowTitle(QString("SNI Relay Manager %1").arg(SRM_VERSION));
    setStyleSheet(appStyleSheet());

    auto* central = new QWidget(this);
    auto* rootLay = new QVBoxLayout(central);

    auto* split = new QSplitter(Qt::Vertical, central);

    auto* tabs = new QTabWidget(split);

    // Оборачивает содержимое вкладки в карточку (единый стиль).
    auto cardify = [](QWidget* content, const QString& title, bool scroll = true) -> QWidget* {
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(16, 14, 16, 16);
        cl->setSpacing(10);
        if (!title.isEmpty()) {
            auto* t = new QLabel(title);
            t->setObjectName("cardTitle");
            cl->addWidget(t);
        }
        cl->addWidget(content);
        if (!scroll) return card;
        auto* host = new QWidget;
        auto* hl = new QVBoxLayout(host);
        hl->setContentsMargins(14, 14, 14, 14);
        hl->addWidget(card);
        hl->addStretch();
        auto* sc = new QScrollArea;
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        sc->setWidget(host);
        return sc;
    };

    // ===== VDS =====
    {
        auto* w = new QWidget;
        auto* vlay = new QVBoxLayout(w);
        vlay->setContentsMargins(0, 0, 0, 0);
        vlay->setSpacing(12);

        m_vdsStatus = new QLabel("Подключение: не проверялось");
        m_vdsStatus->setObjectName("statusBox");
        m_vdsStatus->setTextFormat(Qt::RichText);
        m_vdsStatus->setWordWrap(true);
        vlay->addWidget(m_vdsStatus);

        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_host = new QLineEdit;
        m_host->setToolTip("IP или домен твоего VDS (в поддерживаемой стране)");
        form->addRow("Хост / IP", m_host);

        m_port = new QLineEdit; m_port->setFixedWidth(100);
        m_port->setToolTip("SSH-порт (обычно 22)");
        form->addRow("SSH порт", m_port);

        m_user = new QLineEdit;
        m_user->setToolTip("SSH-пользователь (обычно root)");
        form->addRow("Пользователь", m_user);

        auto* keyRow = new QWidget;
        auto* keyLay = new QHBoxLayout(keyRow); keyLay->setContentsMargins(0,0,0,0);
        m_keyPath = new QLineEdit;
        m_keyPath->setToolTip("Приватный ключ. Предпочтительнее пароля (пароль не хранится в открытом виде).");
        m_browseKey = new QPushButton("…", keyRow); m_browseKey->setFixedWidth(36);
        keyLay->addWidget(m_keyPath); keyLay->addWidget(m_browseKey);
        form->addRow("Приватный ключ", keyRow);

        m_usePassword = new QCheckBox("Использовать пароль");
        m_usePassword->setToolTip("Если ключа нет — вход по паролю (передаётся через SSH_ASKPASS)");
        form->addRow(QString(), m_usePassword);
        m_password = new QLineEdit; m_password->setEchoMode(QLineEdit::Password);
        m_password->setToolTip("SSH-пароль VDS");
        form->addRow("Пароль", m_password);
        m_savePassword = new QCheckBox("Сохранить пароль в конфиг (небезопасно)");
        m_savePassword->setToolTip("Хранить пароль в ~/.config/... (файл 600). Лучше использовать ключ.");
        form->addRow(QString(), m_savePassword);

        auto* btns = new QWidget;
        auto* bl = new QHBoxLayout(btns); bl->setContentsMargins(0,0,0,0);
        auto* test = new QPushButton("Проверить подключение");
        test->setToolTip("Проверить SSH, geo VDS, nginx и stream-модуль");
        bl->addWidget(test); bl->addStretch();
        form->addRow(btns);

        connect(test, &QPushButton::clicked, this, &MainWindow::onTestConnection);
        connect(m_usePassword, &QCheckBox::toggled, m_password, &QLineEdit::setEnabled);
        connect(m_browseKey, &QPushButton::clicked, this, &MainWindow::browseKey);

        vlay->addLayout(form);
        vlay->addStretch();

        tabs->addTab(cardify(w, "VDS"), "VDS");
    }

    // ===== Домены =====
    {
        auto* w = new QWidget;
        auto* lay = new QHBoxLayout(w);
        auto* left = new QWidget; auto* ll = new QVBoxLayout(left);
        ll->addWidget(new QLabel("Домены для релея:"));
        m_domains = new QListWidget; ll->addWidget(m_domains);
        auto* dr = new QWidget; auto* drl = new QHBoxLayout(dr); drl->setContentsMargins(0,0,0,0);
        auto* add = new QPushButton("Добавить"); add->setToolTip("Добавить домен в список релея");
        auto* rem = new QPushButton("Удалить"); rem->setToolTip("Удалить выбранные домены");
        drl->addWidget(add); drl->addWidget(rem); drl->addStretch();
        ll->addWidget(dr);
        auto* dr2 = new QWidget; auto* dr2l = new QHBoxLayout(dr2); dr2l->setContentsMargins(0,0,0,0);
        auto* imp = new QPushButton("Импорт…"); imp->setToolTip("Загрузить список из .txt (по домену в строке)");
        auto* exp = new QPushButton("Экспорт…"); exp->setToolTip("Сохранить текущий список в .txt");
        auto* rst = new QPushButton("Список по умолчанию"); rst->setToolTip("Сбросить к списку Anthropic + OpenAI");
        dr2l->addWidget(imp); dr2l->addWidget(exp); dr2l->addWidget(rst); dr2l->addStretch();
        ll->addWidget(dr2);
        lay->addWidget(left, 2);

        auto* right = new QWidget; auto* rl = new QVBoxLayout(right);
        rl->addWidget(new QLabel("SNI локального сайта (на VDS):"));
        m_siteDomains = new QListWidget; rl->addWidget(m_siteDomains);
        auto* sr = new QWidget; auto* srl = new QHBoxLayout(sr); srl->setContentsMargins(0,0,0,0);
        auto* sadd = new QPushButton("Добавить"); sadd->setToolTip("Домен, который должен идти на локальный сайт, а не наружу");
        auto* srem = new QPushButton("Удалить");
        srl->addWidget(sadd); srl->addWidget(srem); srl->addStretch();
        rl->addWidget(sr);
        rl->addSpacing(8);
        m_hasSite = new QCheckBox("На VDS есть сайт на 443 (default → сайт)");
        rl->addWidget(m_hasSite);
        m_siteBackend = new QLineEdit;
        auto* sbr = new QWidget; auto* sbrl = new QHBoxLayout(sbr); sbrl->setContentsMargins(0,0,0,0);
        sbrl->addWidget(new QLabel("Бэкенд сайта:")); sbrl->addWidget(m_siteBackend);
        rl->addWidget(sbr);
        m_relayExit = new QLineEdit;
        auto* rer = new QWidget; auto* rerL = new QHBoxLayout(rer); rerL->setContentsMargins(0,0,0,0);
        rerL->addWidget(new QLabel("Локальный выход релея:")); rerL->addWidget(m_relayExit);
        rl->addWidget(rer);
        rl->addStretch();
        lay->addWidget(right, 2);

        connect(add, &QPushButton::clicked, this, &MainWindow::addDomain);
        connect(rem, &QPushButton::clicked, this, &MainWindow::removeDomain);
        connect(imp, &QPushButton::clicked, this, &MainWindow::importDomains);
        connect(exp, &QPushButton::clicked, this, &MainWindow::exportDomains);
        connect(rst, &QPushButton::clicked, this, &MainWindow::resetDomains);
        connect(sadd, &QPushButton::clicked, this, &MainWindow::addSiteDomain);
        connect(srem, &QPushButton::clicked, this, &MainWindow::removeSiteDomain);

        tabs->addTab(cardify(w, "ДОМЕНЫ"), "Домены");
    }

    // ===== Релей =====
    {
        auto* w = new QWidget;
        auto* lay = new QVBoxLayout(w);
        auto* dep = new QPushButton("Развернуть / обновить релей на VDS");
        auto* roll = new QPushButton("Откатить последний бэкап на VDS");
        auto* prev = new QPushButton("Показать генерируемый конфиг");
        lay->addWidget(dep); lay->addWidget(roll); lay->addWidget(prev);
        auto* hint = new QLabel(
            "Развёртывание: делает бэкап /etc/nginx, включает релей, при необходимости переносит\n"
            "локальный сайт с 443 на 127.0.0.1:8443, проверяет nginx -t и перезагружает.\n"
            "При ошибке конфиг автоматически откатывается.");
        hint->setWordWrap(true);
        lay->addWidget(hint);
        lay->addStretch();
        connect(dep, &QPushButton::clicked, this, &MainWindow::onDeploy);
        connect(roll, &QPushButton::clicked, this, &MainWindow::onRollback);
        connect(prev, &QPushButton::clicked, this, [this] {
            fromWidgets();
            showTextDialog("Генерируемый relay.conf",
                           "--- relay.conf ---\n" + generateRelayConf() + "------------------");
        });
        tabs->addTab(cardify(w, "РЕЛЕЙ"), "Релей");
    }

    // ===== Клиент =====
    {
        auto* w = new QWidget;
        auto* lay = new QVBoxLayout(w);
        auto* a = new QPushButton("Прописать домены в /etc/hosts");
        auto* r = new QPushButton("Убрать домены из /etc/hosts");
        auto* sh = new QPushButton("Показать текущие записи hosts");
        lay->addWidget(a); lay->addWidget(r); lay->addWidget(sh);
        auto* hint = new QLabel("Нужны права root: используются systemd-run / pkexec / sudo.\n"
                                "Не забудь выключить Secure DNS (DoH) в браузере.");
        hint->setWordWrap(true);
        lay->addWidget(hint);
        lay->addStretch();
        connect(a, &QPushButton::clicked, this, &MainWindow::onApplyHosts);
        connect(r, &QPushButton::clicked, this, &MainWindow::onRemoveHosts);
        connect(sh, &QPushButton::clicked, this, &MainWindow::onShowHostsEntries);
        tabs->addTab(cardify(w, "КЛИЕНТ"), "Клиент");
    }

    // ===== Проверка =====
    {
        auto* w = new QWidget;
        auto* lay = new QVBoxLayout(w);
        auto* btn = new QPushButton("Проверить все домены (через релей)");
        lay->addWidget(btn);
        m_checkTable = new QTableWidget(0, 4);
        m_checkTable->setHorizontalHeaderLabels({ "Домен", "HTTP", "Статус", "Детали" });
        m_checkTable->horizontalHeader()->setStretchLastSection(true);
        m_checkTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_checkTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        lay->addWidget(m_checkTable);
        connect(btn, &QPushButton::clicked, this, &MainWindow::onCheckAll);
        tabs->addTab(cardify(w, "ПРОВЕРКА"), "Проверка");
    }

    // ===== О программе =====
    {
        auto* w = new QWidget;
        auto* lay = new QVBoxLayout(w);
        auto* label = new QLabel;
        label->setTextFormat(Qt::RichText);
        label->setOpenExternalLinks(true);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        label->setWordWrap(true);
        label->setText(QString(
            "<h2>SNI Relay Manager %1</h2>"
            "<p>GUI для развёртывания и управления своим SNI-релеем на VDS:<br>"
            "домены, <code>/etc/hosts</code>, развёртывание релея и проверка — в одном окне.</p>"
            "<p><b>Автор:</b> 0xcds4r<br>"
            "<b>Лицензия:</b> MIT</p>"
            "<p><b>Ссылки:</b><br>"
            "• <a href=\"https://github.com/0xcds4r/sni-relay\">Репозиторий на GitHub</a><br>"
            "• <a href=\"https://github.com/0xcds4r/sni-relay/releases\">Релизы</a><br>"
            "• <a href=\"https://github.com/0xcds4r/sni-relay/blob/main/MTProto.md\">Гайд по MTProto-прокси</a><br>"
            "• <a href=\"https://github.com/0xcds4r/sni-relay#readme\">Документация (README)</a></p>"
        ).arg(SRM_VERSION));
        lay->addWidget(label);
        lay->addStretch();
        tabs->addTab(cardify(w, "О ПРОГРАММЕ", false), "О программе");
    }

    // ===== FAQ =====
    {
        auto* w = new QWidget;
        auto* lay = new QVBoxLayout(w);
        auto* tb = new QTextBrowser;
        tb->setOpenExternalLinks(true);
        tb->setHtml(
            "<h2>FAQ</h2>"

            "<p><b>После «Прописать hosts» ничего не открывается.</b><br>"
            "Скорее всего включён <b>Secure DNS (DoH)</b> в браузере — он резолвит мимо "
            "<code>/etc/hosts</code>. Выключи его. Проверка: <code>getent ahosts &lt;домен&gt;</code> "
            "должен вернуть IP твоего VDS.</p>"

            "<p><b>Chrome ходит в реальные IP, игнорируя hosts.</b><br>"
            "В Chrome включён встроенный резолвер (AsyncDns). Запусти его с "
            "<code>--disable-features=AsyncDns</code> (можно добавить в <code>~/.config/chromium-flags.conf</code>), "
            "либо в <code>chrome://net-internals/#dns</code> нажми Clear host cache.</p>"

            "<p><b>claude.ai / chatgpt.com показывают «Just a moment…» / 403 cf-mitigated.</b><br>"
            "Это Cloudflare-челлендж из-за датацентрового IP VDS. Браузер обычно проходит его сам. "
            "Если в цикле — нужен резидентный IP.</p>"

            "<p><b>Редирект на «app-unavailable-in-region».</b><br>"
            "Anthropic смотрит на страну твоего VDS. Нужен VDS в поддерживаемой стране (не РФ).</p>"

            "<p><b>Telegram Desktop/мобильный не подключается.</b><br>"
            "SNI-релей тут не поможет: клиент ходит к дата-центрам по «голым» IP (без SNI). "
            "Нужен MTProto-прокси: "
            "<a href=\"https://github.com/0xcds4r/sni-relay/blob/main/MTProto.md\">гайд по mtg</a>.</p>"

            "<p><b>Telegram Web: не грузятся фото/видео.</b><br>"
            "Добавь в релей и в zapret-список <code>cdn.telegram.org</code>, <code>cdn1..6.telegram.org</code>, "
            "<code>*.web.telegram.org</code>, выключи DoH, закрой/перезапусти Chrome.</p>"

            "<p><b>nginx: <code>could not build map_hash</code> при развёртывании.</b><br>"
            "Много длинных доменов в stream-map. Обновись до 1.1.0 (там фикс) или добавь в блок "
            "<code>stream {}</code>: <code>map_hash_bucket_size 128; map_hash_max_size 8192;</code>.</p>"

            "<p><b>Сайт на VDS видит всех как 127.0.0.1.</b><br>"
            "Не проброшены реальные IP: у перенесённого сайта должен быть "
            "<code>proxy_protocol</code> в <code>listen</code> и <code>real_ip_header proxy_protocol;</code>.</p>"

            "<p><b>Часть telegram-хостов с ошибкой (macos/cloud/oauth.telegram.org и т.п.).</b><br>"
            "Это служебные/update/почтовые хосты Telegram, приложению не нужны — не мешают.</p>"

            "<p><b>Где хранится конфиг?</b><br>"
            "<code>~/.config/sni-relay-manager/config.json</code> (пароль — только если включена галка «Сохранить»).</p>"

            "<p><b>Как добавить домен?</b><br>"
            "Вкладка «Домены» → «Добавить» → «Развернуть/обновить» → «Прописать hosts».</p>"

            "<p><b>Как откатить изменения на VDS?</b><br>"
            "Вкладка «Релей» → «Откатить последний бэкап» (восстанавливает <code>/etc/nginx</code>).</p>"
        );
        lay->addWidget(tb);
        tabs->addTab(cardify(w, "FAQ", false), "FAQ");
    }

    // ===== MTProto =====
    {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* w = new QWidget;
        auto* lay = new QVBoxLayout(w);
        lay->setContentsMargins(16, 16, 16, 16);
        lay->setSpacing(14);


        auto addCard = [&](const QString& title) -> QVBoxLayout* {
            auto* card = new QFrame;
            card->setObjectName("card");
            auto* cl = new QVBoxLayout(card);
            cl->setContentsMargins(16, 14, 16, 16);
            cl->setSpacing(10);
            if (!title.isEmpty()) {
                auto* t = new QLabel(title);
                t->setObjectName("cardTitle");
                cl->addWidget(t);
            }
            lay->addWidget(card);
            return cl;
        };

        // --- Карточка: статус и подключение ---
        auto* c1 = addCard("СТАТУС И ПОДКЛЮЧЕНИЕ");
        m_mtgStatus = new QLabel("Нажми «Обновить статус»");
        m_mtgStatus->setObjectName("mtgStatus");
        m_mtgStatus->setTextFormat(Qt::RichText);
        m_mtgStatus->setWordWrap(true);
        c1->addWidget(m_mtgStatus);

        m_mtgLink = new QLabel;
        m_mtgLink->setTextFormat(Qt::RichText);
        m_mtgLink->setOpenExternalLinks(true);
        m_mtgLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_mtgLink->setWordWrap(true);
        m_mtgLink->setVisible(false);
        c1->addWidget(m_mtgLink);

        auto* b3 = new QWidget; auto* l3 = new QHBoxLayout(b3); l3->setContentsMargins(0, 0, 0, 0);
        auto* qr = new QPushButton("Показать QR");
        qr->setCheckable(true);
        qr->setToolTip("Показать/скрыть QR и ссылку");
        auto* copylink = new QPushButton("Копировать ссылку");
        copylink->setToolTip("Скопировать ссылку t.me/proxy");
        auto* addtg = new QPushButton("Добавить в Telegram Desktop");
        addtg->setObjectName("primary");
        addtg->setToolTip("Открыть tg://proxy, чтобы Telegram Desktop добавил прокси автоматически");
        auto* save = new QPushButton("Сохранить QR…");
        l3->addWidget(qr); l3->addWidget(copylink); l3->addWidget(addtg); l3->addWidget(save); l3->addStretch();
        c1->addWidget(b3);

        m_mtgQr = new QLabel;
        m_mtgQr->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_mtgQr->setVisible(false);
        c1->addWidget(m_mtgQr);

        // --- Карточка: параметры ---
        auto* c2 = addCard("ПАРАМЕТРЫ");
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(10);

        m_mtgPort = new QSpinBox;
        m_mtgPort->setRange(1, 65535);
        m_mtgPort->setValue(10443);
        m_mtgPort->setFixedWidth(120);
        m_mtgPort->setToolTip("Порт, на котором слушает прокси. 443/80 обычно заняты сайтом/релеем.");

        m_mtgFront = new QComboBox;
        m_mtgFront->setEditable(true);
        m_mtgFront->addItems({ "www.google.com", "www.cloudflare.com", "www.bing.com", "www.apple.com" });
        m_mtgFront->setToolTip("Домен, под чей TLS маскируется прокси (FakeTLS). Популярный и незаблокированный.");

        auto* secRow = new QWidget;
        auto* secLay = new QHBoxLayout(secRow); secLay->setContentsMargins(0, 0, 0, 0);
        m_mtgSecret = new QLineEdit;
        m_mtgSecret->setPlaceholderText("сгенерировать / вставить…");
        m_mtgSecret->setEchoMode(QLineEdit::Password);
        m_mtgSecret->setToolTip("Секрет прокси (пароль). Никому не передавай.");
        { QFont mono = m_mtgSecret->font(); mono.setFamily("monospace"); m_mtgSecret->setFont(mono); }
        auto* secShow = new QPushButton("Показать"); secShow->setCheckable(true);
        auto* secCopy = new QPushButton("Копировать");
        secCopy->setToolTip("Скопировать секрет в буфер обмена");
        secLay->addWidget(m_mtgSecret); secLay->addWidget(secShow); secLay->addWidget(secCopy);

        form->addRow("Порт прокси", m_mtgPort);
        form->addRow("Фронт-домен", m_mtgFront);
        form->addRow("Secret", secRow);
        c2->addLayout(form);

        connect(secShow, &QPushButton::toggled, this, [this, secShow](bool on) {
            m_mtgSecret->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
            secShow->setText(on ? "Скрыть" : "Показать");
        });
        connect(secCopy, &QPushButton::clicked, this, [this] {
            QGuiApplication::clipboard()->setText(m_mtgSecret->text()); logOk("Секрет скопирован");
        });

        // --- Карточка: управление сервисом ---
        auto* c3 = addCard("УПРАВЛЕНИЕ СЕРВИСОМ");
        auto* b1 = new QWidget; auto* l1 = new QHBoxLayout(b1); l1->setContentsMargins(0, 0, 0, 0);
        m_mtgInstallBtn = new QPushButton("Установить и развернуть");
        m_mtgInstallBtn->setObjectName("primary");
        m_mtgInstallBtn->setToolTip("Установить/обновить mtg, при необходимости сгенерировать секрет и развернуть сервис");
        m_mtgGenBtn = new QPushButton("Сгенерировать секрет");
        m_mtgGenBtn->setToolTip("Сгенерировать новый FakeTLS-секрет под выбранный фронт-домен");
        l1->addWidget(m_mtgInstallBtn); l1->addWidget(m_mtgGenBtn); l1->addStretch();
        c3->addWidget(b1);

        auto* b2 = new QWidget; auto* l2 = new QHBoxLayout(b2); l2->setContentsMargins(0, 0, 0, 0);
        m_mtgStartBtn = new QPushButton("Старт");
        m_mtgStopBtn = new QPushButton("Стоп");
        m_mtgRestartBtn = new QPushButton("Рестарт");
        m_mtgStatusBtn = new QPushButton("Обновить статус");
        m_mtgRemoveBtn = new QPushButton("Удалить mtg и сервис");
        m_mtgRemoveBtn->setObjectName("danger");
        m_mtgRemoveBtn->setToolTip("Остановить и удалить mtg с VDS: сервис, /etc/mtg.toml и бинарь");
        l2->addWidget(m_mtgStartBtn); l2->addWidget(m_mtgStopBtn);
        l2->addWidget(m_mtgRestartBtn); l2->addWidget(m_mtgStatusBtn);
        l2->addWidget(m_mtgRemoveBtn);
        l2->addStretch();
        c3->addWidget(b2);

        auto* hint = new QLabel(
            "MTProto-прокси для Telegram Desktop/мобильного: SNI-релей их не покрывает "
            "(ядро ходит по IP). «Установить и развернуть» поставит mtg, создаст секрет и поднимет сервис.");
        hint->setObjectName("hint");
        hint->setWordWrap(true);
        lay->addWidget(hint);
        lay->addStretch();

        connect(m_mtgInstallBtn, &QPushButton::clicked, this, &MainWindow::onMtgInstallDeploy);
        connect(m_mtgGenBtn, &QPushButton::clicked, this, &MainWindow::onMtgGenSecret);
        connect(m_mtgStartBtn, &QPushButton::clicked, this, &MainWindow::onMtgStart);
        connect(m_mtgStopBtn, &QPushButton::clicked, this, &MainWindow::onMtgStop);
        connect(m_mtgRestartBtn, &QPushButton::clicked, this, &MainWindow::onMtgRestart);
        connect(m_mtgStatusBtn, &QPushButton::clicked, this, &MainWindow::refreshMtgStatus);
        connect(m_mtgRemoveBtn, &QPushButton::clicked, this, &MainWindow::onMtgRemove);
        connect(qr, &QPushButton::toggled, this, [this, qr](bool on) {
            if (on) {
                onMtgShowQr();
                m_mtgLink->setVisible(true);
                m_mtgQr->setVisible(true);
                qr->setText("Скрыть QR");
            } else {
                m_mtgLink->setVisible(false);
                m_mtgQr->setVisible(false);
                qr->setText("Показать QR");
            }
        });
        connect(addtg, &QPushButton::clicked, this, &MainWindow::onMtgOpenTelegram);
        connect(save, &QPushButton::clicked, this, &MainWindow::onMtgSaveQr);
        connect(copylink, &QPushButton::clicked, this, [this] {
            fromWidgets();
            if (m_set.host.trimmed().isEmpty() || m_set.mtgPort.trimmed().isEmpty() || m_set.mtgSecret.trimmed().isEmpty()) {
                logErr("нужны хост VDS, порт и секрет"); return;
            }
            QGuiApplication::clipboard()->setText(tgProxyUrl(m_set.host.trimmed(), m_set.mtgPort.trimmed(), m_set.mtgSecret.trimmed()));
            logOk("Ссылка скопирована");
        });

        scroll->setWidget(w);
        tabs->addTab(scroll, "MTProto");

        // автообновление статуса при переходе на вкладку MTProto
        connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int) {
            if (tabs->tabText(tabs->currentIndex()) == "MTProto") refreshMtgStatus();
        });
    }

    split->addWidget(tabs);

    // ===== Лог =====
    {
        auto* lw = new QWidget;
        auto* ll = new QVBoxLayout(lw);
        auto* top = new QWidget; auto* tl = new QHBoxLayout(top); tl->setContentsMargins(0,0,0,0);
        tl->addWidget(new QLabel("Лог:")); tl->addStretch();
        auto* clr = new QPushButton("Очистить"); tl->addWidget(clr);
        ll->addWidget(top);
        m_log = new QPlainTextEdit; m_log->setReadOnly(true);
        ll->addWidget(m_log);
        connect(clr, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
        split->addWidget(lw);
    }

    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    rootLay->addWidget(split);
    setCentralWidget(central);
    setMinimumSize(820, 600);
    resize(1060, 760);
}

void MainWindow::toWidgets() {
    m_host->setText(m_set.host);
    m_port->setText(m_set.port);
    m_user->setText(m_set.user);
    m_keyPath->setText(m_set.keyPath);
    m_usePassword->setChecked(m_set.usePassword);
    m_password->setText(m_set.password);
    m_password->setEnabled(m_set.usePassword);
    m_savePassword->setChecked(m_set.savePassword);
    m_hasSite->setChecked(m_set.hasSite);
    m_siteBackend->setText(m_set.siteBackend);
    m_relayExit->setText(m_set.relayExit);
    m_domains->clear(); for (const auto& d : m_set.domains) m_domains->addItem(d);
    m_siteDomains->clear(); for (const auto& d : m_set.siteDomains) m_siteDomains->addItem(d);
    { const int p = m_set.mtgPort.toInt(); m_mtgPort->setValue(p > 0 ? p : 10443); }
    m_mtgFront->setCurrentText(m_set.mtgFront);
    m_mtgSecret->setText(m_set.mtgSecret);
}

void MainWindow::fromWidgets() {
    m_set.host = trim(m_host->text());
    m_set.port = trim(m_port->text());
    m_set.user = trim(m_user->text());
    m_set.keyPath = trim(m_keyPath->text());
    m_set.usePassword = m_usePassword->isChecked();
    m_set.password = m_password->text();
    m_set.savePassword = m_savePassword->isChecked();
    m_set.hasSite = m_hasSite->isChecked();
    m_set.siteBackend = trim(m_siteBackend->text());
    m_set.relayExit = trim(m_relayExit->text());
    m_set.domains.clear(); for (int i = 0; i < m_domains->count(); ++i) m_set.domains << m_domains->item(i)->text().trimmed();
    m_set.siteDomains.clear(); for (int i = 0; i < m_siteDomains->count(); ++i) m_set.siteDomains << m_siteDomains->item(i)->text().trimmed();
    m_set.mtgPort = QString::number(m_mtgPort->value());
    m_set.mtgFront = m_mtgFront->currentText().trimmed();
    m_set.mtgSecret = trim(m_mtgSecret->text());
    m_set.save();
}

// ---------- слоты: VDS ----------

void MainWindow::browseKey() {
    QString f = QFileDialog::getOpenFileName(this, "Приватный ключ", QDir::homePath() + "/.ssh");
    if (!f.isEmpty()) m_keyPath->setText(f);
}

void MainWindow::onTestConnection() {
    const QString script = QString::fromUtf8(R"BASH(echo "S_HOST=$(hostname)"
. /etc/os-release 2>/dev/null; echo "S_OS=$PRETTY_NAME"
echo "S_GEO=$(curl -s --max-time 8 https://ipinfo.io/country 2>/dev/null)"
echo "S_NGINX=$(nginx -v 2>&1 | head -1)"
ls /etc/nginx/modules-enabled 2>/dev/null | grep -q stream && echo S_STREAM=yes || echo S_STREAM=no
echo "== listening =="; ss -tlnp 2>/dev/null | grep -E ':(443|8443|9443)\b' || true
)BASH");
    showBusy("Проверка подключения к VDS…");
    runSsh(script, "Проверка подключения к VDS", [this](bool ok, const QString& out) {
        hideBusy();
        QString sHost, sOs, sGeo, sNginx, sStream;
        for (const QString& l : out.split('\n')) {
            const QString t = l.trimmed();
            if (t.startsWith("S_HOST=")) sHost = t.mid(7);
            else if (t.startsWith("S_OS=")) sOs = t.mid(5);
            else if (t.startsWith("S_GEO=")) sGeo = t.mid(6);
            else if (t.startsWith("S_NGINX=")) sNginx = t.mid(8);
            else if (t.startsWith("S_STREAM=")) sStream = t.mid(9);
        }
        if (!ok) {
            m_vdsStatus->setText("<b>Подключение:</b> <span style='color:#ff8f8f'>ошибка</span> — SSH недоступен");
            logErr("проверка подключения не удалась");
            return;
        }
        QString nginx = sNginx;
        { QRegularExpression re("nginx/[0-9.]+"); auto m = re.match(sNginx); if (m.hasMatch()) nginx = m.captured(0); }
        const bool okStream = (sStream == "yes");
        QString html = "<b>Подключение:</b> <span style='color:#59d17a'>OK</span>"
                       " · <b>хост:</b> " + sHost.toHtmlEscaped() +
                       " · <b>OS:</b> " + sOs.toHtmlEscaped() +
                       " · <b>geo:</b> " + sGeo.toHtmlEscaped() +
                       " · <b>nginx:</b> " + nginx.toHtmlEscaped() +
                       " · <b>stream:</b> " + (okStream ? QString("<span style='color:#59d17a'>да</span>")
                                                          : QString("<span style='color:#ff8f8f'>НЕТ</span>"));
        m_vdsStatus->setText(html);
    });
}

// ---------- слоты: домены ----------

void MainWindow::addDomain() {
    bool ok = false;
    QString d = QInputDialog::getText(this, "Добавить домен", "Домен:", QLineEdit::Normal, "", &ok).trimmed();
    if (ok && !d.isEmpty()) m_domains->addItem(d);
    fromWidgets();
}
void MainWindow::removeDomain() {
    for (auto* it : m_domains->selectedItems()) delete it;
    fromWidgets();
}
void MainWindow::importDomains() {
    QString f = QFileDialog::getOpenFileName(this, "Импорт доменов");
    if (f.isEmpty()) return;
    QFile file(f);
    if (!file.open(QIODevice::ReadOnly)) return;
    for (QString line : QString::fromLocal8Bit(file.readAll()).split('\n')) {
        line = line.section('#', 0, 0).trimmed();
        if (!line.isEmpty()) m_domains->addItem(line);
    }
    fromWidgets();
}
void MainWindow::exportDomains() {
    QString f = QFileDialog::getSaveFileName(this, "Экспорт доменов", QDir::homePath() + "/domains.txt");
    if (f.isEmpty()) return;
    QFile file(f);
    if (!file.open(QIODevice::WriteOnly)) return;
    for (int i = 0; i < m_domains->count(); ++i) file.write(m_domains->item(i)->text().toUtf8() + "\n");
    logOk("Экспортировано: " + f);
}
void MainWindow::resetDomains() {
    m_domains->clear();
    for (const auto& d : Settings::defaultDomains()) m_domains->addItem(d);
    fromWidgets();
}
void MainWindow::addSiteDomain() {
    bool ok = false;
    QString d = QInputDialog::getText(this, "SNI сайта", "Домен сайта:", QLineEdit::Normal, "", &ok).trimmed();
    if (ok && !d.isEmpty()) m_siteDomains->addItem(d);
    fromWidgets();
}
void MainWindow::removeSiteDomain() {
    for (auto* it : m_siteDomains->selectedItems()) delete it;
    fromWidgets();
}

// ---------- слоты: релей ----------

void MainWindow::onDeploy() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Укажи хост VDS"); return; }
    log("Развёртывание релея…");
    showBusy("Развёртывание релея…");
    runSsh(remoteDeployScript(), "Развёртывание релея", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("DEPLOY_OK")) logOk("Релей развёрнут и nginx перезагружен");
        else logErr("Развёртывание не удалось — см. вывод выше");
    });
}

void MainWindow::onRollback() {
    fromWidgets();
    showBusy("Откат конфига nginx…");
    runSsh(remoteRollbackScript(), "Откат конфига nginx на VDS", [this](bool, const QString&) { hideBusy(); });
}

// ---------- слоты: клиент ----------

void MainWindow::onApplyHosts() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Укажи хост VDS"); return; }
    QString content = buildHosts(true);
    const QString delim = "SRMEOF_HOSTS_9f3a";
    QString script = "#!/bin/bash\nset -e\n"
                     "cp -a /etc/hosts /etc/hosts.bak-$(date +%F-%H%M%S)\n"
                     "cat > /etc/hosts <<'" + delim + "'\n" + content + delim + "\n"
                     "resolvectl flush-caches 2>/dev/null || true\n"
                     "echo HOSTS_OK\n";
    showBusy("Обновление /etc/hosts…");
    runElevated(script, "Прописать домены в /etc/hosts", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("HOSTS_OK")) logOk("Домены прописаны в /etc/hosts");
        else logErr("Не удалось обновить /etc/hosts");
    });
}

void MainWindow::onRemoveHosts() {
    fromWidgets();
    QString content = buildHosts(false);
    const QString delim = "SRMEOF_HOSTS_9f3a";
    QString script = "#!/bin/bash\nset -e\n"
                     "cp -a /etc/hosts /etc/hosts.bak-$(date +%F-%H%M%S)\n"
                     "cat > /etc/hosts <<'" + delim + "'\n" + content + delim + "\n"
                     "resolvectl flush-caches 2>/dev/null || true\n"
                     "echo HOSTS_OK\n";
    showBusy("Обновление /etc/hosts…");
    runElevated(script, "Убрать домены из /etc/hosts", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("HOSTS_OK")) logOk("Домены убраны из /etc/hosts");
        else logErr("Не удалось обновить /etc/hosts");
    });
}

void MainWindow::onShowHostsEntries() {
    fromWidgets();
    QSet<QString> want;
    for (const auto& d : m_set.domains) { QString t = d.trimmed(); if (!t.isEmpty()) want.insert(t); }
    QFile f("/etc/hosts");
    if (!f.open(QIODevice::ReadOnly)) { logErr("не читается /etc/hosts"); return; }
    QStringList found;
    for (const QString& line : QString::fromLocal8Bit(f.readAll()).split('\n')) {
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 1; i < parts.size(); ++i)
            if (want.contains(parts[i])) { found << line; break; }
    }
    const QString body = found.isEmpty() ? QString("(нет)\n") : found.join('\n') + "\n";
    showTextDialog("Записи /etc/hosts",
                   "--- записи /etc/hosts для доменов релея ---\n" + body + "------------------------------------------");
}

// ---------- слоты: проверка ----------

void MainWindow::setCheckRow(int row, const QString& domain, const QString& http,
                             const QString& status, const QString& details) {
    auto* d = new QTableWidgetItem(domain);
    auto* h = new QTableWidgetItem(http);
    auto* s = new QTableWidgetItem(status);
    auto* de = new QTableWidgetItem(details);
    QColor col;
    if (status.startsWith("REGION")) col = Qt::red;
    else if (status == "challenge") col = QColor(200, 140, 0);
    else if (status == "OK") col = QColor(0, 150, 0);
    else if (status == "нет ответа") col = Qt::red;
    if (col.isValid()) { s->setForeground(col); h->setForeground(col); }
    m_checkTable->setItem(row, 0, d);
    m_checkTable->setItem(row, 1, h);
    m_checkTable->setItem(row, 2, s);
    m_checkTable->setItem(row, 3, de);
}

void MainWindow::onCheckAll() {
    fromWidgets();
    m_checkTable->setRowCount(0);
    m_checkQueue.clear();
    for (const auto& d : m_set.domains) { QString t = d.trimmed(); if (!t.isEmpty()) m_checkQueue << t; }
    m_checkIdx = 0;
    m_checkActive = 0;
    if (m_checkQueue.isEmpty()) { logErr("Список доменов пуст"); return; }
    m_checkTable->setRowCount(m_checkQueue.size());
    for (int i = 0; i < m_checkQueue.size(); ++i)
        m_checkTable->setItem(i, 0, new QTableWidgetItem(m_checkQueue[i]));
    log("Проверка " + QString::number(m_checkQueue.size()) + " доменов (параллельно)…");
    showBusy(QString("Проверка доменов… 0/%1").arg(m_checkQueue.size()));
    const int concurrency = 12;
    for (int i = 0; i < concurrency; ++i) launchCheckTask();
}

void MainWindow::launchCheckTask() {
    if (m_checkIdx >= m_checkQueue.size()) {
        if (m_checkActive == 0) { hideBusy(); logOk("Проверка завершена"); }
        return;
    }
    const int row = m_checkIdx;
    const QString d = m_checkQueue[m_checkIdx++];
    m_checkActive++;
    QStringList args{ "-s", "-o", "/dev/null", "-D", "-", "-m", "10", "--max-time", "10", "https://" + d + "/" };
    runProcess("curl", args, QProcessEnvironment::systemEnvironment(), "проверка " + d, {},
        [this, row, d](bool, const QString& out) {
            QString http, status = "нет ответа", details;
            for (const QString& ln : out.split('\n')) {
                const QString l = ln.trimmed();
                if (l.startsWith("HTTP/")) {
                    const QStringList p = l.split(' ');
                    if (p.size() >= 2) http = p[1];
                } else if (l.startsWith("location:")) {
                    details = l.mid(9).trimmed();
                } else if (l.toLower().startsWith("cf-mitigated:")) {
                    details = "cf-mitigated: " + l.section(':', 1).trimmed();
                }
            }
            if (details.contains("app-unavailable-in-region")) status = "REGION BLOCK";
            else if (details.contains("cf-mitigated")) status = "challenge";
            else if (!http.isEmpty() && (http.startsWith('2') || http.startsWith('3'))) status = "OK";
            else if (!http.isEmpty()) status = "HTTP " + http;
            setCheckRow(row, d, http, status, details);
            m_checkActive--;
            showBusy(QString("Проверка доменов… %1/%2").arg(m_checkIdx - m_checkActive).arg(m_checkQueue.size()));
            launchCheckTask();
        }, false);
}

// ---------- MTProto (mtg) ----------

void MainWindow::onMtgInstallDeploy() {
    fromWidgets();
    const QString install = R"BASH(set -e
case "$(uname -m)" in x86_64) A=amd64;; aarch64) A=arm64;; *) A=amd64;; esac
VER=$(curl -s --max-time 20 https://api.github.com/repos/9seconds/mtg/releases/latest | grep -oE '"tag_name": *"v[0-9.]+"' | grep -oE 'v[0-9.]+')
[ -n "$VER" ] || { echo NO_VER; exit 1; }
echo "mtg $VER ($A)"
cd /tmp
curl -sSL --max-time 120 -o mtg.tar.gz "https://github.com/9seconds/mtg/releases/download/$VER/mtg-${VER#v}-linux-$A.tar.gz"
D="mtg-${VER#v}-linux-$A"
tar xzf mtg.tar.gz
install -m755 "$D/mtg" /usr/local/bin/mtg
rm -rf mtg.tar.gz "$D"
/usr/local/bin/mtg --version
echo MTG_INSTALLED
)BASH";
    showBusy("Установка mtg на VDS…");
    runSsh(install, "Установка mtg на VDS", [this](bool ok, const QString& out) {
        if (!ok || !out.contains("MTG_INSTALLED")) {
            hideBusy(); logErr("установка mtg не удалась"); refreshMtgStatus(); return;
        }
        if (m_set.mtgSecret.trimmed().isEmpty()) {
            const QString front = m_set.mtgFront.trimmed().isEmpty() ? QString("www.google.com") : m_set.mtgFront.trimmed();
            showBusy("Генерация секрета…");
            runSsh(QString("set -e\ncommand -v mtg >/dev/null || { echo NO_MTG; exit 1; }\nmtg generate-secret %1\n").arg(qshell(front)),
                   "Генерация секрета mtg", [this](bool ok2, const QString& out2) {
                if (ok2) {
                    QString sec;
                    for (const QString& l : out2.split('\n', Qt::SkipEmptyParts)) {
                        const QString t = l.trimmed();
                        if (!t.contains(' ') && t.size() >= 20 && !t.startsWith("->") && !t.startsWith("set "))
                            sec = t;
                    }
                    if (!sec.isEmpty()) { m_mtgSecret->setText(sec); fromWidgets(); logOk("Секрет сгенерирован"); }
                }
                onMtgDeploy();   // дальше — развёртывание (свой busy + refresh)
            });
        } else {
            onMtgDeploy();
        }
    });
}

void MainWindow::onMtgGenSecret() {
    fromWidgets();
    const QString front = m_set.mtgFront.trimmed().isEmpty() ? QString("www.google.com") : m_set.mtgFront.trimmed();
    const QString script = QString("set -e\ncommand -v mtg >/dev/null || { echo NO_MTG; exit 1; }\nmtg generate-secret %1\n").arg(qshell(front));
    showBusy("Генерация секрета…");
    runSsh(script, "Генерация секрета mtg", [this](bool ok, const QString& out) {
        hideBusy();
        if (!ok) { logErr("не удалось сгенерировать секрет (mtg установлен?)"); return; }
        QString sec;
        for (const QString& l : out.split('\n', Qt::SkipEmptyParts)) {
            const QString t = l.trimmed();
            if (!t.contains(' ') && t.size() >= 20 && !t.startsWith("->") && !t.startsWith("set "))
                sec = t;
        }
        if (sec.isEmpty()) { logErr("секрет не распознан"); return; }
        m_mtgSecret->setText(sec);
        fromWidgets();
        logOk("Секрет получен и сохранён в конфиг");
    });
}

void MainWindow::onMtgDeploy() {
    fromWidgets();
    if (m_set.mtgSecret.trimmed().isEmpty()) { hideBusy(); logErr("Сначала сгенерируй секрет"); refreshMtgStatus(); return; }
    const QString port = m_set.mtgPort.trimmed().isEmpty() ? QString("10443") : m_set.mtgPort.trimmed();
    const QString ip = m_set.host.trimmed();
    const bool ip4 = QRegularExpression("^[0-9.]+$").match(ip).hasMatch();

    QString toml;
    toml += "secret = \"" + m_set.mtgSecret.trimmed() + "\"\n";
    toml += "bind-to = \"0.0.0.0:" + port + "\"\n";
    toml += "concurrency = 8192\n";
    toml += "prefer-ip = \"prefer-ipv4\"\n";
    if (ip4) toml += "public-ipv4 = \"" + ip + "\"\n";
    toml += "tolerate-time-skewness = \"30s\"\n";

    const QString unit =
        "[Unit]\nDescription=mtg MTProto proxy\nAfter=network-online.target\nWants=network-online.target\n\n"
        "[Service]\nExecStart=/usr/local/bin/mtg run /etc/mtg.toml\nRestart=always\nRestartSec=3\n"
        "LimitNOFILE=65536\nNoNewPrivileges=true\n\n[Install]\nWantedBy=multi-user.target\n";

    QString script;
    script += "set -e\ncommand -v mtg >/dev/null || { echo NO_MTG; exit 1; }\n";
    script += "cat > /etc/mtg.toml <<'EOF_TOML'\n" + toml + "EOF_TOML\n";
    script += "chmod 600 /etc/mtg.toml\n";
    script += "cat > /etc/systemd/system/mtg.service <<'EOF_UNIT'\n" + unit + "EOF_UNIT\n";
    script += "systemctl daemon-reload\n";
    script += "systemctl enable mtg >/dev/null 2>&1 || true\n";
    script += "systemctl restart mtg\n";
    script += "sleep 1\n";
    script += "systemctl is-active mtg\n";
    script += "ss -tlnp | grep ':" + port + "' || true\n";
    script += "echo MTG_DEPLOYED\n";
    showBusy("Развёртывание сервиса…");
    runSsh(script, "Развёртывание mtg на VDS", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("MTG_DEPLOYED")) logOk("MTProto-прокси развёрнут");
        else logErr("Развёртывание mtg не удалось");
        refreshMtgStatus();
    });
}

void MainWindow::onMtgStart()   { fromWidgets(); showBusy("Старт mtg…"); runSsh("systemctl start mtg && systemctl is-active mtg", "Старт mtg", [this](bool, const QString&) { hideBusy(); refreshMtgStatus(); }); }
void MainWindow::onMtgStop()    { fromWidgets(); showBusy("Стоп mtg…"); runSsh("systemctl stop mtg || true", "Стоп mtg", [this](bool, const QString&) { hideBusy(); refreshMtgStatus(); }); }
void MainWindow::onMtgRestart() { fromWidgets(); showBusy("Рестарт mtg…"); runSsh("systemctl restart mtg && systemctl is-active mtg", "Рестарт mtg", [this](bool, const QString&) { hideBusy(); refreshMtgStatus(); }); }

void MainWindow::onMtgStatus() {
    fromWidgets();
    const QString port = m_set.mtgPort.trimmed().isEmpty() ? QString("10443") : m_set.mtgPort.trimmed();
    const QString script = "systemctl is-active mtg 2>/dev/null || echo inactive\n"
                           "systemctl status mtg --no-pager 2>/dev/null | head -12\n"
                           "ss -tlnp | grep ':" + port + "' || echo 'порт не слушается'\n";
    runSsh(script, "Статус mtg");
}

void MainWindow::onMtgShowQr() {
    fromWidgets();
    const QString host = m_set.host.trimmed();
    const QString port = m_set.mtgPort.trimmed();
    const QString sec = m_set.mtgSecret.trimmed();
    if (host.isEmpty() || port.isEmpty() || sec.isEmpty()) {
        logErr("Нужны хост VDS, порт и секрет");
        return;
    }
    const QString url = tgProxyUrl(host, port, sec);
    const QString tg = QString("tg://proxy?server=%1&port=%2&secret=%3").arg(host, port, sec);
    m_mtgLink->setText(QString("Ссылка (открой на телефоне): <a href=\"%1\">%1</a>").arg(url));
    // QR кодируем схемой tg:// — сканер сразу открывает приложение Telegram.
    const QImage img = makeQrImage(tg, 300);
    if (!img.isNull()) m_mtgQr->setPixmap(QPixmap::fromImage(img));
    logOk("Ссылка и QR готовы (tg://)");
}

void MainWindow::onMtgSaveQr() {
    if (m_mtgQr->pixmap().isNull()) { logErr("Сначала «Показать ссылку и QR»"); return; }
    const QString f = QFileDialog::getSaveFileName(this, "Сохранить QR", QDir::homePath() + "/telegram-proxy.png", "PNG (*.png)");
    if (f.isEmpty()) return;
    if (m_mtgQr->pixmap().toImage().save(f, "PNG")) logOk("QR сохранён: " + f);
    else logErr("не удалось сохранить QR");
}

void MainWindow::refreshMtgStatus() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { m_mtgStatus->setText("Статус: укажи VDS на вкладке «VDS»"); return; }
    showBusy("Обновление статуса mtg…");
    QApplication::processEvents();
    const QString ping = tcpPing(m_set.host.trimmed());
    const QString port = m_set.mtgPort.trimmed().isEmpty() ? QString("10443") : m_set.mtgPort.trimmed();
    const QString script = QString(
        "command -v mtg >/dev/null 2>&1 && echo \"VER=$(mtg --version 2>/dev/null | head -1 | awk '{print $1}')\" || echo VER=NO\n"
        "[ -f /etc/systemd/system/mtg.service ] && echo UNIT=yes || echo UNIT=no\n"
        "echo \"ACTIVE=$(systemctl is-active mtg 2>/dev/null || true)\"\n"
        "ss -tlnp 2>/dev/null | grep -q ':%1 ' && echo PORT=yes || echo PORT=no\n"
        "[ -f /etc/mtg.toml ] && echo TOML=yes || echo TOML=no\n"
        "echo TOML_SECRET=$(grep -E '^[[:space:]]*secret[[:space:]]*=' /etc/mtg.toml 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' \"')\n"
        "echo TOML_BIND=$(grep -E '^[[:space:]]*bind-to[[:space:]]*=' /etc/mtg.toml 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' \"')\n"
    ).arg(port);
    runSsh(script, "Статус mtg", [this, ping](bool ok, const QString& out) {
        hideBusy();
        QString ver = "?", unit = "no", active = "inactive", port = "no", toml = "no", tsec, tbind;
        for (const QString& l : out.split('\n')) {
            const QString t = l.trimmed();
            if (t.startsWith("VER=")) ver = t.mid(4);
            else if (t.startsWith("UNIT=")) unit = t.mid(5);
            else if (t.startsWith("ACTIVE=")) active = t.mid(7);
            else if (t.startsWith("PORT=")) port = t.mid(5);
            else if (t.startsWith("TOML=")) toml = t.mid(5);
            else if (t.startsWith("TOML_SECRET=")) tsec = t.mid(12);
            else if (t.startsWith("TOML_BIND=")) tbind = t.mid(10);
        }
        if (!ok && ver == "?") { m_mtgStatus->setText("Статус: не удалось получить (VDS/SSH?)"); return; }
        const bool installed = (ver != "NO" && ver != "?");
        const bool haveUnit = (unit == "yes");
        const bool isActive = (active == "active");

        // если на VDS есть mtg.toml — подставляем его значения в поля
        if (toml == "yes" && !tsec.trimmed().isEmpty()) {
            m_mtgSecret->setText(tsec.trimmed());
            const int idx = tbind.lastIndexOf(':');
            const int p = (idx >= 0) ? tbind.mid(idx + 1).toInt() : 0;
            if (p > 0) m_mtgPort->setValue(p);
            const QString front = mtgFrontFromSecret(tsec.trimmed());
            if (!front.isEmpty()) m_mtgFront->setCurrentText(front);
            fromWidgets();
        }

        QString s = "<b>mtg:</b> " + (installed ? ver : QString("не установлен"));
        s += " · <b>сервис:</b> " + (haveUnit ? active : QString("нет"));
        s += QString(" · <b>порт:</b> ") + (port == "yes" ? QString("слушается") : QString("не слушается"));
        if (toml == "yes") s += " · <b>конфиг:</b> подставлен с VDS";
        s += QString(" · <b>пинг:</b> ") + (ping.isEmpty() ? QString("n/a") : ping);
        m_mtgStatus->setText(s);

        m_mtgInstallBtn->setText((installed && haveUnit) ? "Обновить mtg и сервис" : "Установить и развернуть");
        m_mtgInstallBtn->setToolTip(installed ? "Обновить mtg и переразвернуть сервис" : "Установить mtg, сгенерировать секрет и развернуть сервис");
        m_mtgGenBtn->setEnabled(installed);
        m_mtgStartBtn->setEnabled(haveUnit && !isActive);
        m_mtgStopBtn->setEnabled(haveUnit && isActive);
        m_mtgRestartBtn->setEnabled(haveUnit);
        m_mtgRemoveBtn->setEnabled(installed || haveUnit);
    });
}

void MainWindow::onMtgRemove() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Не задан хост VDS"); return; }
    if (QMessageBox::question(this, "Удалить mtg",
            "Удалить MTProto-прокси с VDS?\n\n"
            "Будет остановлен и отключён сервис, удалены /etc/mtg.toml и /usr/local/bin/mtg.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    showBusy("Удаление mtg…");
    const QString script = R"BASH(systemctl stop mtg 2>/dev/null || true
systemctl disable mtg 2>/dev/null || true
rm -f /etc/systemd/system/mtg.service /etc/mtg.toml /usr/local/bin/mtg
systemctl daemon-reload
systemctl reset-failed mtg 2>/dev/null || true
echo MTG_REMOVED
)BASH";
    runSsh(script, "Удаление mtg", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("MTG_REMOVED")) logOk("mtg и сервис удалены с VDS");
        else logErr("не удалось удалить mtg");
        refreshMtgStatus();
    });
}

void MainWindow::onMtgOpenTelegram() {
    fromWidgets();
    const QString host = m_set.host.trimmed();
    const QString port = m_set.mtgPort.trimmed();
    const QString sec = m_set.mtgSecret.trimmed();
    if (host.isEmpty() || port.isEmpty() || sec.isEmpty()) { logErr("Нужны хост VDS, порт и секрет"); return; }

    QProcess chk;
    chk.start("xdg-mime", { "query", "default", "x-scheme-handler/tg" });
    if (chk.waitForFinished(3000)) {
        const QString handler = QString::fromLocal8Bit(chk.readAllStandardOutput()).trimmed();
        if (handler.isEmpty())
            log("Telegram Desktop не зарегистрирован как обработчик tg:// — используй ссылку t.me или QR.");
    }

    const QString tg = QString("tg://proxy?server=%1&port=%2&secret=%3").arg(host, port, sec);
    if (QDesktopServices::openUrl(QUrl(tg))) logOk("Открываю Telegram Desktop…");
    else logErr("Не удалось открыть tg:// — Telegram Desktop установлен?");
}
