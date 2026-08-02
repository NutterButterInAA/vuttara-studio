#include "AppPaths.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace Vuttara::AppPaths {

QString applicationDataDirectory()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(path);
    return path;
}

QString logDirectory()
{
    const QString path = QDir(applicationDataDirectory()).filePath(QStringLiteral("logs"));
    QDir().mkpath(path);
    return path;
}

QString engineConfigDirectory()
{
    const QString path = QDir(applicationDataDirectory()).filePath(QStringLiteral("engine"));
    QDir().mkpath(path);
    return path;
}

QString projectStatePath()
{
    const QString path = QDir(applicationDataDirectory()).filePath(QStringLiteral("projects"));
    QDir().mkpath(path);
    return QDir(path).filePath(QStringLiteral("default-vuttara-project.json"));
}

QString createStartupLog()
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString path = QDir(logDirectory()).filePath(QStringLiteral("vuttara-studio-%1.log").arg(stamp));

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "Vuttara Studio startup\n";
        stream << "Version: " << VUTTARA_STUDIO_VERSION << "\n";
#ifdef VUTTARA_STUDIO_RELEASE_BUILD
        stream << "Mode: stable release\n";
#else
        stream << "Mode: local development\n";
#endif
        stream << "UTC: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n";
    }

    return path;
}

bool appendLogLine(const QString& logPath, const QString& message)
{
    if (logPath.isEmpty()) {
        return false;
    }

    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << "  " << message << "\n";
    return true;
}

}
