#pragma once

#include <QString>
#include <QVector>

namespace Vuttara {

struct AudioDeviceInfo
{
    QString name;
    QString deviceId;
    bool defaultDevice = false;
};

QVector<AudioDeviceInfo> enumerateDesktopAudioDevices();
QVector<AudioDeviceInfo> enumerateMicrophoneDevices();

}
