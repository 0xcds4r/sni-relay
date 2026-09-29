#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <functional>

#include "settings.h"

class QLineEdit;
class QSpinBox;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;
class QLabel;
class QCheckBox;
class QComboBox;
class QTabWidget;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onTestConnection();
    void onDeploy();
    void onRollback();
    void onApplyHosts();
    void onRemoveHosts();
    void onShowHostsEntries();
    void onCheckAll();
    void addDomain();
    void removeDomain();
    void importDomains();
    void exportDomains();
    void resetDomains();
    void addSiteDomain();
    void removeSiteDomain();
    void browseKey();

private:
    void buildUi();
    void toWidgets();
    void fromWidgets();

    // --- процессы ---
    using DoneFn = std::function<void(bool ok, const QString& out)>;
    void runProcess(const QString& program, const QStringList& args,
                    const QProcessEnvironment& env, const QString& desc,
                    const QByteArray& stdinData, DoneFn done, bool streamLog);
    void runSsh(const QString& script, const QString& desc, DoneFn done = {}, bool streamLog = true);
    void runElevated(const QString& script, const QString& desc, DoneFn done = {}, bool streamLog = true);
    bool commandExists(const QString& name) const;

    // --- генерация ---
    QString generateRelayConf() const;
    QString remoteDeployScript() const;
    QString remoteRollbackScript() const;
    QString buildHosts(bool addRelay) const;   // новый /etc/hosts
    QString hostsBlock() const;

    // --- проверка доменов ---
    void checkNext();
    void setCheckRow(int row, const QString& domain, const QString& http,
                     const QString& status, const QString& details);

    // --- утилиты ---
    void log(const QString& s);
    void logOk(const QString& s);
    void logErr(const QString& s);
    QString qshell(const QString& s) const;

    Settings m_set;

    // VDS
    QLineEdit* m_host = nullptr;
    QLineEdit* m_port = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_keyPath = nullptr;
    QPushButton* m_browseKey = nullptr;
    QCheckBox* m_usePassword = nullptr;
    QLineEdit* m_password = nullptr;
    QCheckBox* m_savePassword = nullptr;

    // relay settings
    QLineEdit* m_relayExit = nullptr;
    QCheckBox* m_hasSite = nullptr;
    QLineEdit* m_siteBackend = nullptr;
    QListWidget* m_siteDomains = nullptr;
    QListWidget* m_domains = nullptr;

    // check
    QTableWidget* m_checkTable = nullptr;
    QStringList m_checkQueue;
    int m_checkIdx = 0;

    QPlainTextEdit* m_log = nullptr;
};
