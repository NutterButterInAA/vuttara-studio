#pragma once

#include <QString>

namespace Vuttara::AppPaths {
QString applicationDataDirectory();
QString logDirectory();
QString engineConfigDirectory();
QString projectStatePath();
QString createStartupLog();
bool appendLogLine(const QString& logPath, const QString& message);
}
