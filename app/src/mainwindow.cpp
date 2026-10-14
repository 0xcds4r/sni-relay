#include "mainwindow.h"
#include "generator.h"
#include "sshutil.h"
#include "qr.h"
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
#include <QCloseEvent>
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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVector>
#include <QSysInfo>
#include <QStandardPaths>
#include <QThread>
#include <memory>

// ---------- вспомогательные ----------

static QString trim(const QString& s) { return s.trimmed(); }

// Латентность до VDS: TCP-connect до 443 (fallback 80). ICMP часто заблокирован.
static QString monoFamily() {
#ifdef Q_OS_WIN
    return QStringLiteral("Consolas");
#else
    return QStringLiteral("monospace");
#endif
}

static QString tcpPing(const QString& host) {
    if (host.isEmpty()) return QString();
#ifdef Q_OS_WIN
    // Windows: замер TCP-connect через наш curl (ping.exe может отсутствовать).
    QProcess p;
    p.start(sshutil::tool("curl"), { "-s", "-o", "NUL", "-w", "%{time_connect}",
                                     "--connect-timeout", "4", "--insecure", "https://" + host + "/" });
    if (!p.waitForFinished(7000)) { p.kill(); return QString(); }
    bool ok = false;
    const double sec = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed().toDouble(&ok);
    return (ok && sec > 0) ? QString::number(qRound(sec * 1000)) + " ms" : QString();
#else
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
#endif
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
    { QFont f = te->font(); f.setFamily(monoFamily()); te->setFont(f); }
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

void MainWindow::closeEvent(QCloseEvent* e) {
    stopTun();
    QMainWindow::closeEvent(e);
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
#ifdef Q_OS_WIN
    // script — PowerShell. Запускаем через powershell.exe (для прав админа — RunAs).
    const QString tmp = QDir::tempPath() + QString("/srm-elev-%1.ps1").arg(QCoreApplication::applicationPid());
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly)) { logErr("не удалось создать временный скрипт"); if (done) done(false, QString()); return; }
        f.write(script.toUtf8());
        f.close();
    }
    QString prog = "powershell.exe";
    QStringList args = { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", QDir::toNativeSeparators(tmp) };
    runProcess(prog, args, QProcessEnvironment::systemEnvironment(), desc, {}, done, streamLog);
#else
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
#endif
}

// ---------- генерация конфигов ----------

QString MainWindow::generateRelayConf() const { return gen::relayConf(m_set); }
QString MainWindow::remoteDeployScript() const { return gen::deployScript(m_set); }
QString MainWindow::remoteRollbackScript() const { return gen::rollbackScript(m_set); }

QString MainWindow::buildHosts(bool addRelay) const {
    QString current;
    QFile f(sshutil::hostsPath());
    if (f.open(QIODevice::ReadOnly)) current = QString::fromLocal8Bit(f.readAll());
    return gen::hostsFile(m_set, addRelay, current);
}

QString MainWindow::mtgEffPort() const {
    if (m_set.mtgVia443) return QString("443");
    return m_set.mtgPort.trimmed().isEmpty() ? QString("10443") : m_set.mtgPort.trimmed();
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
        m_mtgQr->setObjectName("qr");
        m_mtgQr->setAlignment(Qt::AlignCenter);
        m_mtgQr->setStyleSheet("QLabel#qr { background: #ffffff; border: 1px solid #3a3c48; border-radius: 16px; padding: 16px; }");
        m_mtgQr->setVisible(false);
        c1->addWidget(m_mtgQr, 0, Qt::AlignHCenter);

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
        m_mtgPort->setMinimumHeight(40);
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
        { QFont mono = m_mtgSecret->font(); mono.setFamily(monoFamily()); m_mtgSecret->setFont(mono); }
        auto* secShow = new QPushButton("Показать"); secShow->setCheckable(true);
        auto* secCopy = new QPushButton("Копировать");
        secCopy->setToolTip("Скопировать секрет в буфер обмена");
        secLay->addWidget(m_mtgSecret); secLay->addWidget(secShow); secLay->addWidget(secCopy);

        m_mtgVia443 = new QCheckBox("Слушать на 443 через релей (рекомендуется)");
        m_mtgVia443->setToolTip("mtg слушает на loopback-порту, а релей отдаёт его по SNI фронт-домена на 443. "
                                "Клиент подключается к 443 — обходит блокировку нестандартных портов.");
        form->addRow(QString(), m_mtgVia443);

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
                m_mtgQr->setVisible(true);
                qr->setText("Скрыть QR");
            } else {
                m_mtgQr->setVisible(false);
                qr->setText("Показать QR");
            }
        });
        connect(addtg, &QPushButton::clicked, this, &MainWindow::onMtgOpenTelegram);
        connect(save, &QPushButton::clicked, this, &MainWindow::onMtgSaveQr);
        connect(copylink, &QPushButton::clicked, this, [this] {
            fromWidgets();
            if (m_set.host.trimmed().isEmpty() || m_set.mtgSecret.trimmed().isEmpty()) {
                logErr("нужны хост VDS и секрет"); return;
            }
            QGuiApplication::clipboard()->setText(tgProxyUrl(m_set.host.trimmed(), mtgEffPort(), m_set.mtgSecret.trimmed()));
            logOk("Ссылка скопирована");
        });

        scroll->setWidget(w);
        tabs->addTab(scroll, "MTProto");

        // автообновление статуса при переходе на вкладку MTProto
        connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int) {
            if (tabs->tabText(tabs->currentIndex()) == "MTProto") refreshMtgStatus();
        });
    }

    // ===== Reality (VLESS + Xray) =====
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
        m_realStatus = new QLabel("Нажми «Обновить статус»");
        m_realStatus->setObjectName("mtgStatus");
        m_realStatus->setTextFormat(Qt::RichText);
        m_realStatus->setWordWrap(true);
        c1->addWidget(m_realStatus);

        auto* b3 = new QWidget; auto* l3 = new QHBoxLayout(b3); l3->setContentsMargins(0, 0, 0, 0);
        auto* rqr = new QPushButton("Показать QR");
        rqr->setCheckable(true);
        auto* rcopylink = new QPushButton("Копировать vless://");
        rcopylink->setToolTip("Скопировать ссылку vless:// для клиента (v2rayNG/Nekoray/sing-box)");
        auto* rsave = new QPushButton("Сохранить QR…");
        l3->addWidget(rqr); l3->addWidget(rcopylink); l3->addWidget(rsave); l3->addStretch();
        c1->addWidget(b3);

        m_realQr = new QLabel;
        m_realQr->setObjectName("qr");
        m_realQr->setAlignment(Qt::AlignCenter);
        m_realQr->setStyleSheet("QLabel#qr { background: #ffffff; border: 1px solid #3a3c48; border-radius: 16px; padding: 16px; }");
        m_realQr->setVisible(false);
        c1->addWidget(m_realQr, 0, Qt::AlignHCenter);

        // --- Карточка: параметры ---
        auto* c2 = addCard("ПАРАМЕТРЫ");
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(10);

        m_realPort = new QSpinBox;
        m_realPort->setRange(1, 65535);
        m_realPort->setValue(2053);
        m_realPort->setFixedWidth(120);
        m_realPort->setMinimumHeight(40);
        m_realPort->setToolTip("Публичный порт Reality. Клиент идёт на VDS:порт, SNI = маскировочный домен.");

        m_realDest = new QComboBox;
        m_realDest->setEditable(true);
        m_realDest->addItems({ "dl.google.com", "www.microsoft.com", "www.samsung.com",
                               "www.lovelive-anime.jp", "www.apple.com", "www.cloudflare.com" });
        m_realDest->setToolTip("Чужой домен, под чей TLS маскируется Reality (dest/serverName). "
                               "Должен быть доступен с VDS и не заблокирован у тебя.");

        m_realFlow = new QComboBox;
        m_realFlow->addItems({ "xtls-rprx-vision", "none" });
        m_realFlow->setToolTip("Flow для VLESS. Для Reality обычно xtls-rprx-vision.");

        m_realFp = new QComboBox;
        m_realFp->addItems({ "chrome", "firefox", "safari", "ios", "android", "edge", "random", "randomized" });
        m_realFp->setToolTip("Отпечаток TLS клиента (uTLS). chrome — обычно лучший выбор.");

        auto* uuidRow = new QWidget; auto* uuidLay = new QHBoxLayout(uuidRow); uuidLay->setContentsMargins(0, 0, 0, 0);
        m_realUuid = new QLineEdit; m_realUuid->setPlaceholderText("сгенерировать…");
        { QFont mono = m_realUuid->font(); mono.setFamily(monoFamily()); m_realUuid->setFont(mono); }
        auto* uuidCopy = new QPushButton("Копировать");
        uuidLay->addWidget(m_realUuid); uuidLay->addWidget(uuidCopy);

        auto* pubRow = new QWidget; auto* pubLay = new QHBoxLayout(pubRow); pubLay->setContentsMargins(0, 0, 0, 0);
        m_realPub = new QLineEdit; m_realPub->setPlaceholderText("pbk (публичный ключ)");
        { QFont mono = m_realPub->font(); mono.setFamily(monoFamily()); m_realPub->setFont(mono); }
        auto* pubCopy = new QPushButton("Копировать");
        pubLay->addWidget(m_realPub); pubLay->addWidget(pubCopy);

        auto* privRow = new QWidget; auto* privLay = new QHBoxLayout(privRow); privLay->setContentsMargins(0, 0, 0, 0);
        m_realPriv = new QLineEdit; m_realPriv->setPlaceholderText("privateKey (сервер)");
        m_realPriv->setEchoMode(QLineEdit::Password);
        { QFont mono = m_realPriv->font(); mono.setFamily(monoFamily()); m_realPriv->setFont(mono); }
        auto* privShow = new QPushButton("Показать"); privShow->setCheckable(true);
        privLay->addWidget(m_realPriv); privLay->addWidget(privShow);

        m_realSid = new QLineEdit; m_realSid->setPlaceholderText("shortId (hex)");
        { QFont mono = m_realSid->font(); mono.setFamily(monoFamily()); m_realSid->setFont(mono); }

        m_realTransport = new QComboBox;
        m_realTransport->addItem("Reality (TCP)", "reality");
        m_realTransport->addItem("gRPC + TLS (Cloudflare)", "grpc");
        m_realTransport->setToolTip("Reality — свой TLS-маскировочный домен, без Cloudflare.\n"
                                    "gRPC + TLS — за Cloudflare (домен проксируется, gRPC включён, SSL Flexible).");

        m_realDomain = new QLineEdit; m_realDomain->setPlaceholderText("домен за Cloudflare (для gRPC)");
        m_realDomain->setToolTip("Домен, проксируемый через Cloudflare (оранжевое облако). Клиент идёт на него:443.");
        m_realService = new QLineEdit; m_realService->setPlaceholderText("serviceName (по умолчанию grpc)");
        m_realGrpcPort = new QLineEdit; m_realGrpcPort->setFixedWidth(120);
        m_realGrpcPort->setPlaceholderText("2083");
        m_realGrpcPort->setToolTip("Порт origin для gRPC (Cloudflare-совместимый: 443/2053/2083/2087/2096/8443).");

        form->addRow("Транспорт", m_realTransport);
        form->addRow("Домен (Cloudflare)", m_realDomain);
        form->addRow("serviceName", m_realService);
        form->addRow("Порт origin (gRPC)", m_realGrpcPort);
        form->addRow("Порт", m_realPort);
        form->addRow("Маскировочный домен", m_realDest);
        form->addRow("Flow", m_realFlow);
        form->addRow("Fingerprint", m_realFp);
        form->addRow("UUID", uuidRow);
        form->addRow("Public key", pubRow);
        form->addRow("Private key", privRow);
        form->addRow("Short id", m_realSid);
        c2->addLayout(form);

        auto* cfHint = new QLabel(
            "gRPC+TLS за Cloudflare: A-запись домена → IP VDS, Proxy ON (оранжевое облако), "
            "SSL/TLS = Flexible, Network → gRPC = ON. Клиент подключается к домену на 443; "
            "origin слушает порт выше. Для Reality поля «домен/serviceName/порт gRPC» не нужны.");
        cfHint->setObjectName("hint"); cfHint->setWordWrap(true);
        c2->addWidget(cfHint);

        connect(privShow, &QPushButton::toggled, this, [this, privShow](bool on) {
            m_realPriv->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
            privShow->setText(on ? "Скрыть" : "Показать");
        });
        connect(uuidCopy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(m_realUuid->text()); logOk("UUID скопирован"); });
        connect(pubCopy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(m_realPub->text()); logOk("Public key скопирован"); });

        // --- Карточка: управление сервисом ---
        auto* c3 = addCard("УПРАВЛЕНИЕ СЕРВИСОМ");
        auto* b1 = new QWidget; auto* l1 = new QHBoxLayout(b1); l1->setContentsMargins(0, 0, 0, 0);
        m_realInstallBtn = new QPushButton("Установить Xray и развернуть");
        m_realInstallBtn->setObjectName("primary");
        m_realInstallBtn->setToolTip("Установить/обновить Xray-core, сгенерировать ключи и развернуть Reality-сервис");
        m_realGenBtn = new QPushButton("Сгенерировать ключи");
        m_realGenBtn->setToolTip("Сгенерировать UUID, x25519-ключи и shortId (нужен установленный Xray)");
        l1->addWidget(m_realInstallBtn); l1->addWidget(m_realGenBtn); l1->addStretch();
        c3->addWidget(b1);

        auto* b2 = new QWidget; auto* l2 = new QHBoxLayout(b2); l2->setContentsMargins(0, 0, 0, 0);
        m_realStartBtn = new QPushButton("Старт");
        m_realStopBtn = new QPushButton("Стоп");
        m_realRestartBtn = new QPushButton("Рестарт");
        m_realStatusBtn = new QPushButton("Обновить статус");
        m_realRemoveBtn = new QPushButton("Удалить Xray и сервис");
        m_realRemoveBtn->setObjectName("danger");
        m_realRemoveBtn->setToolTip("Остановить и удалить Xray с VDS: сервис, конфиг, sidecar и бинарь");
        l2->addWidget(m_realStartBtn); l2->addWidget(m_realStopBtn);
        l2->addWidget(m_realRestartBtn); l2->addWidget(m_realStatusBtn);
        l2->addWidget(m_realRemoveBtn); l2->addStretch();
        c3->addWidget(b2);

        // --- Карточка: клиент (подключение) ---
        auto* c4 = addCard("КЛИЕНТ — ПОДКЛЮЧЕНИЕ");
        m_realClientStatus = new QLabel("Отключено");
        m_realClientStatus->setObjectName("mtgStatus");
        m_realClientStatus->setTextFormat(Qt::RichText);
        m_realClientStatus->setWordWrap(true);
        c4->addWidget(m_realClientStatus);

        auto* cb = new QWidget; auto* cbl = new QHBoxLayout(cb); cbl->setContentsMargins(0, 0, 0, 0);
        m_realConnectBtn = new QPushButton("Подключиться");
        m_realConnectBtn->setObjectName("primary");
        m_realConnectBtn->setToolTip("Запустить локальный Xray-клиент и включить системный прокси");
        m_realDisconnectBtn = new QPushButton("Отключиться");
        m_realDisconnectBtn->setEnabled(false);
        auto* ipBtn = new QPushButton("Проверить IP");
        ipBtn->setToolTip("Показать внешний IP через прокси (должен быть IP VDS)");
        cbl->addWidget(m_realConnectBtn); cbl->addWidget(m_realDisconnectBtn); cbl->addWidget(ipBtn); cbl->addStretch();
        c4->addWidget(cb);

        auto* chint = new QLabel(
            "«Подключиться» поднимает системный TUN-туннель через sing-box — весь трафик идёт через VDS. "
            "Потребуется пароль sudo (спросит один раз). «Отключиться» вернёт маршруты как было.");
        chint->setObjectName("hint"); chint->setWordWrap(true);
        c4->addWidget(chint);

        connect(m_realConnectBtn, &QPushButton::clicked, this, &MainWindow::onRealityConnect);
        connect(m_realDisconnectBtn, &QPushButton::clicked, this, &MainWindow::onRealityDisconnect);
        connect(ipBtn, &QPushButton::clicked, this, &MainWindow::onRealityCheckIp);

        auto* hint = new QLabel(
            "VLESS + Reality (Xray): обходит блокировку по SNI. Клиент идёт на VDS:порт с SNI "
            "маскировочного домена — провайдер видит обычный TLS к разрешённому сайту, а цель едет внутри шифра. "
            "Работает для X/Instagram/Facebook и др. (в отличие от SNI-релея). "
            "Клиент — v2rayNG/Nekoray/sing-box: скопируй ссылку vless:// или отсканируй QR.");
        hint->setObjectName("hint");
        hint->setWordWrap(true);
        lay->addWidget(hint);
        lay->addStretch();

        connect(m_realInstallBtn, &QPushButton::clicked, this, &MainWindow::onRealityInstallDeploy);
        connect(m_realGenBtn, &QPushButton::clicked, this, &MainWindow::onRealityGenKeys);
        connect(m_realStartBtn, &QPushButton::clicked, this, &MainWindow::onRealityStart);
        connect(m_realStopBtn, &QPushButton::clicked, this, &MainWindow::onRealityStop);
        connect(m_realRestartBtn, &QPushButton::clicked, this, &MainWindow::onRealityRestart);
        connect(m_realStatusBtn, &QPushButton::clicked, this, &MainWindow::refreshRealityStatus);
        connect(m_realRemoveBtn, &QPushButton::clicked, this, &MainWindow::onRealityRemove);
        connect(rqr, &QPushButton::toggled, this, [this, rqr](bool on) {
            if (on) { onRealityShowQr(); m_realQr->setVisible(true); rqr->setText("Скрыть QR"); }
            else { m_realQr->setVisible(false); rqr->setText("Показать QR"); }
        });
        connect(rsave, &QPushButton::clicked, this, &MainWindow::onRealitySaveQr);
        connect(rcopylink, &QPushButton::clicked, this, &MainWindow::onRealityCopyLink);

        scroll->setWidget(w);
        tabs->addTab(scroll, "Reality");

        connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int) {
            if (tabs->tabText(tabs->currentIndex()) == "Reality") refreshRealityStatus();
        });
    }

    // ===== Интеграции =====
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
        auto hint = [](const QString& s) {
            auto* l = new QLabel(s); l->setObjectName("hint"); l->setWordWrap(true); return l;
        };
        auto statusBox = [](const QString& s) {
            auto* l = new QLabel(s); l->setObjectName("statusBox"); l->setWordWrap(true); l->setTextFormat(Qt::RichText); return l;
        };

        // Браузер
        auto* c1 = addCard("БРАУЗЕР (CHROME / CHROMIUM)");
        c1->addWidget(hint("Если браузер резолвит мимо /etc/hosts (встроенный DNS AsyncDns) — добавь флаг "
                           "и перезапусти браузер. Также выключи Secure DNS (DoH)."));
        m_intBrowserStatus = statusBox("Статус: не проверялся");
        c1->addWidget(m_intBrowserStatus);
        auto* b1 = new QWidget; auto* l1 = new QHBoxLayout(b1); l1->setContentsMargins(0, 0, 0, 0);
        auto* brCheck = new QPushButton("Проверить");
        auto* brFix = new QPushButton("Отключить AsyncDns"); brFix->setObjectName("primary");
        brFix->setToolTip("Добавить --disable-features=AsyncDns в конфиг-флаги Chrome/Chromium");
        l1->addWidget(brCheck); l1->addWidget(brFix); l1->addStretch();
        c1->addWidget(b1);

        // Zapret
        auto* c2 = addCard("ZAPRET");
        c2->addWidget(hint("Домены с SNI-фильтром нужно держать и в zapret-списках (десинк прячет SNI). "
                           "Ниже — пересечение доменов релея с list-general.txt / list-general-user.txt."));
        m_intZapretStatus = statusBox("Статус: не проверялся");
        c2->addWidget(m_intZapretStatus);
        auto* zBtn = new QPushButton("Проверить пересечения с zapret");
        { auto* bw = new QWidget; auto* bl = new QHBoxLayout(bw); bl->setContentsMargins(0,0,0,0); bl->addWidget(zBtn); bl->addStretch(); c2->addWidget(bw); }

        // Порты
        auto* c3 = addCard("ПОРТЫ");
        c3->addWidget(hint("Проверь занятые порты на VDS и подбери свободный для mtg."));
        m_intPortsStatus = statusBox("Статус: не проверялся");
        c3->addWidget(m_intPortsStatus);
        auto* b3 = new QWidget; auto* l3 = new QHBoxLayout(b3); l3->setContentsMargins(0, 0, 0, 0);
        auto* pShow = new QPushButton("Занятые порты на VDS");
        auto* pFree = new QPushButton("Свободный порт для mtg");
        l3->addWidget(pShow); l3->addWidget(pFree); l3->addStretch();
        c3->addWidget(b3);

        lay->addStretch();
        scroll->setWidget(w);
        tabs->addTab(scroll, "Интеграции");

        connect(brCheck, &QPushButton::clicked, this, &MainWindow::checkBrowser);
        connect(brFix, &QPushButton::clicked, this, &MainWindow::fixBrowserAsyncDns);
        connect(zBtn, &QPushButton::clicked, this, &MainWindow::checkZapretOverlap);
        connect(pShow, &QPushButton::clicked, this, &MainWindow::showVdsPorts);
        connect(pFree, &QPushButton::clicked, this, &MainWindow::pickFreeMtgPort);
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

            "<p><b>MTProto: статус «недоступен» / бесконечное «подключение».</b><br>"
            "Обычно оператор режет нестандартный порт (например 10443). Включи на вкладке MTProto "
            "<b>«Слушать на 443 через релей»</b>, нажми «Установить и развернуть» и добавь прокси заново "
            "с портом <b>443</b> (ссылка и QR обновятся сами). "
            "Если висит после верной ссылки — проверь, что на телефоне включена авто-дата/время "
            "(рассинхрон часов ломает FakeTLS).</p>"

            "<p><b>443 занят моим сайтом — не сломается?</b><br>"
            "Нет. При «Слушать на 443 через релей» mtg слушает loopback-порт, а релей отдаёт его на 443 "
            "по SNI фронт-домена (обычно <code>www.google.com</code>). Твой сайт по своему SNI "
            "(<code>hugedev.ru</code>/default) не затрагивается. Проверяется через <code>nginx -t</code> с "
            "автооткатом.</p>"

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

            "<p><b>X / Instagram / Facebook не открываются (SNI-релей не помогает).</b><br>"
            "Провайдер режет соединение <b>по SNI</b> ещё до твоего VDS — прозрачный SNI-релей тут бессилен. "
            "Открой вкладку <b>Reality</b> → «Установить Xray и развернуть» и добавь полученную ссылку "
            "<code>vless://</code> в клиент (v2rayNG / Nekoray / sing-box). Reality маскируется под "
            "разрешённый домен, а настоящий адрес прячет внутри шифра.</p>"

            "<p><b>Reality: порт занят / не подключается.</b><br>"
            "Reality слушает свой публичный порт (по умолчанию 2053), 443 остаётся за SNI-релеем. "
            "Если 2053 занят — укажи другой свободный порт. Маскировочный домен должен быть доступен "
            "с VDS и не заблокирован у тебя (хорошие варианты: <code>dl.google.com</code>, "
            "<code>www.microsoft.com</code>).</p>"

            "<p><b>Desktop: «Подключиться» просит пароль sudo.</b><br>"
            "Системный TUN-туннель создаёт сетевой интерфейс и меняет маршруты — для этого нужны права root. "
            "Пароль вводится один раз и хранится только в памяти сессии. Клиент (<b>sing-box</b>) скачивается "
            "автоматически при первом подключении. «Отключиться» (или закрытие окна) останавливает туннель "
            "и возвращает маршруты.</p>"

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
    m_mtgVia443->setChecked(m_set.mtgVia443);
    { const int p = m_set.realityPort.toInt(); m_realPort->setValue(p > 0 ? p : 2053); }
    m_realDest->setCurrentText(m_set.realityDest);
    m_realFlow->setCurrentText(m_set.realityFlow);
    m_realFp->setCurrentText(m_set.realityFingerprint);
    m_realUuid->setText(m_set.realityUuid);
    m_realPub->setText(m_set.realityPublicKey);
    m_realPriv->setText(m_set.realityPrivateKey);
    m_realSid->setText(m_set.realityShortId);
    m_realTransport->setCurrentIndex(m_set.realityTransport.trimmed() == "grpc" ? 1 : 0);
    m_realDomain->setText(m_set.realityDomain);
    m_realService->setText(m_set.realityServiceName);
    m_realGrpcPort->setText(m_set.realityGrpcPort);
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
    m_set.mtgVia443 = m_mtgVia443->isChecked();
    m_set.realityPort = QString::number(m_realPort->value());
    m_set.realityDest = m_realDest->currentText().trimmed();
    m_set.realityFlow = m_realFlow->currentText().trimmed();
    m_set.realityFingerprint = m_realFp->currentText().trimmed();
    m_set.realityUuid = trim(m_realUuid->text());
    m_set.realityPublicKey = trim(m_realPub->text());
    m_set.realityPrivateKey = trim(m_realPriv->text());
    m_set.realityShortId = trim(m_realSid->text());
    m_set.realityTransport = m_realTransport->currentData().toString();
    m_set.realityDomain = trim(m_realDomain->text());
    m_set.realityServiceName = trim(m_realService->text());
    m_set.realityGrpcPort = trim(m_realGrpcPort->text());
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

bool MainWindow::writeHostsDirect(const QString& content) {
    const QString h = sshutil::hostsPath();
    if (!QFileInfo::exists(h)) return false;
    QFile::copy(h, h + ".bak-" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss"));
    QFile f(h);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data = content.toUtf8();
    const bool ok = (f.write(data) == data.size());
    f.close();
    return ok;
}

void MainWindow::onApplyHosts() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Укажи хост VDS"); return; }
    const QString content = buildHosts(true);
#ifdef Q_OS_WIN
    if (writeHostsDirect(content)) { logOk("Домены прописаны в hosts: " + sshutil::hostsPath()); return; }
    QString script = "$h = '" + sshutil::hostsPath() + "'\n";
    script += "Copy-Item -Force $h \"$h.bak-$(Get-Date -Format yyyyMMddHHmmss)\"\n";
    script += "Set-Content -Path $h -Encoding ASCII -Value @'\n" + content + "'@\n";
    script += "& ipconfig /flushdns | Out-Null\nWrite-Output HOSTS_OK\n";
#else
    const QString delim = "SRMEOF_HOSTS_9f3a";
    QString script = "#!/bin/bash\nset -e\n"
                     "cp -a /etc/hosts /etc/hosts.bak-$(date +%F-%H%M%S)\n"
                     "cat > /etc/hosts <<'" + delim + "'\n" + content + delim + "\n"
                     "resolvectl flush-caches 2>/dev/null || true\n"
                     "echo HOSTS_OK\n";
#endif
    showBusy("Обновление hosts…");
    runElevated(script, "Прописать домены в hosts", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("HOSTS_OK")) logOk("Домены прописаны в hosts");
        else logErr("Не удалось обновить hosts");
    });
}

void MainWindow::onRemoveHosts() {
    fromWidgets();
    const QString content = buildHosts(false);
#ifdef Q_OS_WIN
    if (writeHostsDirect(content)) { logOk("Домены убраны из hosts: " + sshutil::hostsPath()); return; }
    QString script = "$h = '" + sshutil::hostsPath() + "'\n";
    script += "Copy-Item -Force $h \"$h.bak-$(Get-Date -Format yyyyMMddHHmmss)\"\n";
    script += "Set-Content -Path $h -Encoding ASCII -Value @'\n" + content + "'@\n";
    script += "& ipconfig /flushdns | Out-Null\nWrite-Output HOSTS_OK\n";
#else
    const QString delim = "SRMEOF_HOSTS_9f3a";
    QString script = "#!/bin/bash\nset -e\n"
                     "cp -a /etc/hosts /etc/hosts.bak-$(date +%F-%H%M%S)\n"
                     "cat > /etc/hosts <<'" + delim + "'\n" + content + delim + "\n"
                     "resolvectl flush-caches 2>/dev/null || true\n"
                     "echo HOSTS_OK\n";
#endif
    showBusy("Обновление hosts…");
    runElevated(script, "Убрать домены из hosts", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("HOSTS_OK")) logOk("Домены убраны из hosts");
        else logErr("Не удалось обновить hosts");
    });
}

void MainWindow::onShowHostsEntries() {
    fromWidgets();
    QSet<QString> want;
    for (const auto& d : m_set.domains) { QString t = d.trimmed(); if (!t.isEmpty()) want.insert(t); }
    const QString hp = sshutil::hostsPath();
    QFile f(hp);
    if (!f.open(QIODevice::ReadOnly)) { logErr("не читается " + hp); return; }
    QStringList found;
    for (const QString& line : QString::fromLocal8Bit(f.readAll()).split('\n')) {
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 1; i < parts.size(); ++i)
            if (want.contains(parts[i])) { found << line; break; }
    }
    const QString body = found.isEmpty() ? QString("(нет)\n") : found.join('\n') + "\n";
    showTextDialog("Записи hosts",
                   "--- записи hosts для доменов релея ---\n" + body +
                   "------------------------------------------\nфайл: " + hp);
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
    runProcess(sshutil::tool("curl"), args, QProcessEnvironment::systemEnvironment(), "проверка " + d, {},
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
    const bool via = m_set.mtgVia443;
    const QString localPort = m_set.mtgLocalPort.trimmed().isEmpty() ? QString("9999") : m_set.mtgLocalPort.trimmed();
    const QString bindPort = via ? localPort : port;
    const QString ip = m_set.host.trimmed();
    const bool ip4 = QRegularExpression("^[0-9.]+$").match(ip).hasMatch();

    QString toml;
    toml += "secret = \"" + m_set.mtgSecret.trimmed() + "\"\n";
    toml += "bind-to = \"" + QString(via ? "127.0.0.1:" : "0.0.0.0:") + bindPort + "\"\n";
    toml += "concurrency = 8192\n";
    toml += "prefer-ip = \"prefer-ipv4\"\n";
    if (ip4) toml += "public-ipv4 = \"" + ip + "\"\n";
    toml += "tolerate-time-skewness = \"30s\"\n";
    if (via) toml += "proxy-protocol-listener = true\n";

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
    script += "ss -tlnp | grep ':" + bindPort + "' || true\n";
    script += "echo MTG_DEPLOYED\n";
    showBusy("Развёртывание сервиса…");
    runSsh(script, "Развёртывание mtg на VDS", [this](bool ok, const QString& out) {
        hideBusy();
        const bool okd = ok && out.contains("MTG_DEPLOYED");
        if (okd) logOk("MTProto-прокси развёрнут"); else logErr("Развёртывание mtg не удалось");
        if (okd && m_set.mtgVia443) {
            showBusy("Развёртывание релея (маршрут mtg)…");
            runSsh(remoteDeployScript(), "Развёртывание релея", [this](bool, const QString&) { hideBusy(); refreshMtgStatus(); });
        } else {
            refreshMtgStatus();
        }
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
    const QString port = mtgEffPort();
    const QString sec = m_set.mtgSecret.trimmed();
    if (host.isEmpty() || port.isEmpty() || sec.isEmpty()) {
        logErr("Нужны хост VDS, порт и секрет");
        return;
    }
    const QString tg = QString("tg://proxy?server=%1&port=%2&secret=%3").arg(host, port, sec);
    // QR кодируем схемой tg:// — сканер сразу открывает приложение Telegram.
    const QImage img = makeQrImage(tg, 300);
    if (!img.isNull()) m_mtgQr->setPixmap(QPixmap::fromImage(img));
    logOk("Ссылка и QR готовы (tg://)");
}

void MainWindow::onMtgSaveQr() {
    if (m_mtgQr->pixmap().isNull()) { logErr("Сначала «Показать QR»"); return; }
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
    const QString bindPort = m_set.mtgVia443
        ? (m_set.mtgLocalPort.trimmed().isEmpty() ? QString("9999") : m_set.mtgLocalPort.trimmed())
        : (m_set.mtgPort.trimmed().isEmpty() ? QString("10443") : m_set.mtgPort.trimmed());
    const QString script = QString(
        "command -v mtg >/dev/null 2>&1 && echo \"VER=$(mtg --version 2>/dev/null | head -1 | awk '{print $1}')\" || echo VER=NO\n"
        "[ -f /etc/systemd/system/mtg.service ] && echo UNIT=yes || echo UNIT=no\n"
        "echo \"ACTIVE=$(systemctl is-active mtg 2>/dev/null || true)\"\n"
        "ss -tlnp 2>/dev/null | grep -q ':%1 ' && echo PORT=yes || echo PORT=no\n"
        "[ -f /etc/mtg.toml ] && echo TOML=yes || echo TOML=no\n"
        "echo TOML_SECRET=$(grep -E '^[[:space:]]*secret[[:space:]]*=' /etc/mtg.toml 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' \"')\n"
        "echo TOML_BIND=$(grep -E '^[[:space:]]*bind-to[[:space:]]*=' /etc/mtg.toml 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' \"')\n"
    ).arg(bindPort);
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
            if (tbind.startsWith("127.0.0.1:")) {
                m_mtgVia443->setChecked(true);
                if (p > 0) m_set.mtgLocalPort = QString::number(p);
            } else {
                m_mtgVia443->setChecked(false);
                if (p > 0) m_mtgPort->setValue(p);
            }
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
    const QString port = mtgEffPort();
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

// ---------- Reality (VLESS + Xray) ----------

void MainWindow::onRealityInstallDeploy() {
    fromWidgets();
    showBusy("Установка Xray на VDS…");
    runSsh(gen::realityInstall(), "Установка Xray на VDS", [this](bool ok, const QString& out) {
        if (!ok || !out.contains("XRAY_INSTALLED")) {
            hideBusy(); logErr("установка Xray не удалась"); refreshRealityStatus(); return;
        }
        onRealityGenKeys();
    });
}

void MainWindow::onRealityGenKeys() {
    fromWidgets();
    showBusy("Генерация ключей Reality…");
    runSsh(gen::realityGenKeys(), "Генерация ключей Reality", [this](bool ok, const QString& out) {
        if (!ok || !out.contains("REALITY_KEYS_OK")) {
            hideBusy(); logErr("не удалось сгенерировать ключи (Xray установлен?)"); refreshRealityStatus(); return;
        }
        QString uuid, priv, pub, sid;
        for (const QString& l : out.split('\n')) {
            const QString t = l.trimmed();
            if (t.startsWith("UUID=")) uuid = t.mid(5);
            else if (t.startsWith("PRIV=")) priv = t.mid(5);
            else if (t.startsWith("PUB=")) pub = t.mid(4);
            else if (t.startsWith("SHORTID=")) sid = t.mid(8);
        }
        if (!uuid.isEmpty()) m_realUuid->setText(uuid);
        if (!priv.isEmpty()) m_realPriv->setText(priv);
        if (!pub.isEmpty()) m_realPub->setText(pub);
        if (!sid.isEmpty()) m_realSid->setText(sid);
        fromWidgets();
        logOk("Ключи Reality сгенерированы");
        onRealityDeploy();
    });
}

void MainWindow::onRealityDeploy() {
    fromWidgets();
    if (m_set.realityUuid.trimmed().isEmpty() || m_set.realityPrivateKey.trimmed().isEmpty() ||
        m_set.realityShortId.trimmed().isEmpty()) {
        hideBusy(); logErr("Нужны UUID, private key и short id — сначала «Сгенерировать ключи»");
        refreshRealityStatus(); return;
    }
    showBusy("Развёртывание Reality…");
    runSsh(gen::realityDeployScript(m_set), "Развёртывание Reality", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("REALITY_DEPLOYED")) logOk("VLESS+Reality развёрнут");
        else logErr("развёртывание Reality не удалось");
        refreshRealityStatus();
    });
}

void MainWindow::onRealityStart()   { fromWidgets(); showBusy("Старт Xray…"); runSsh("systemctl start xray && systemctl is-active xray", "Старт Xray", [this](bool, const QString&) { hideBusy(); refreshRealityStatus(); }); }
void MainWindow::onRealityStop()    { fromWidgets(); showBusy("Стоп Xray…"); runSsh("systemctl stop xray || true", "Стоп Xray", [this](bool, const QString&) { hideBusy(); refreshRealityStatus(); }); }
void MainWindow::onRealityRestart() { fromWidgets(); showBusy("Рестарт Xray…"); runSsh("systemctl restart xray && systemctl is-active xray", "Рестарт Xray", [this](bool, const QString&) { hideBusy(); refreshRealityStatus(); }); }

void MainWindow::onRealityStatus() {
    fromWidgets();
    runSsh(gen::realityStatus(m_set), "Статус Reality");
}

void MainWindow::onRealityShowQr() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty() || m_set.realityUuid.trimmed().isEmpty() ||
        m_set.realityPublicKey.trimmed().isEmpty()) {
        logErr("Нужны хост VDS, UUID и public key");
        return;
    }
    const QString link = gen::realityClientLink(m_set);
    const QImage img = makeQrImage(link, 300);
    if (!img.isNull()) m_realQr->setPixmap(QPixmap::fromImage(img));
    logOk("Ссылка и QR готовы (vless://)");
}

void MainWindow::onRealitySaveQr() {
    if (m_realQr->pixmap().isNull()) { logErr("Сначала «Показать QR»"); return; }
    const QString f = QFileDialog::getSaveFileName(this, "Сохранить QR", QDir::homePath() + "/reality-vless.png", "PNG (*.png)");
    if (f.isEmpty()) return;
    if (m_realQr->pixmap().toImage().save(f, "PNG")) logOk("QR сохранён: " + f);
    else logErr("не удалось сохранить QR");
}

void MainWindow::onRealityCopyLink() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty() || m_set.realityUuid.trimmed().isEmpty()) {
        logErr("нужны хост VDS и UUID"); return;
    }
    QGuiApplication::clipboard()->setText(gen::realityClientLink(m_set));
    logOk("Ссылка vless:// скопирована");
}

void MainWindow::onRealityRemove() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Не задан хост VDS"); return; }
    if (QMessageBox::question(this, "Удалить Reality",
            "Удалить Xray и Reality-сервис с VDS?\n\n"
            "Будет остановлен и отключён сервис, удалены конфиг, sidecar-файл и бинарь xray.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    showBusy("Удаление Xray…");
    runSsh(gen::realityRemove(), "Удаление Xray", [this](bool ok, const QString& out) {
        hideBusy();
        if (ok && out.contains("REALITY_REMOVED")) logOk("Xray и Reality удалены с VDS");
        else logErr("не удалось удалить Xray");
        refreshRealityStatus();
    });
}

void MainWindow::refreshRealityStatus() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { m_realStatus->setText("Статус: укажи VDS на вкладке «VDS»"); return; }
    showBusy("Обновление статуса Reality…");
    QApplication::processEvents();
    const QString ping = tcpPing(m_set.host.trimmed());
    runSsh(gen::realityStatus(m_set), "Статус Reality", [this, ping](bool ok, const QString& out) {
        hideBusy();
        QString ver = "?", unit = "no", active = "inactive", port = "no", cfg = "no", side = "no", b64;
        for (const QString& l : out.split('\n')) {
            const QString t = l.trimmed();
            if (t.startsWith("VER=")) ver = t.mid(4);
            else if (t.startsWith("UNIT=")) unit = t.mid(5);
            else if (t.startsWith("ACTIVE=")) active = t.mid(7);
            else if (t.startsWith("PORT=")) port = t.mid(5);
            else if (t.startsWith("CFG=")) cfg = t.mid(4);
            else if (t.startsWith("SIDE=")) side = t.mid(5);
            else if (t.startsWith("SIDE_B64=")) b64 = t.mid(9);
        }
        if (!ok && ver == "?") { m_realStatus->setText("Статус: не удалось получить (VDS/SSH?)"); return; }
        const bool installed = (ver != "NO" && ver != "?");
        const bool haveUnit = (unit == "yes");
        const bool isActive = (active == "active");

        // импорт параметров из sidecar на VDS
        if (side == "yes" && !b64.trimmed().isEmpty()) {
            const QByteArray j = QByteArray::fromBase64(b64.trimmed().toLatin1());
            const QJsonObject o = QJsonDocument::fromJson(j).object();
            if (!o.isEmpty()) {
                const bool grpc = (o.value("transport").toString() == "grpc");
                if (o.contains("transport")) m_realTransport->setCurrentIndex(grpc ? 1 : 0);
                if (o.contains("domain")) m_realDomain->setText(o.value("domain").toString());
                if (o.contains("serviceName")) m_realService->setText(o.value("serviceName").toString());
                if (o.contains("grpcPort")) m_realGrpcPort->setText(o.value("grpcPort").toString());
                if (o.contains("port")) {
                    const QString ps = o.value("port").toString();
                    if (grpc) { if (!ps.isEmpty()) m_realGrpcPort->setText(ps); }
                    else { const int p = ps.toInt(); if (p > 0) m_realPort->setValue(p); }
                }
                if (o.contains("dest")) m_realDest->setCurrentText(o.value("dest").toString());
                if (o.contains("flow")) m_realFlow->setCurrentText(o.value("flow").toString());
                if (o.contains("fingerprint")) m_realFp->setCurrentText(o.value("fingerprint").toString());
                if (o.contains("uuid")) m_realUuid->setText(o.value("uuid").toString());
                if (o.contains("publicKey")) m_realPub->setText(o.value("publicKey").toString());
                if (o.contains("privateKey")) m_realPriv->setText(o.value("privateKey").toString());
                if (o.contains("shortId")) m_realSid->setText(o.value("shortId").toString());
                fromWidgets();
            }
        }

        QString s = "<b>Xray:</b> " + (installed ? ver : QString("не установлен"));
        s += " · <b>сервис:</b> " + (haveUnit ? active : QString("нет"));
        s += QString(" · <b>порт:</b> ") + (port == "yes" ? QString("слушается") : QString("не слушается"));
        if (cfg == "yes") s += " · <b>конфиг:</b> есть";
        if (side == "yes") s += " · <b>параметры:</b> подставлены с VDS";
        s += QString(" · <b>пинг:</b> ") + (ping.isEmpty() ? QString("n/a") : ping);
        m_realStatus->setText(s);

        m_realInstallBtn->setText((installed && haveUnit) ? "Обновить Xray и сервис" : "Установить Xray и развернуть");
        m_realInstallBtn->setToolTip(installed ? "Обновить Xray и переразвернуть сервис"
                                               : "Установить Xray, сгенерировать ключи и развернуть сервис");
        m_realGenBtn->setEnabled(installed);
        m_realStartBtn->setEnabled(haveUnit && !isActive);
        m_realStopBtn->setEnabled(haveUnit && isActive);
        m_realRestartBtn->setEnabled(haveUnit);
        m_realRemoveBtn->setEnabled(installed || haveUnit);
    });
}

// ---------- Reality: клиент (системный TUN-туннель через sing-box) ----------

static QString realityDataDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/.local/share/sni-relay-manager";
    return base;
}

bool MainWindow::ensureSingboxBinary(QString& exeOut, QString& errOut) {
#ifdef Q_OS_WIN
    const QString name = "sing-box.exe";
#else
    const QString name = "sing-box";
#endif
    const QString inPath = sshutil::tool("sing-box");
    { QProcess p; p.start(inPath, { "version" }); if (p.waitForFinished(4000) && p.exitCode() == 0) { exeOut = inPath; return true; } }
    const QString managed = realityDataDir() + "/" + name;
    if (QFileInfo::exists(managed)) {
        QProcess p; p.start(managed, { "version" });
        if (p.waitForFinished(4000) && p.exitCode() == 0) { exeOut = managed; return true; }
    }
    QDir().mkpath(realityDataDir());
    QString asset;
    const QString arch = QSysInfo::currentCpuArchitecture();
#ifdef Q_OS_WIN
    asset = "sing-box-windows-amd64.zip";
#elif defined(Q_OS_MACOS)
    asset = (arch == "arm64") ? "sing-box-darwin-arm64.tar.gz" : "sing-box-darwin-amd64.tar.gz";
#else
    if (arch == "arm64" || arch == "aarch64") asset = "sing-box-linux-arm64.tar.gz";
    else if (arch.contains("arm")) asset = "sing-box-linux-armv7.tar.gz";
    else asset = "sing-box-linux-amd64.tar.gz";
#endif
    // Имена ассетов sing-box содержат версию → сначала узнаём последний тег.
    QString ver;
    {
        QProcess g;
        g.start(sshutil::tool("curl"), { "-fsSL", "--max-time", "20",
            "https://api.github.com/repos/SagerNet/sing-box/releases/latest" });
        if (g.waitForFinished(25000) && g.exitCode() == 0) {
            const QString j = QString::fromLocal8Bit(g.readAllStandardOutput());
            const QRegularExpression re("\"tag_name\"\\s*:\\s*\"v?([0-9][^\"]*)\"");
            const auto m = re.match(j);
            if (m.hasMatch()) ver = m.captured(1);
        }
    }
    const QString suffix = asset.mid(QString("sing-box-").size()); // напр. linux-amd64.tar.gz
    const QString url = ver.isEmpty()
        ? "https://github.com/SagerNet/sing-box/releases/latest/download/" + asset
        : "https://github.com/SagerNet/sing-box/releases/download/v" + ver + "/sing-box-" + ver + "-" + suffix;
    const QString dl = QDir::tempPath() + "/srm-singbox" + (asset.endsWith(".zip") ? ".zip" : ".tar.gz");
    log("Скачиваю sing-box: " + url);
    {
        QProcess c;
        c.start(sshutil::tool("curl"), { "-fsSL", "--max-time", "180", "-o", dl, url });
        if (!c.waitForFinished(200000) || c.exitCode() != 0) { errOut = "не удалось скачать sing-box"; return false; }
    }
    const QString tmp = QDir::tempPath() + "/srm-singbox-extract";
    QDir(tmp).removeRecursively(); QDir().mkpath(tmp);
    bool ok = false;
#ifdef Q_OS_WIN
    {
        QProcess p; p.start("powershell.exe", { "-NoProfile", "-Command",
            QString("Expand-Archive -Force -LiteralPath '%1' -DestinationPath '%2'").arg(dl, tmp) });
        if (p.waitForFinished(60000) && p.exitCode() == 0) ok = true;
    }
#else
    {
        QProcess p; p.start("tar", { "xzf", dl, "-C", tmp, "--strip-components=1" });
        if (p.waitForFinished(60000) && p.exitCode() == 0) ok = true;
    }
#endif
    QFile::remove(dl);
    QString found;
    if (ok) {
        if (QFileInfo::exists(tmp + "/" + name)) found = tmp + "/" + name;
        else {
            const QStringList hits = QDir(tmp).entryList(QStringList() << name, QDir::Files, QDir::NoSort);
            if (!hits.isEmpty()) found = tmp + "/" + hits.first();
        }
    }
    if (found.isEmpty() || !QFileInfo::exists(found)) { errOut = "не удалось распаковать sing-box"; return false; }
    QFile::remove(managed);
    if (!QFile::copy(found, managed)) { errOut = "не удалось установить sing-box"; return false; }
    QFile::setPermissions(managed, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                   QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                   QFileDevice::ReadOther | QFileDevice::ExeOther);
    QDir(tmp).removeRecursively();
    QProcess p; p.start(managed, { "version" });
    if (!(p.waitForFinished(4000) && p.exitCode() == 0)) { errOut = "скачанный sing-box не запускается"; return false; }
    exeOut = managed;
    return true;
}

void MainWindow::sudoRun(const QStringList& args, const QString& desc, DoneFn done) {
    if (m_sudoPass.isEmpty()) {
        bool ok = false;
        const QString p = QInputDialog::getText(this, "sudo",
            "Пароль sudo (для запуска системного туннеля):", QLineEdit::Password, "", &ok);
        if (!ok || p.isEmpty()) { logErr("нужен пароль sudo"); if (done) done(false, QString()); return; }
        m_sudoPass = p;
    }
    runProcess("sudo", QStringList{ "-S", "-p", "" } + args,
               QProcessEnvironment::systemEnvironment(), desc, (m_sudoPass + "\n").toUtf8(),
               [this, done](bool ok, const QString& out) {
                   if (!ok && out.contains("incorrect password", Qt::CaseInsensitive)) m_sudoPass.clear();
                   if (done) done(ok, out);
               }, true);
}

void MainWindow::stopTun() {
    if (!m_tunRunning) return;
    QProcess p;
    p.start("sudo", { "-S", "-p", "", "systemctl", "stop", "sni-relay-tun" });
    p.write((m_sudoPass + "\n").toUtf8()); p.closeWriteChannel();
    p.waitForFinished(5000);
    m_tunRunning = false;
}

void MainWindow::updateRealityClientUi() {
    if (m_realConnectBtn) m_realConnectBtn->setEnabled(!m_tunRunning);
    if (m_realDisconnectBtn) m_realDisconnectBtn->setEnabled(m_tunRunning);
    if (m_realClientStatus) {
        m_realClientStatus->setText(m_tunRunning
            ? "<b>Подключено</b> · системный TUN-туннель (весь трафик через VDS)"
            : "Отключено");
    }
}

void MainWindow::onRealityConnect() {
    fromWidgets();
    const bool grpc = (m_set.realityTransport.trimmed() == "grpc");
    if (m_set.host.trimmed().isEmpty() || m_set.realityUuid.trimmed().isEmpty()) {
        logErr("Нужны хост VDS и UUID — сначала разверни Reality"); return;
    }
    if (!grpc && m_set.realityPublicKey.trimmed().isEmpty()) {
        logErr("Нужен public key (Reality) — сначала разверни на VDS"); return;
    }
    showBusy("Подготовка TUN-туннеля…");
    QApplication::processEvents();
    QString exe, err;
    if (!ensureSingboxBinary(exe, err)) { hideBusy(); logErr("sing-box: " + err); return; }
    const QString cfgPath = realityDataDir() + "/singbox.json";
    {
        QFile f(cfgPath);
        if (!f.open(QIODevice::WriteOnly)) { hideBusy(); logErr("не удалось записать singbox.json"); return; }
        f.write(gen::realityTunConfig(m_set).toUtf8());
        f.close();
    }
    sudoRun({ "systemctl", "stop", "sni-relay-tun" }, "Стоп старого туннеля", [this, exe, cfgPath](bool, const QString&) {
        showBusy("Запуск sing-box (TUN)…");
        sudoRun({ "systemd-run", "--system", "--unit=sni-relay-tun", "--collect",
                  "--property=Type=simple", "--property=Restart=on-failure",
                  exe, "run", "-c", cfgPath }, "Запуск sing-box", [this](bool, const QString&) {
            QThread::msleep(1500);
            sudoRun({ "systemctl", "is-active", "sni-relay-tun" }, "Проверка туннеля", [this](bool ok2, const QString& out2) {
                hideBusy();
                m_tunRunning = ok2 && out2.contains("active");
                if (m_tunRunning) logOk("TUN-туннель поднят — весь трафик через VDS");
                else logErr("не удалось поднять туннель: " + out2.trimmed());
                updateRealityClientUi();
            });
        });
    });
}

void MainWindow::onRealityDisconnect() {
    showBusy("Отключение туннеля…");
    QApplication::processEvents();
    sudoRun({ "systemctl", "stop", "sni-relay-tun" }, "Остановка туннеля", [this](bool, const QString&) {
        hideBusy();
        m_tunRunning = false;
        logOk("Туннель отключён");
        updateRealityClientUi();
    });
}

void MainWindow::onRealityCheckIp() {
    fromWidgets();
    if (!m_tunRunning) { logErr("Сначала «Подключиться»"); return; }
    showBusy("Проверка внешнего IP…");
    auto* p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [this, p](int, QProcess::ExitStatus) {
        hideBusy();
        const QString ip = QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed();
        if (!ip.isEmpty()) {
            const bool ok = (ip == m_set.host.trimmed());
            log((ok ? QString("[OK] ") : QString("[!] ")) + "внешний IP: " + ip +
                (ok ? QString(" (VDS)") : QString(" — ожидался ") + m_set.host.trimmed()));
        } else logErr("не удалось получить IP");
        p->deleteLater();
    });
    p->start(sshutil::tool("curl"), { "-s", "--max-time", "15", "https://ipinfo.io/ip" });
}
// ---------- Интеграции ----------

void MainWindow::checkBrowser() {
#ifdef Q_OS_WIN
    m_intBrowserStatus->setText("Windows: флаги Chromium задаются не через .conf — укажите "
                                "<code>--disable-features=AsyncDns</code> в ярлыке/политике вручную и "
                                "выключите Secure DNS в настройках браузера.");
    return;
#endif
    QStringList lines;
    const QList<QPair<QString, QString>> files = {
        { QDir::homePath() + "/.config/chromium-flags.conf", "Chromium" },
        { QDir::homePath() + "/.config/chrome-flags.conf",   "Google Chrome" },
    };
    for (const auto& f : files) {
        QFile file(f.first);
        if (!file.exists()) { lines << QString("%1: конфиг-файл не найден").arg(f.second); continue; }
        const QString c = file.open(QIODevice::ReadOnly) ? QString::fromLocal8Bit(file.readAll()) : QString();
        lines << QString("%1: AsyncDns %2").arg(f.second, c.contains("AsyncDns") ? "уже отключён" : "включён (резолвит мимо /etc/hosts)");
    }
    QStringList inst;
    for (const char* b : { "chromium", "google-chrome", "google-chrome-stable", "brave" })
        if (commandExists(b)) inst << b;
    QString s = lines.join("<br>");
    s += "<br>Браузеры: " + (inst.isEmpty() ? QString("не найдены") : inst.join(", "));
    s += "<br>Не забудь выключить <b>Secure DNS (DoH)</b>.";
    m_intBrowserStatus->setText(s);
}

void MainWindow::fixBrowserAsyncDns() {
#ifdef Q_OS_WIN
    log("Windows: добавь --disable-features=AsyncDns в ярлык Chrome/Chromium вручную.");
    return;
#endif
    const QByteArray flag("--disable-features=AsyncDns\n");
    auto fixFile = [&](const QString& path, const QString& name, bool applicable) -> QString {
        if (!applicable) return QString();
        QFile f(path);
        QString content;
        if (f.exists()) {
            if (f.open(QIODevice::ReadOnly)) { content = QString::fromLocal8Bit(f.readAll()); f.close(); }
            if (content.contains("AsyncDns")) return name + ": уже есть";
        }
        QFile out(path);
        if (!out.open(QIODevice::Append)) return name + ": не удалось записать";
        if (!content.isEmpty() && !content.endsWith('\n')) out.write("\n");
        out.write(flag);
        out.close();
        return name + ": добавлено";
    };
    const bool hasChromium = QDir(QDir::homePath() + "/.config/chromium").exists() || commandExists("chromium");
    const bool hasChrome = QDir(QDir::homePath() + "/.config/google-chrome").exists()
                           || commandExists("google-chrome") || commandExists("google-chrome-stable");
    QStringList done;
    if (hasChromium) done << fixFile(QDir::homePath() + "/.config/chromium-flags.conf", "Chromium", true);
    if (hasChrome)   done << fixFile(QDir::homePath() + "/.config/chrome-flags.conf", "Google Chrome", true);
    if (done.isEmpty()) done << "Chrome/Chromium не найдены";
    log("AsyncDns: " + done.join("; "));
    checkBrowser();
}

void MainWindow::checkZapretOverlap() {
#ifdef Q_OS_WIN
    m_intZapretStatus->setText("zapret-списки — только Linux (/opt/zapret). На Windows не применимо.");
    return;
#endif
    fromWidgets();
    const QStringList paths = {
        "/opt/zapret/hostlists/list-general.txt",
        "/opt/zapret/hostlists/list-general-user.txt",
    };
    QSet<QString> zap;
    bool read = false;
    for (const QString& p : paths) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) continue;
        read = true;
        for (QString l : QString::fromLocal8Bit(f.readAll()).split('\n')) {
            l = l.trimmed();
            if (!l.isEmpty() && !l.startsWith('#')) zap.insert(l);
        }
    }
    if (!read) { m_intZapretStatus->setText("Не удалось прочитать списки zapret (нужен /opt/zapret/hostlists)"); return; }
    QStringList inZ, outZ;
    for (const auto& d : m_set.domains) {
        const QString t = d.trimmed();
        if (t.isEmpty()) continue;
        (zap.contains(t) ? inZ : outZ) << t;
    }
    QString s = QString("<b>В zapret-списках:</b> %1 · <b>вне списков:</b> %2").arg(inZ.size()).arg(outZ.size());
    s += "<br>В списках (десинк прячет SNI): " + (inZ.isEmpty() ? QString("—") : inZ.join(", "));
    m_intZapretStatus->setText(s);
    log(QString("Пересечение с zapret: в списках %1, вне %2").arg(inZ.size()).arg(outZ.size()));
}

void MainWindow::showVdsPorts() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Укажи VDS на вкладке «VDS»"); return; }
    showBusy("Чтение портов VDS…");
    runSsh("ss -tlnp 2>/dev/null", "Порты VDS", [this](bool ok, const QString& out) {
        hideBusy();
        if (!ok) { m_intPortsStatus->setText("Не удалось получить список портов"); return; }
        m_intPortsStatus->setText("Занятые порты получены — открыто окно");
        showTextDialog("Занятые порты на VDS", out);
    });
}

void MainWindow::pickFreeMtgPort() {
    fromWidgets();
    if (m_set.host.trimmed().isEmpty()) { logErr("Укажи VDS на вкладке «VDS»"); return; }
    showBusy("Подбор свободного порта…");
    const QString sc = QString::fromUtf8(R"SH(for p in 2053 2083 2087 2096 8443 9443 10443 10555 11000 12000 13000 14000; do ss -tln 2>/dev/null | grep -q ":$p " || { echo PORT=$p; exit 0; }; done; echo PORT=
)SH");
    runSsh(sc, "Свободный порт mtg", [this](bool, const QString& out) {
        hideBusy();
        QString p;
        for (const QString& l : out.split('\n')) {
            const QString t = l.trimmed();
            if (t.startsWith("PORT=")) { p = t.mid(5).trimmed(); break; }
        }
        if (p.isEmpty()) { m_intPortsStatus->setText("Свободный порт в наборе не найден"); return; }
        m_mtgPort->setValue(p.toInt());
        fromWidgets();
        m_intPortsStatus->setText(QString("Свободный порт для mtg: <b>%1</b> (подставлен во вкладке MTProto)").arg(p));
        logOk("Свободный порт mtg: " + p);
    });
}
