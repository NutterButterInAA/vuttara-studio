#pragma once

#include <QString>
#include <QVector>

namespace Vuttara {

struct DisplayInfo
{
    QString monitorId;
    QString deviceName;
    QString description;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool primary = false;
};

[[nodiscard]] QVector<DisplayInfo> enumerateWindowsDisplays();

}
