#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QByteArray;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QTimer;
class QWidget;

namespace Vuttara {
class VuttaraEngine;
}

class UpdateManager final : public QObject
{
    Q_OBJECT

public:
    explicit UpdateManager(Vuttara::VuttaraEngine* engine, QWidget* parentWindow);

    void startAutomaticChecks();
    void checkForUpdates(bool interactive);

    static bool runSelfTest(QStringList& lines);

signals:
    void statusMessage(const QString& message, int timeoutMilliseconds);

private:
    struct UpdateInfo
    {
        QString version;
        QString fileName;
        QUrl downloadUrl;
        qint64 expectedSize = 0;
        QString expectedSha256;
        QString summary;
        QStringList changes;
        QUrl releaseNotesUrl;
    };

    static int compareVersions(const QString& left, const QString& right);
    static bool parseFeed(
        const QByteArray& payload,
        UpdateInfo* update,
        QString* errorMessage);
    static bool isTrustedInstallerUrl(const QUrl& url);
    static QString normalizeSha256(const QString& value);
    static QString fileSha256(const QString& path);
    static QString releaseNotesText(const UpdateInfo& update);

    bool outputBusy() const;
    void handleFeedReply(QNetworkReply* reply, bool interactive);
    void promptForDownload(const UpdateInfo& update);
    void beginDownload(const UpdateInfo& update);
    void handleDownloadFinished();
    void promptForInstall(const UpdateInfo& update, const QString& installerPath);
    bool launchInstaller(const UpdateInfo& update, const QString& installerPath);
    void resetDownloadState(bool removePartialFile);
    void reportCheckFailure(const QString& message, bool interactive);

    Vuttara::VuttaraEngine* engine_ = nullptr;
    QWidget* parentWindow_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    QTimer* periodicTimer_ = nullptr;
    QNetworkReply* feedReply_ = nullptr;
    QNetworkReply* downloadReply_ = nullptr;
    QProgressDialog* progressDialog_ = nullptr;
    QFile* downloadFile_ = nullptr;
    UpdateInfo activeUpdate_;
    QString partialDownloadPath_;
    QString completedDownloadPath_;
    bool downloadWriteFailed_ = false;
};
