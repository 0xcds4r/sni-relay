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
class QProgressBar;
class QResizeEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* e) override;

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

    // MTProto
    void onMtgInstallDeploy();
    void onMtgGenSecret();
    void onMtgDeploy();
    void onMtgRemove();
    void onMtgStart();
    void onMtgStop();
    void onMtgRestart();
    void onMtgStatus();
    void onMtgShowQr();
    void onMtgSaveQr();
    void onMtgOpenTelegram();
    void refreshMtgStatus();

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
    void launchCheckTask();
    void setCheckRow(int row, const QString& domain, const QString& http,
                     const QString& status, const QString& details);

    // --- загрузочный оверлей ---
    void showBusy(const QString& text);
    void hideBusy();

    // --- окно с текстом (просмотр конфига и т.п.) ---
    void showTextDialog(const QString& title, const QString& content);

    // эффективный порт прокси для клиента (443 при выводе через релей)
    QString mtgEffPort() const;

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
    QLabel* m_vdsStatus = nullptr;

    // relay settings
    QLineEdit* m_relayExit = nullptr;
    QCheckBox* m_hasSite = nullptr;
    QLineEdit* m_siteBackend = nullptr;
    QListWidget* m_siteDomains = nullptr;
    QListWidget* m_domains = nullptr;

    // MTProto
    QSpinBox* m_mtgPort = nullptr;
    QComboBox* m_mtgFront = nullptr;
    QLineEdit* m_mtgSecret = nullptr;
    QCheckBox* m_mtgVia443 = nullptr;
    QLabel* m_mtgLink = nullptr;
    QLabel* m_mtgQr = nullptr;
    QLabel* m_mtgStatus = nullptr;
    QPushButton* m_mtgInstallBtn = nullptr;
    QPushButton* m_mtgGenBtn = nullptr;
    QPushButton* m_mtgStartBtn = nullptr;
    QPushButton* m_mtgStopBtn = nullptr;
    QPushButton* m_mtgRestartBtn = nullptr;
    QPushButton* m_mtgStatusBtn = nullptr;
    QPushButton* m_mtgRemoveBtn = nullptr;

    // check
    QTableWidget* m_checkTable = nullptr;
    QStringList m_checkQueue;
    int m_checkIdx = 0;
    int m_checkActive = 0;

    QPlainTextEdit* m_log = nullptr;

    // оверлей загрузки
    QWidget* m_busy = nullptr;
    QLabel* m_busyLabel = nullptr;
    QProgressBar* m_busyBar = nullptr;
    bool m_busyCursor = false;
};
