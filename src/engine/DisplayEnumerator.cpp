#include "DisplayEnumerator.hpp"

#include <QtGlobal>

#include <windows.h>

namespace Vuttara {
namespace {

struct EnumerationState
{
    QVector<DisplayInfo>* displays = nullptr;
};

BOOL CALLBACK enumerateDisplay(
    HMONITOR monitor,
    HDC deviceContext,
    LPRECT monitorRectangle,
    LPARAM parameter)
{
    Q_UNUSED(deviceContext);
    Q_UNUSED(monitorRectangle);

    auto* state = reinterpret_cast<EnumerationState*>(parameter);
    if (state == nullptr || state->displays == nullptr) {
        return FALSE;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return TRUE;
    }

    DISPLAY_DEVICEW displayDevice{};
    displayDevice.cb = sizeof(displayDevice);

    QString monitorId;
    if (EnumDisplayDevicesW(
            monitorInfo.szDevice,
            0,
            &displayDevice,
            EDD_GET_DEVICE_INTERFACE_NAME)) {
        monitorId = QString::fromWCharArray(displayDevice.DeviceID);
    }

    if (monitorId.isEmpty()) {
        monitorId = QString::fromWCharArray(monitorInfo.szDevice);
    }

    const int width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    const int height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    const bool primary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
    const int displayNumber = state->displays->size() + 1;

    DisplayInfo display;
    display.monitorId = monitorId;
    display.deviceName = QString::fromWCharArray(monitorInfo.szDevice);
    display.x = monitorInfo.rcMonitor.left;
    display.y = monitorInfo.rcMonitor.top;
    display.width = width;
    display.height = height;
    display.primary = primary;
    display.description = QStringLiteral("Display %1 — %2 × %3 @ %4,%5%6")
                              .arg(displayNumber)
                              .arg(width)
                              .arg(height)
                              .arg(display.x)
                              .arg(display.y)
                              .arg(primary ? QStringLiteral(" (Primary)") : QString{});

    state->displays->append(display);
    return TRUE;
}

}

QVector<DisplayInfo> enumerateWindowsDisplays()
{
    QVector<DisplayInfo> displays;
    EnumerationState state{&displays};
    EnumDisplayMonitors(nullptr, nullptr, &enumerateDisplay, reinterpret_cast<LPARAM>(&state));
    return displays;
}

}
