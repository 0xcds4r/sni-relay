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
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
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

void MainWindow::log(const QString& s) {
    if (!m_log) return;
    m_log->appendPlainText(s);
}
void MainWindow::logOk(const QString& s)  { log("[OK] " + s); }
void MainWindow::logErr(const QString& s) { log("[!!] " + s); }

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

    auto* central = new QWidget(this);
    auto* rootLay = new QVBoxLayout(central);

    auto* split = new QSplitter(Qt::Vertical, central);

    auto* tabs = new QTabWidget(split);

    // ===== VDS =====
    {
        auto* w = new QWidget;
        auto* form = new QFormLayout(w);
        m_host = new QLineEdit; form->addRow("Хост / IP", m_host);
        m_port = new QLineEdit; form->addRow("SSH порт", m_port);
        m_user = new QLineEdit; form->addRow("Пользователь", m_user);

        auto* keyRow = new QWidget;
        auto* keyLay = new QHBoxLayout(keyRow); keyLay->setContentsMargins(0,0,0,0);
        m_keyPath = new QLineEdit; m_browseKey = new QPushButton("…", keyRow);
        keyLay->addWidget(m_keyPath); keyLay->addWidget(m_browseKey);
        form->addRow("Приватный ключ", keyRow);

        m_usePassword = new QCheckBox("Использовать пароль");
        form->addRow(QString(), m_usePassword);
        m_password = new QLineEdit; m_password->setEchoMode(QLineEdit::Password);
        form->addRow("Пароль", m_password);
        m_savePassword = new QCheckBox("Сохранить пароль в конфиг (небезопасно)");
        form->addRow(QString(), m_savePassword);

        auto* btns = new QWidget;
        auto* bl = new QHBoxLayout(btns); bl->setContentsMargins(0,0,0,0);
        auto* test = new QPushButton("Проверить подключение");
        bl->addWidget(test); bl->addStretch();
        form->addRow(btns);

        connect(test, &QPushButton::clicked, this, &MainWindow::onTestConnection);
        connect(m_usePassword, &QCheckBox::toggled, m_password, &QLineEdit::setEnabled);
        connect(m_browseKey, &QPushButton::clicked, this, &MainWindow::browseKey);

        tabs->addTab(w, "VDS");
    }

    // ===== Домены =====
    {
        auto* w = new QWidget;
        auto* lay = new QHBoxLayout(w);
        auto* left = new QWidget; auto* ll = new QVBoxLayout(left);
        ll->addWidget(new QLabel("Домены для релея:"));
        m_domains = new QListWidget; ll->addWidget(m_domains);
        auto* dr = new QWidget; auto* drl = new QHBoxLayout(dr); drl->setContentsMargins(0,0,0,0);
        auto* add = new QPushButton("Добавить"); auto* rem = new QPushButton("Удалить");
        drl->addWidget(add); drl->addWidget(rem); drl->addStretch();
        ll->addWidget(dr);
        auto* dr2 = new QWidget; auto* dr2l = new QHBoxLayout(dr2); dr2l->setContentsMargins(0,0,0,0);
        auto* imp = new QPushButton("Импорт…"); auto* exp = new QPushButton("Экспорт…"); auto* rst = new QPushButton("Список по умолчанию");
        dr2l->addWidget(imp); dr2l->addWidget(exp); dr2l->addWidget(rst); dr2l->addStretch();
        ll->addWidget(dr2);
        lay->addWidget(left, 2);

        auto* right = new QWidget; auto* rl = new QVBoxLayout(right);
        rl->addWidget(new QLabel("SNI локального сайта (на VDS):"));
        m_siteDomains = new QListWidget; rl->addWidget(m_siteDomains);
        auto* sr = new QWidget; auto* srl = new QHBoxLayout(sr); srl->setContentsMargins(0,0,0,0);
        auto* sadd = new QPushButton("Добавить"); auto* srem = new QPushButton("Удалить");
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

        tabs->addTab(w, "Домены");
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
        connect(prev, &QPushButton::clicked, this, [this] { fromWidgets(); log("--- relay.conf ---\n" + generateRelayConf() + "------------------"); });
        tabs->addTab(w, "Релей");
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
        tabs->addTab(w, "Клиент");
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
        tabs->addTab(w, "Проверка");
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
        tabs->addTab(w, "О программе");
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
        tabs->addTab(w, "FAQ");
    }

    // ===== MTProto =====
    {
        auto* w = new QWidget;
        auto* lay = new QVBoxLayout(w);

        auto* form = new QFormLayout;
        m_mtgPort = new QLineEdit; m_mtgPort->setFixedWidth(120);
        m_mtgFront = new QLineEdit;
        m_mtgSecret = new QLineEdit; m_mtgSecret->setPlaceholderText("сгенерировать / вставить…");
        form->addRow("Порт прокси", m_mtgPort);
        form->addRow("Фронт-домен (маскировка)", m_mtgFront);
        form->addRow("Secret", m_mtgSecret);
        lay->addLayout(form);

        m_mtgStatus = new QLabel("Статус: нажми «Обновить статус»");
        m_mtgStatus->setTextFormat(Qt::RichText);
        m_mtgStatus->setWordWrap(true);
        lay->addWidget(m_mtgStatus);

        auto* b1 = new QWidget; auto* l1 = new QHBoxLayout(b1); l1->setContentsMargins(0, 0, 0, 0);
        m_mtgInstallBtn = new QPushButton("Установить mtg на VDS");
        m_mtgGenBtn = new QPushButton("Сгенерировать секрет");
        l1->addWidget(m_mtgInstallBtn); l1->addWidget(m_mtgGenBtn); l1->addStretch();
        lay->addWidget(b1);

        auto* b2 = new QWidget; auto* l2 = new QHBoxLayout(b2); l2->setContentsMargins(0, 0, 0, 0);
        m_mtgDeployBtn = new QPushButton("Развернуть сервис");
        m_mtgStartBtn = new QPushButton("Старт");
        m_mtgStopBtn = new QPushButton("Стоп");
        m_mtgRestartBtn = new QPushButton("Рестарт");
        m_mtgStatusBtn = new QPushButton("Обновить статус");
        l2->addWidget(m_mtgDeployBtn); l2->addWidget(m_mtgStartBtn); l2->addWidget(m_mtgStopBtn);
        l2->addWidget(m_mtgRestartBtn); l2->addWidget(m_mtgStatusBtn);
        l2->addStretch();
        lay->addWidget(b2);

        auto* b3 = new QWidget; auto* l3 = new QHBoxLayout(b3); l3->setContentsMargins(0, 0, 0, 0);
        auto* qr = new QPushButton("Показать ссылку и QR");
        auto* save = new QPushButton("Сохранить QR…");
        l3->addWidget(qr); l3->addWidget(save); l3->addStretch();
        lay->addWidget(b3);

        m_mtgLink = new QLabel; m_mtgLink->setTextFormat(Qt::RichText);
        m_mtgLink->setOpenExternalLinks(true);
        m_mtgLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_mtgLink->setWordWrap(true);
        lay->addWidget(m_mtgLink);

        m_mtgQr = new QLabel;
        m_mtgQr->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        lay->addWidget(m_mtgQr);

        auto* hint = new QLabel(
            "MTProto-прокси для Telegram Desktop/мобильного (SNI-релей их не покрывает — ядро идёт по IP).\n"
            "Порядок: «Установить mtg на VDS» → «Сгенерировать секрет» → «Развернуть сервис» → «Показать QR».");
        hint->setWordWrap(true);
        lay->addWidget(hint);
        lay->addStretch();

        connect(m_mtgInstallBtn, &QPushButton::clicked, this, &MainWindow::onMtgInstall);
        connect(m_mtgGenBtn, &QPushButton::clicked, this, &MainWindow::onMtgGenSecret);
        connect(m_mtgDeployBtn, &QPushButton::clicked, this, &MainWindow::onMtgDeploy);
        connect(m_mtgStartBtn, &QPushButton::clicked, this, &MainWindow::onMtgStart);
        connect(m_mtgStopBtn, &QPushButton::clicked, this, &MainWindow::onMtgStop);
        connect(m_mtgRestartBtn, &QPushButton::clicked, this, &MainWindow::onMtgRestart);
        connect(m_mtgStatusBtn, &QPushButton::clicked, this, &MainWindow::refreshMtgStatus);
        connect(qr, &QPushButton::clicked, this, &MainWindow::onMtgShowQr);
        connect(save, &QPushButton::clicked, this, &MainWindow::onMtgSaveQr);

        tabs->addTab(w, "MTProto");

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
    resize(1000, 720);
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
    m_mtgPort->setText(m_set.mtgPort);
    m_mtgFront->setText(m_set.mtgFront);
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
    m_set.mtgPort = trim(m_mtgPort->text());
    m_set.mtgFront = trim(m_mtgFront->text());
    m_set.mtgSecret = trim(m_mtgSecret->text());
    m_set.save();
}

// ---------- слоты: VDS ----------

void MainWindow::browseKey() {
    QString f = QFileDialog::getOpenFileName(this, "Приватный ключ", QDir::homePath() + "/.ssh");
    if (!f.isEmpty()) m_keyPath->setText(f);
}

void MainWindow::onTestConnection() {
    QString script =
        "echo '== host =='; hostname; . /etc/os-release 2>/dev/null; echo \"$PRETTY_NAME\"\n"
        "echo '== geo =='; curl -s --max-time 8 https://ipinfo.io/json 2>/dev/null; echo\n"
        "echo '== nginx =='; nginx -v 2>&1\n"
        "ls /etc/nginx/modules-enabled 2>/dev/null | grep -q stream && echo 'stream module: yes' || echo 'stream module: NO'\n"
        "echo '== claude с VDS =='; curl -sI --max-time 10 https://claude.ai/ 2>/dev/null | grep -iE '^HTTP|^location|cf-mitigated'\n"
        "echo '== listening =='; ss -tlnp 2>/dev/null | grep -E ':(443|8443|9443)\\b' || true\n";
    runSsh(script, "Проверка подключения к VDS");
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
    runSsh(remoteDeployScript(), "Развёртывание релея", [this](bool ok, const QString& out) {
        if (ok && out.contains("DEPLOY_OK")) logOk("Релей развёрнут и nginx перезагружен");
        else logErr("Развёртывание не удалось — см. вывод выше");
    });
}

void MainWindow::onRollback() {
    fromWidgets();
    runSsh(remoteRollbackScript(), "Откат конфига nginx на VDS");
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
    runElevated(script, "Прописать домены в /etc/hosts", [this](bool ok, const QString& out) {
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
    runElevated(script, "Убрать домены из /etc/hosts", [this](bool ok, const QString& out) {
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
    log("--- записи /etc/hosts для доменов релея ---");
    int n = 0;
    for (const QString& line : QString::fromLocal8Bit(f.readAll()).split('\n')) {
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 1; i < parts.size(); ++i)
            if (want.contains(parts[i])) { log(line); ++n; break; }
    }
    if (n == 0) log("(нет)");
    log("------------------------------------------");
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
    if (m_checkQueue.isEmpty()) { logErr("Список доменов пуст"); return; }
    m_checkTable->setRowCount(m_checkQueue.size());
    for (int i = 0; i < m_checkQueue.size(); ++i)
        m_checkTable->setItem(i, 0, new QTableWidgetItem(m_checkQueue[i]));
    log("Проверка " + QString::number(m_checkQueue.size()) + " доменов…");
    checkNext();
}

void MainWindow::checkNext() {
    if (m_checkIdx >= m_checkQueue.size()) { logOk("Проверка завершена"); return; }
    const int row = m_checkIdx;
    const QString d = m_checkQueue[m_checkIdx++];
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
            checkNext();
        }, false);
}

// ---------- MTProto (mtg) ----------

void MainWindow::onMtgInstall() {
    fromWidgets();
    const QString script = R"BASH(set -e
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
    runSsh(script, "Установка mtg на VDS", [this](bool, const QString&) { refreshMtgStatus(); });
}

void MainWindow::onMtgGenSecret() {
    fromWidgets();
    const QString front = m_set.mtgFront.trimmed().isEmpty() ? QString("www.google.com") : m_set.mtgFront.trimmed();
    const QString script = QString("set -e\ncommand -v mtg >/dev/null || { echo NO_MTG; exit 1; }\nmtg generate-secret %1\n").arg(qshell(front));
    runSsh(script, "Генерация секрета mtg", [this](bool ok, const QString& out) {
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
    if (m_set.mtgSecret.trimmed().isEmpty()) { logErr("Сначала сгенерируй секрет"); return; }
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
    runSsh(script, "Развёртывание mtg на VDS", [this](bool ok, const QString& out) {
        if (ok && out.contains("MTG_DEPLOYED")) logOk("MTProto-прокси развёрнут");
        else logErr("Развёртывание mtg не удалось");
        refreshMtgStatus();
    });
}

void MainWindow::onMtgStart()   { fromWidgets(); runSsh("systemctl start mtg && systemctl is-active mtg", "Старт mtg", [this](bool, const QString&) { refreshMtgStatus(); }); }
void MainWindow::onMtgStop()    { fromWidgets(); runSsh("systemctl stop mtg || true", "Стоп mtg", [this](bool, const QString&) { refreshMtgStatus(); }); }
void MainWindow::onMtgRestart() { fromWidgets(); runSsh("systemctl restart mtg && systemctl is-active mtg", "Рестарт mtg", [this](bool, const QString&) { refreshMtgStatus(); }); }

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
    m_mtgLink->setText(QString("Ссылка (открой на телефоне): <a href=\"%1\">%1</a><br>tg: <code>%2</code>").arg(url, tg));
    const QImage img = makeQrImage(url, 300);
    if (!img.isNull()) m_mtgQr->setPixmap(QPixmap::fromImage(img));
    logOk("Ссылка и QR готовы");
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
    const QString port = m_set.mtgPort.trimmed().isEmpty() ? QString("10443") : m_set.mtgPort.trimmed();
    const QString script = QString(
        "command -v mtg >/dev/null 2>&1 && echo \"VER=$(mtg --version 2>/dev/null | head -1 | awk '{print $1}')\" || echo VER=NO\n"
        "[ -f /etc/systemd/system/mtg.service ] && echo UNIT=yes || echo UNIT=no\n"
        "echo \"ACTIVE=$(systemctl is-active mtg 2>/dev/null || true)\"\n"
        "ss -tlnp 2>/dev/null | grep -q ':%1 ' && echo PORT=yes || echo PORT=no\n"
    ).arg(port);
    runSsh(script, "Статус mtg", [this](bool ok, const QString& out) {
        QString ver = "?", unit = "no", active = "inactive", port = "no";
        for (const QString& l : out.split('\n')) {
            const QString t = l.trimmed();
            if (t.startsWith("VER=")) ver = t.mid(4);
            else if (t.startsWith("UNIT=")) unit = t.mid(5);
            else if (t.startsWith("ACTIVE=")) active = t.mid(7);
            else if (t.startsWith("PORT=")) port = t.mid(5);
        }
        if (!ok && ver == "?") { m_mtgStatus->setText("Статус: не удалось получить (VDS/SSH?)"); return; }
        const bool installed = (ver != "NO" && ver != "?");
        const bool haveUnit = (unit == "yes");
        const bool isActive = (active == "active");

        QString s = "<b>mtg:</b> " + (installed ? ver : QString("не установлен"));
        s += " · <b>сервис:</b> " + (haveUnit ? active : QString("нет"));
        s += QString(" · <b>порт:</b> ") + (port == "yes" ? QString("слушается") : QString("не слушается"));
        m_mtgStatus->setText(s);

        m_mtgInstallBtn->setText(installed ? "Переустановить / обновить mtg" : "Установить mtg на VDS");
        m_mtgGenBtn->setEnabled(installed);
        m_mtgDeployBtn->setText(haveUnit ? "Обновить сервис" : "Развернуть сервис");
        m_mtgDeployBtn->setEnabled(installed);
        m_mtgStartBtn->setEnabled(haveUnit && !isActive);
        m_mtgStopBtn->setEnabled(haveUnit && isActive);
        m_mtgRestartBtn->setEnabled(haveUnit);
    });
}
