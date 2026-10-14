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
class QCloseEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onTestConnection();
    void onDeploy();
    void onRollback();
    void onApplyHosts();
    void onRemoveHosts();
    void onShowHostsEntries();
    bool writeHostsDirect(const QString& content);
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

    // Reality (VLESS + Xray)
    void onRealityInstallDeploy();
    void onRealityGenKeys();
    void onRealityDeploy();
    void onRealityRemove();
    void onRealityStart();
    void onRealityStop();
    void onRealityRestart();
    void onRealityStatus();
    void onRealityShowQr();
    void onRealitySaveQr();
    void onRealityCopyLink();
    void refreshRealityStatus();
    void onRealityConnect();
    void onRealityDisconnect();
    void onRealityCheckIp();

    // Интеграции
    void checkBrowser();
    void fixBrowserAsyncDns();
    void checkZapretOverlap();
    void showVdsPorts();
    void pickFreeMtgPort();

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

    // Reality-клиент: системный TUN-туннель через sing-box
    bool ensureSingboxBinary(QString& exeOut, QString& errOut);
    void sudoRun(const QStringList& args, const QString& desc, DoneFn done = {});
    void stopTun();
    void updateRealityClientUi();

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
    QLabel* m_mtgQr = nullptr;
    QLabel* m_mtgStatus = nullptr;
    QPushButton* m_mtgInstallBtn = nullptr;
    QPushButton* m_mtgGenBtn = nullptr;
    QPushButton* m_mtgStartBtn = nullptr;
    QPushButton* m_mtgStopBtn = nullptr;
    QPushButton* m_mtgRestartBtn = nullptr;
    QPushButton* m_mtgStatusBtn = nullptr;
    QPushButton* m_mtgRemoveBtn = nullptr;

    // Reality (VLESS + Xray)
    QSpinBox* m_realPort = nullptr;
    QComboBox* m_realDest = nullptr;
    QComboBox* m_realFlow = nullptr;
    QComboBox* m_realFp = nullptr;
    QLineEdit* m_realUuid = nullptr;
    QLineEdit* m_realPub = nullptr;
    QLineEdit* m_realPriv = nullptr;
    QLineEdit* m_realSid = nullptr;
    QComboBox* m_realTransport = nullptr;
    QLineEdit* m_realDomain = nullptr;
    QLineEdit* m_realService = nullptr;
    QLineEdit* m_realGrpcPort = nullptr;
    QLabel* m_realStatus = nullptr;
    QLabel* m_realQr = nullptr;
    QPushButton* m_realInstallBtn = nullptr;
    QPushButton* m_realGenBtn = nullptr;
    QPushButton* m_realStartBtn = nullptr;
    QPushButton* m_realStopBtn = nullptr;
    QPushButton* m_realRestartBtn = nullptr;
    QPushButton* m_realStatusBtn = nullptr;
    QPushButton* m_realRemoveBtn = nullptr;
    QPushButton* m_realConnectBtn = nullptr;
    QPushButton* m_realDisconnectBtn = nullptr;
    QLabel* m_realClientStatus = nullptr;
    QString m_sudoPass;                 // пароль sudo (только в памяти сессии)
    bool m_tunRunning = false;          // активен ли системный TUN-туннель

    // Интеграции
    QLabel* m_intBrowserStatus = nullptr;
    QLabel* m_intZapretStatus = nullptr;
    QLabel* m_intPortsStatus = nullptr;

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
