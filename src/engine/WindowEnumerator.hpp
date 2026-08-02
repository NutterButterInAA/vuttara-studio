#pragma once

#include <QString>
#include <QVector>

namespace Vuttara {

struct WindowInfo
{
    QString encodedValue;
    QString title;
    QString windowClass;
    QString executable;
    QString description;
    bool minimized = false;
};

[[nodiscard]] QVector<WindowInfo> enumerateCapturableWindows();

}
