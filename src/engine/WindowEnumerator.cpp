#include "WindowEnumerator.hpp"

#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <array>
#include <vector>

#include <dwmapi.h>
#include <windows.h>

namespace Vuttara {
namespace {

QString encodedWindowField(QString value)
{
    value.replace(QStringLiteral("#"), QStringLiteral("#22"));
    value.replace(QStringLiteral(":"), QStringLiteral("#3A"));
    return value;
}

QString windowText(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1U, L'\0');
    if (GetWindowTextW(window, buffer.data(), static_cast<int>(buffer.size())) <= 0) {
        return {};
    }

    return QString::fromWCharArray(buffer.data()).trimmed();
}

QString windowClass(HWND window)
{
    std::array<wchar_t, 512> buffer{};
    const int length = GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
    return length > 0 ? QString::fromWCharArray(buffer.data(), length) : QString{};
}

QString executableName(HWND window)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0 || processId == GetCurrentProcessId()) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return {};
    }

    std::vector<wchar_t> path(32768, L'\0');
    DWORD pathLength = static_cast<DWORD>(path.size());
    const BOOL success = QueryFullProcessImageNameW(process, 0, path.data(), &pathLength);
    CloseHandle(process);

    if (!success || pathLength == 0) {
        return {};
    }

    return QFileInfo(QString::fromWCharArray(path.data(), static_cast<int>(pathLength))).fileName();
}

bool isCloaked(HWND window)
{
    DWORD cloaked = 0;
    const HRESULT result = DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(result) && cloaked != 0;
}

bool isInternalWindowsExecutable(const QString& executable)
{
    static const QSet<QString> excluded = {
        QStringLiteral("applicationframehost.exe"),
        QStringLiteral("cortana.exe"),
        QStringLiteral("gamebar.exe"),
        QStringLiteral("lockapp.exe"),
        QStringLiteral("microsoft.notes.exe"),
        QStringLiteral("peopleexperiencehost.exe"),
        QStringLiteral("searchapp.exe"),
        QStringLiteral("searchui.exe"),
        QStringLiteral("shellexperiencehost.exe"),
        QStringLiteral("startmenuexperiencehost.exe"),
        QStringLiteral("systemsettings.exe"),
        QStringLiteral("tabtip.exe"),
        QStringLiteral("textinputhost.exe"),
        QStringLiteral("time.exe"),
        QStringLiteral("video.ui.exe"),
    };

    const QString normalized = executable.toLower();
    return excluded.contains(normalized) || normalized.startsWith(QStringLiteral("windowsinternal"));
}

struct EnumerationState
{
    QVector<WindowInfo>* windows = nullptr;
    QSet<QString> encodedValues;
};

BOOL CALLBACK enumerateWindow(HWND window, LPARAM parameter)
{
    auto* state = reinterpret_cast<EnumerationState*>(parameter);
    if (state == nullptr || state->windows == nullptr) {
        return FALSE;
    }

    if (!IsWindowVisible(window) || isCloaked(window)) {
        return TRUE;
    }

    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_CHILD) != 0 || (extendedStyle & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }

    const QString title = windowText(window);
    const QString className = windowClass(window);
    const QString executable = executableName(window);
    if (title.isEmpty() || className.isEmpty() || executable.isEmpty()) {
        return TRUE;
    }

    if (isInternalWindowsExecutable(executable)) {
        return TRUE;
    }

    if (executable.compare(QStringLiteral("explorer.exe"), Qt::CaseInsensitive) == 0 && title.isEmpty()) {
        return TRUE;
    }

    const QString encoded = QStringLiteral("%1:%2:%3")
                                .arg(
                                    encodedWindowField(title),
                                    encodedWindowField(className),
                                    encodedWindowField(executable));
    if (state->encodedValues.contains(encoded)) {
        return TRUE;
    }

    const bool minimized = IsIconic(window) != FALSE;

    WindowInfo info;
    info.encodedValue = encoded;
    info.title = title;
    info.windowClass = className;
    info.executable = executable;
    info.minimized = minimized;
    info.description = QStringLiteral("[%1] %2%3")
                           .arg(
                               executable,
                               title,
                               minimized ? QStringLiteral(" (Minimized — restore to preview)") : QString{});

    state->encodedValues.insert(encoded);
    state->windows->append(info);
    return TRUE;
}

}

QVector<WindowInfo> enumerateCapturableWindows()
{
    QVector<WindowInfo> windows;
    EnumerationState state{&windows, {}};
    EnumWindows(&enumerateWindow, reinterpret_cast<LPARAM>(&state));

    std::sort(
        windows.begin(),
        windows.end(),
        [](const WindowInfo& left, const WindowInfo& right) {
            if (left.minimized != right.minimized) {
                return !left.minimized;
            }
            return left.description.compare(right.description, Qt::CaseInsensitive) < 0;
        });

    return windows;
}

}
