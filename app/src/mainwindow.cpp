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
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QColor>
#include <QCoreApplication>
#include <QSplitter>
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
