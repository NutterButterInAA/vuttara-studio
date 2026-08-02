#include "UpdateManager.hpp"

#include "engine/VuttaraEngine.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <algorithm>

namespace {

const QUrl kStableFeedUrl(
    QStringLiteral("https://vuttarastudio.nuttabuttainaa.com/updates/clean-rewrite/latest.json"));
constexpr int kAutomaticCheckDelayMilliseconds = 15000;
constexpr int kPeriodicCheckMilliseconds = 6 * 60 * 60 * 1000;

QString currentVersion()
{
    return QStringLiteral(VUTTARA_STUDIO_VERSION);
}

}

UpdateManager::UpdateManager(
    Vuttara::VuttaraEngine* engine,
    QWidget* parentWindow)
    : QObject(parentWindow)
    , engine_(engine)
    , parentWindow_(parentWindow)
    , network_(new QNetworkAccessManager(this))
    , periodicTimer_(new QTimer(this))
{
    periodicTimer_->setInterval(kPeriodicCheckMilliseconds);
    connect(periodicTimer_, &QTimer::timeout, this, [this]() {
        checkForUpdates(false);
    });
}

void UpdateManager::startAutomaticChecks()
{
#ifdef VUTTARA_STUDIO_RELEASE_BUILD
    if (QCoreApplication::arguments().contains(QStringLiteral("--self-test"))) {
        return;
    }

    QTimer::singleShot(kAutomaticCheckDelayMilliseconds, this, [this]() {
        checkForUpdates(false);
    });
    periodicTimer_->start();
#endif
}

void UpdateManager::checkForUpdates(bool interactive)
{
#ifndef VUTTARA_STUDIO_RELEASE_BUILD
    if (interactive) {
        QMessageBox::information(
            parentWindow_,
            QStringLiteral("Vuttara Studio Updates"),
            QStringLiteral(
                "Update checks are disabled in this local development build."));
    }
    return;
#else
    if (feedReply_ != nullptr || downloadReply_ != nullptr) {
        if (interactive) {
            QMessageBox::information(
                parentWindow_,
                QStringLiteral("Vuttara Studio Updates"),
                QStringLiteral("An update operation is already in progress."));
        }
        return;
    }

    QNetworkRequest request(kStableFeedUrl);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("VuttaraStudio/%1 Windows-x64").arg(currentVersion()));
    request.setRawHeader("Accept", "application/json");

    feedReply_ = network_->get(request);
    QNetworkReply* const reply = feedReply_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive]() {
        handleFeedReply(reply, interactive);
    });

    emit statusMessage(QStringLiteral("Checking for Vuttara Studio updates…"), 5000);
#endif
}

int UpdateManager::compareVersions(const QString& left, const QString& right)
{
    const auto parse = [](const QString& version, QVector<int>* parts) {
        static const QRegularExpression pattern(
            QStringLiteral(R"(^\s*(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?\s*$)"));
        const QRegularExpressionMatch match = pattern.match(version);
        if (!match.hasMatch()) {
            return false;
        }

        parts->clear();
        for (int index = 1; index <= 4; ++index) {
            bool ok = false;
            const QString captured = match.captured(index);
            const int value = captured.isEmpty() ? 0 : captured.toInt(&ok);
            if (!captured.isEmpty() && !ok) {
                return false;
            }
            parts->append(value);
        }
        return true;
    };

    QVector<int> leftParts;
    QVector<int> rightParts;
    if (!parse(left, &leftParts) || !parse(right, &rightParts)) {
        return 0;
    }

    for (int index = 0; index < leftParts.size(); ++index) {
        if (leftParts[index] < rightParts[index]) {
            return -1;
        }
        if (leftParts[index] > rightParts[index]) {
            return 1;
        }
    }
    return 0;
}

QString UpdateManager::normalizeSha256(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(normalized).hasMatch() ? normalized : QString{};
}

bool UpdateManager::isTrustedInstallerUrl(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("https")) {
        return false;
    }
    if (url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    return url.path().startsWith(
        QStringLiteral("/NutterButterInAA/vuttara-studio/releases/download/"));
}

bool UpdateManager::parseFeed(
    const QByteArray& payload,
    UpdateInfo* update,
    QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (update == nullptr) {
        return fail(QStringLiteral("No update output object was supplied."));
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("The update feed is not valid JSON."));
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        return fail(QStringLiteral("The update feed schema is unsupported."));
    }
    if (root.value(QStringLiteral("productId")).toString() !=
        QStringLiteral("com.nuttabuttainaa.vuttarastudio")) {
        return fail(QStringLiteral("The update feed belongs to a different product."));
    }
    if (root.value(QStringLiteral("productLine")).toString() !=
        QStringLiteral("clean-rewrite")) {
        return fail(QStringLiteral("The update feed belongs to a different Vuttara Studio product line."));
    }
    if (root.value(QStringLiteral("channel")).toString() != QStringLiteral("stable")) {
        return fail(QStringLiteral("The update feed is not the stable channel."));
    }

    UpdateInfo parsed;
    parsed.version = root.value(QStringLiteral("version")).toString().trimmed();
    if (compareVersions(parsed.version, parsed.version) != 0 ||
        !QRegularExpression(QStringLiteral(R"(^\d+\.\d+\.\d+(?:\.\d+)?$)"))
             .match(parsed.version)
             .hasMatch()) {
        return fail(QStringLiteral("The update feed contains an invalid version."));
    }

    const QJsonObject download = root.value(QStringLiteral("download")).toObject();
    parsed.fileName = download.value(QStringLiteral("fileName")).toString().trimmed();
    parsed.downloadUrl = QUrl(download.value(QStringLiteral("url")).toString());
    parsed.expectedSize = static_cast<qint64>(
        download.value(QStringLiteral("size")).toDouble(0));

    if (parsed.fileName.isEmpty() ||
        QFileInfo(parsed.fileName).fileName() != parsed.fileName ||
        !parsed.fileName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        return fail(QStringLiteral("The update feed contains an unsafe installer filename."));
    }
    if (!isTrustedInstallerUrl(parsed.downloadUrl)) {
        return fail(QStringLiteral("The installer URL is not an approved GitHub release URL."));
    }
    if (QFileInfo(parsed.downloadUrl.path()).fileName() != parsed.fileName) {
        return fail(QStringLiteral("The installer URL and filename do not match."));
    }
    if (parsed.expectedSize <= 0) {
        return fail(QStringLiteral("The installer size is missing or invalid."));
    }

    const QJsonObject integrity = root.value(QStringLiteral("integrity")).toObject();
    if (integrity.value(QStringLiteral("algorithm")).toString().compare(
            QStringLiteral("SHA-256"),
            Qt::CaseInsensitive) != 0) {
        return fail(QStringLiteral("The update feed does not require SHA-256."));
    }
    parsed.expectedSha256 = normalizeSha256(
        integrity.value(QStringLiteral("sha256")).toString());
    if (parsed.expectedSha256.isEmpty()) {
        return fail(QStringLiteral("The update feed contains an invalid SHA-256 value."));
    }

    const QJsonObject policy = root.value(QStringLiteral("installationPolicy")).toObject();
    if (!policy.value(QStringLiteral("requireUserApproval")).toBool() ||
        !policy.value(QStringLiteral("blockInstallWhileStreaming")).toBool() ||
        !policy.value(QStringLiteral("blockInstallWhileRecording")).toBool() ||
        !policy.value(QStringLiteral("blockAutomaticRestartWhileBusy")).toBool()) {
        return fail(QStringLiteral("The update feed does not enforce the required safe installation policy."));
    }

    const QJsonObject releaseNotes = root.value(QStringLiteral("releaseNotes")).toObject();
    parsed.summary = releaseNotes.value(QStringLiteral("summary")).toString().trimmed();
    parsed.releaseNotesUrl = QUrl(releaseNotes.value(QStringLiteral("url")).toString());
    const QJsonArray changes = releaseNotes.value(QStringLiteral("changes")).toArray();
    for (const QJsonValue& value : changes) {
        const QString change = value.toString().trimmed();
        if (!change.isEmpty()) {
            parsed.changes.append(change);
        }
    }

    *update = parsed;
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

QString UpdateManager::fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString UpdateManager::releaseNotesText(const UpdateInfo& update)
{
    QString text;
    if (!update.summary.isEmpty()) {
        text += update.summary;
    }
    if (!update.changes.isEmpty()) {
        if (!text.isEmpty()) {
            text += QStringLiteral("\n\n");
        }
        for (const QString& change : update.changes) {
            text += QStringLiteral("• %1\n").arg(change);
        }
        text = text.trimmed();
    }
    if (update.releaseNotesUrl.isValid()) {
        if (!text.isEmpty()) {
            text += QStringLiteral("\n\n");
        }
        text += QStringLiteral("Release notes: %1").arg(update.releaseNotesUrl.toString());
    }
    return text;
}

bool UpdateManager::outputBusy() const
{
    return engine_ != nullptr && (engine_->isStreaming() || engine_->isRecording());
}

void UpdateManager::handleFeedReply(QNetworkReply* reply, bool interactive)
{
    if (reply != feedReply_) {
        reply->deleteLater();
        return;
    }

    feedReply_ = nullptr;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        reportCheckFailure(
            QStringLiteral("The stable update feed could not be reached: %1")
                .arg(networkErrorText),
            interactive);
        return;
    }

    UpdateInfo update;
    QString parseError;
    if (!parseFeed(payload, &update, &parseError)) {
        reportCheckFailure(parseError, interactive);
        return;
    }

    if (compareVersions(update.version, currentVersion()) <= 0) {
        emit statusMessage(
            QStringLiteral("Vuttara Studio %1 is up to date.").arg(currentVersion()),
            7000);
        if (interactive) {
            QMessageBox::information(
                parentWindow_,
                QStringLiteral("Vuttara Studio Updates"),
                QStringLiteral("Vuttara Studio %1 is the latest available release.")
                    .arg(currentVersion()));
        }
        return;
    }

    promptForDownload(update);
}

void UpdateManager::promptForDownload(const UpdateInfo& update)
{
    const QString notes = releaseNotesText(update);
    const QString message = QStringLiteral(
        "Vuttara Studio %1 is available.\n\n%2\n\n"
        "The installer will be downloaded from the official GitHub release and "
        "verified with SHA-256 before it can run. Download it now?")
        .arg(update.version, notes.isEmpty() ? QStringLiteral("A new stable release is available.") : notes);

    const QMessageBox::StandardButton choice = QMessageBox::question(
        parentWindow_,
        QStringLiteral("Vuttara Studio Update Available"),
        message,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (choice == QMessageBox::Yes) {
        beginDownload(update);
    } else {
        emit statusMessage(
            QStringLiteral("Vuttara Studio %1 is available; download postponed.")
                .arg(update.version),
            8000);
    }
}

void UpdateManager::beginDownload(const UpdateInfo& update)
{
    const QString updateDirectory = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("updates"));
    if (!QDir().mkpath(updateDirectory)) {
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Download Failed"),
            QStringLiteral("The private update-download directory could not be created."));
        return;
    }

    completedDownloadPath_ = QDir(updateDirectory).filePath(update.fileName);
    partialDownloadPath_ = completedDownloadPath_ + QStringLiteral(".part");
    activeUpdate_ = update;

    if (QFileInfo(completedDownloadPath_).isFile() &&
        QFileInfo(completedDownloadPath_).size() == update.expectedSize &&
        fileSha256(completedDownloadPath_) == update.expectedSha256) {
        promptForInstall(update, completedDownloadPath_);
        return;
    }

    QFile::remove(partialDownloadPath_);
    downloadFile_ = new QFile(partialDownloadPath_);
    if (!downloadFile_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete downloadFile_;
        downloadFile_ = nullptr;
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Download Failed"),
            QStringLiteral("The temporary installer file could not be created."));
        return;
    }

    QNetworkRequest request(update.downloadUrl);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("VuttaraStudio/%1 Windows-x64").arg(currentVersion()));
    request.setRawHeader("Accept", "application/octet-stream");

    downloadWriteFailed_ = false;
    downloadReply_ = network_->get(request);
    progressDialog_ = new QProgressDialog(
        QStringLiteral("Downloading Vuttara Studio %1…").arg(update.version),
        QStringLiteral("Cancel"),
        0,
        100,
        parentWindow_);
    progressDialog_->setWindowTitle(QStringLiteral("Vuttara Studio Update"));
    progressDialog_->setWindowModality(Qt::WindowModal);
    progressDialog_->setAutoClose(false);
    progressDialog_->setAutoReset(false);
    progressDialog_->setMinimumDuration(0);
    progressDialog_->show();

    connect(progressDialog_, &QProgressDialog::canceled, downloadReply_, &QNetworkReply::abort);
    connect(downloadReply_, &QNetworkReply::readyRead, this, [this]() {
        if (downloadReply_ == nullptr || downloadFile_ == nullptr) {
            return;
        }
        const QByteArray bytes = downloadReply_->readAll();
        if (downloadFile_->write(bytes) != bytes.size()) {
            downloadWriteFailed_ = true;
            downloadReply_->abort();
        }
    });
    connect(
        downloadReply_,
        &QNetworkReply::downloadProgress,
        this,
        [this](qint64 received, qint64 total) {
            if (progressDialog_ == nullptr) {
                return;
            }
            if (total > 0) {
                const int percent = static_cast<int>(
                    std::clamp<qint64>((received * 100) / total, 0, 100));
                progressDialog_->setValue(percent);
            } else {
                progressDialog_->setRange(0, 0);
            }
        });
    connect(downloadReply_, &QNetworkReply::finished, this, &UpdateManager::handleDownloadFinished);

    emit statusMessage(
        QStringLiteral("Downloading Vuttara Studio %1 from GitHub…").arg(update.version),
        6000);
}

void UpdateManager::handleDownloadFinished()
{
    if (downloadReply_ == nullptr) {
        return;
    }

    if (downloadFile_ != nullptr) {
        const QByteArray remaining = downloadReply_->readAll();
        if (!remaining.isEmpty()) {
            downloadFile_->write(remaining);
        }
        downloadFile_->flush();
        downloadFile_->close();
    }

    const QNetworkReply::NetworkError networkError = downloadReply_->error();
    const QString networkErrorText = downloadReply_->errorString();

    if (downloadWriteFailed_) {
        resetDownloadState(true);
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Download Failed"),
            QStringLiteral("The installer could not be written to disk."));
        return;
    }

    if (networkError != QNetworkReply::NoError) {
        const bool canceled = networkError == QNetworkReply::OperationCanceledError;
        resetDownloadState(true);
        if (!canceled) {
            QMessageBox::critical(
                parentWindow_,
                QStringLiteral("Update Download Failed"),
                QStringLiteral("The installer download failed: %1").arg(networkErrorText));
        }
        return;
    }

    const QFileInfo partialFile(partialDownloadPath_);
    if (!partialFile.isFile() || partialFile.size() != activeUpdate_.expectedSize) {
        resetDownloadState(true);
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Verification Failed"),
            QStringLiteral("The downloaded installer size did not match the published release feed."));
        return;
    }

    const QString actualSha256 = fileSha256(partialDownloadPath_);
    if (actualSha256 != activeUpdate_.expectedSha256) {
        resetDownloadState(true);
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Verification Failed"),
            QStringLiteral("The downloaded installer failed SHA-256 verification and was deleted."));
        return;
    }

    if (progressDialog_ != nullptr) {
        progressDialog_->setValue(100);
    }

    QFile::remove(completedDownloadPath_);
    if (!QFile::rename(partialDownloadPath_, completedDownloadPath_)) {
        resetDownloadState(true);
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Verification Failed"),
            QStringLiteral("The verified installer could not be finalized."));
        return;
    }

    const UpdateInfo verifiedUpdate = activeUpdate_;
    const QString verifiedPath = completedDownloadPath_;
    resetDownloadState(false);

    emit statusMessage(
        QStringLiteral("Vuttara Studio %1 downloaded and verified.").arg(verifiedUpdate.version),
        8000);
    promptForInstall(verifiedUpdate, verifiedPath);
}

void UpdateManager::promptForInstall(
    const UpdateInfo& update,
    const QString& installerPath)
{
    if (outputBusy()) {
        QMessageBox::warning(
            parentWindow_,
            QStringLiteral("Update Ready"),
            QStringLiteral(
                "Vuttara Studio %1 was downloaded and verified, but it cannot be "
                "installed while streaming or recording. Stop all active outputs, then "
                "use Help > Check for Updates to continue.")
                .arg(update.version));
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        parentWindow_,
        QStringLiteral("Install Vuttara Studio Update"),
        QStringLiteral(
            "Vuttara Studio %1 is downloaded and verified.\n\n"
            "Install it now? Vuttara Studio will close and launch the normal interactive "
            "installer. Terms acceptance and Privacy acknowledgement will remain required.")
            .arg(update.version),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (choice == QMessageBox::Yes) {
        launchInstaller(update, installerPath);
    } else {
        emit statusMessage(
            QStringLiteral("Verified Vuttara Studio %1 installer saved for later.")
                .arg(update.version),
            8000);
    }
}

bool UpdateManager::launchInstaller(
    const UpdateInfo& update,
    const QString& installerPath)
{
    if (outputBusy()) {
        QMessageBox::warning(
            parentWindow_,
            QStringLiteral("Installation Blocked"),
            QStringLiteral("Stop streaming and recording before installing an update."));
        return false;
    }

    const QFileInfo installer(installerPath);
    if (!installer.isFile() || installer.size() != update.expectedSize ||
        fileSha256(installerPath) != update.expectedSha256) {
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Verification Failed"),
            QStringLiteral("The installer changed after verification and will not be launched."));
        return false;
    }

    if (!QProcess::startDetached(
            installerPath,
            QStringList{},
            installer.absolutePath())) {
        QMessageBox::critical(
            parentWindow_,
            QStringLiteral("Update Launch Failed"),
            QStringLiteral("The verified installer could not be started."));
        return false;
    }

    emit statusMessage(
        QStringLiteral("Launching Vuttara Studio %1 installer…").arg(update.version),
        5000);
    QTimer::singleShot(0, qApp, []() {
        QCoreApplication::quit();
    });
    return true;
}

void UpdateManager::resetDownloadState(bool removePartialFile)
{
    if (downloadReply_ != nullptr) {
        downloadReply_->deleteLater();
        downloadReply_ = nullptr;
    }
    if (downloadFile_ != nullptr) {
        if (downloadFile_->isOpen()) {
            downloadFile_->close();
        }
        delete downloadFile_;
        downloadFile_ = nullptr;
    }
    if (progressDialog_ != nullptr) {
        progressDialog_->close();
        progressDialog_->deleteLater();
        progressDialog_ = nullptr;
    }
    if (removePartialFile && !partialDownloadPath_.isEmpty()) {
        QFile::remove(partialDownloadPath_);
    }
    partialDownloadPath_.clear();
    completedDownloadPath_.clear();
    activeUpdate_ = {};
    downloadWriteFailed_ = false;
}

void UpdateManager::reportCheckFailure(const QString& message, bool interactive)
{
    emit statusMessage(QStringLiteral("Update check failed: %1").arg(message), 9000);
    if (interactive) {
        QMessageBox::warning(
            parentWindow_,
            QStringLiteral("Vuttara Studio Updates"),
            message);
    }
}

bool UpdateManager::runSelfTest(QStringList& lines)
{
    bool passed = true;

    if (compareVersions(QStringLiteral("0.0.1"), QStringLiteral("0.0.2")) >= 0 ||
        compareVersions(QStringLiteral("0.1.0"), QStringLiteral("0.0.9")) <= 0 ||
        compareVersions(QStringLiteral("0.0.1"), QStringLiteral("0.0.1")) != 0) {
        lines << QStringLiteral("FAIL: Clean-rewrite semantic-version comparison failed.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Clean-rewrite semantic-version comparison is deterministic.");
    }

    const QUrl trustedUrl(QStringLiteral(
        "https://github.com/NutterButterInAA/vuttara-studio/releases/download/v0.0.2/Vuttara-Studio-0.0.2-Setup.exe"));
    const QUrl untrustedUrl(QStringLiteral(
        "https://example.com/Vuttara-Studio-0.0.2-Setup.exe"));
    if (!isTrustedInstallerUrl(trustedUrl) || isTrustedInstallerUrl(untrustedUrl)) {
        lines << QStringLiteral("FAIL: Updater GitHub-origin policy validation failed.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Updater accepts only the official GitHub release path over HTTPS.");
    }

    const QByteArray sampleFeed = R"json({
        "schemaVersion": 1,
        "product": "Vuttara Studio",
        "productId": "com.nuttabuttainaa.vuttarastudio",
        "productLine": "clean-rewrite",
        "channel": "stable",
        "version": "0.0.2",
        "download": {
            "url": "https://github.com/NutterButterInAA/vuttara-studio/releases/download/v0.0.2/Vuttara-Studio-0.0.2-Setup.exe",
            "fileName": "Vuttara-Studio-0.0.2-Setup.exe",
            "size": 123456
        },
        "integrity": {
            "algorithm": "SHA-256",
            "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        },
        "releaseNotes": {
            "url": "https://vuttarastudio.nuttabuttainaa.com/release-notes/clean-rewrite/0.0.2/",
            "summary": "Updater validation fixture.",
            "changes": ["Verified change"]
        },
        "installationPolicy": {
            "requireUserApproval": true,
            "blockInstallWhileStreaming": true,
            "blockInstallWhileRecording": true,
            "blockAutomaticRestartWhileBusy": true
        }
    })json";

    UpdateInfo parsed;
    QString parseError;
    if (!parseFeed(sampleFeed, &parsed, &parseError) ||
        parsed.version != QStringLiteral("0.0.2") ||
        parsed.expectedSize != 123456 ||
        parsed.expectedSha256 != QStringLiteral(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")) {
        lines << QStringLiteral("FAIL: Clean-rewrite stable update feed validation failed: %1")
                     .arg(parseError);
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Clean-rewrite update feed, SHA-256, filename, and safety policy validation passed.");
    }

    QByteArray legacyFeed = sampleFeed;
    legacyFeed.replace("\"clean-rewrite\"", "\"legacy\"");
    UpdateInfo rejected;
    QString rejectionReason;
    if (parseFeed(legacyFeed, &rejected, &rejectionReason)) {
        lines << QStringLiteral("FAIL: The clean-rewrite updater accepted a legacy product-line feed.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: The clean-rewrite updater rejects legacy release feeds.");
    }

    return passed;
}
