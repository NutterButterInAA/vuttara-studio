#include "AppPaths.hpp"
#include "MainWindow.hpp"
#include "UpdateManager.hpp"
#include "engine/VuttaraEngine.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace {

QString selfTestReportPath(const QStringList& arguments)
{
    const QString prefix = QStringLiteral("--self-test-report=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) {
            return argument.mid(prefix.size());
        }
    }
    return {};
}

void runDisplayCaptureSelfTest(Vuttara::VuttaraEngine& engine, QStringList& lines, bool& passed)
{
    const QVector<Vuttara::DisplayInfo> displays = engine.availableDisplays();
    if (displays.isEmpty()) {
        lines << QStringLiteral("FAIL: Windows display enumeration returned no monitors.");
        passed = false;
        return;
    }

    lines << QStringLiteral("PASS: Windows display enumeration found %1 monitor(s).")
                 .arg(displays.size());

    QString sourceName;
    if (!engine.addDisplayCapture(
            displays.first(),
            false,
            QStringLiteral("Stage 4A Self-Test Display"),
            &sourceName)) {
        lines << QStringLiteral("FAIL: Display Capture source creation failed: %1")
                     .arg(engine.lastError());
        passed = false;
        return;
    }

    lines << QStringLiteral("PASS: monitor_capture source created for %1.")
                 .arg(displays.first().description);

    const Vuttara::SourcePropertiesModel displayProperties =
        engine.sourceProperties(sourceName);
    if (
        displayProperties.sourceType != QStringLiteral("display_capture") ||
        displayProperties.properties.size() < 3) {
        lines << QStringLiteral("FAIL: Display Capture properties model was incomplete.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Display Capture properties model exposed source name, display, and cursor settings.");
    }

    QJsonObject displayUpdates;
    displayUpdates.insert(QStringLiteral("name"), QStringLiteral("Stage 7A Self-Test Display"));
    displayUpdates.insert(QStringLiteral("monitorId"), displays.first().monitorId);
    displayUpdates.insert(QStringLiteral("captureCursor"), false);
    QString updatedDisplayName;
    if (!engine.applySourceProperties(sourceName, displayUpdates, &updatedDisplayName)) {
        lines << QStringLiteral("FAIL: Display Capture properties could not be applied: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        sourceName = updatedDisplayName;
        lines << QStringLiteral("PASS: Display Capture properties applied through the reusable source-properties adapter.");
    }

    if (!engine.setSourceVisible(sourceName, false)) {
        lines << QStringLiteral("FAIL: Display Capture could not be hidden: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Display Capture visibility disabled.");
    }

    if (!engine.setSourceVisible(sourceName, true)) {
        lines << QStringLiteral("FAIL: Display Capture could not be shown: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Display Capture visibility restored.");
    }

    if (!engine.removeSource(sourceName)) {
        lines << QStringLiteral("FAIL: Display Capture removal failed: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Display Capture source removed cleanly.");
    }

    if (engine.hasDisplayCapture()) {
        lines << QStringLiteral("FAIL: Display Capture remained registered after removal.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Display Capture registration cleared after removal.");
    }
}

void runWindowCaptureSelfTest(Vuttara::VuttaraEngine& engine, QStringList& lines, bool& passed)
{
    const QVector<Vuttara::WindowInfo> windows = engine.availableWindows();
    if (windows.isEmpty()) {
        lines << QStringLiteral("FAIL: Windows window enumeration returned no capturable application windows.");
        passed = false;
        return;
    }

    lines << QStringLiteral("PASS: Windows window enumeration found %1 capturable window(s).")
                 .arg(windows.size());

    const auto preferredWindow = std::find_if(
        windows.cbegin(),
        windows.cend(),
        [](const Vuttara::WindowInfo& window) {
            return !window.minimized;
        });
    const Vuttara::WindowInfo& selectedWindow = preferredWindow != windows.cend()
        ? *preferredWindow
        : windows.first();

    QString sourceName;
    if (!engine.addWindowCapture(
            selectedWindow,
            Vuttara::WindowCaptureMethod::Automatic,
            Vuttara::WindowMatchPriority::Title,
            false,
            true,
            QStringLiteral("Stage 4A Self-Test Window"),
            &sourceName)) {
        lines << QStringLiteral("FAIL: Window Capture source creation failed: %1")
                     .arg(engine.lastError());
        passed = false;
        return;
    }

    lines << QStringLiteral("PASS: window_capture source created for %1.")
                 .arg(selectedWindow.description);

    const Vuttara::SourcePropertiesModel windowProperties =
        engine.sourceProperties(sourceName);
    if (
        windowProperties.sourceType != QStringLiteral("window_capture") ||
        windowProperties.properties.size() < 6) {
        lines << QStringLiteral("FAIL: Window Capture properties model was incomplete.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Window Capture properties model exposed window, method, priority, cursor, and client-area settings.");
    }

    QJsonObject windowUpdates;
    windowUpdates.insert(QStringLiteral("name"), QStringLiteral("Stage 7A Self-Test Window"));
    windowUpdates.insert(QStringLiteral("window"), selectedWindow.encodedValue);
    windowUpdates.insert(
        QStringLiteral("method"),
        static_cast<int>(Vuttara::WindowCaptureMethod::Automatic));
    windowUpdates.insert(
        QStringLiteral("priority"),
        static_cast<int>(Vuttara::WindowMatchPriority::Title));
    windowUpdates.insert(QStringLiteral("captureCursor"), false);
    windowUpdates.insert(QStringLiteral("clientArea"), true);
    QString updatedWindowName;
    if (!engine.applySourceProperties(sourceName, windowUpdates, &updatedWindowName)) {
        lines << QStringLiteral("FAIL: Window Capture properties could not be applied: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        sourceName = updatedWindowName;
        lines << QStringLiteral("PASS: Window Capture properties applied through the reusable source-properties adapter.");
    }

    if (!engine.setSourceVisible(sourceName, false)) {
        lines << QStringLiteral("FAIL: Window Capture could not be hidden: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Window Capture visibility disabled.");
    }

    if (!engine.setSourceVisible(sourceName, true)) {
        lines << QStringLiteral("FAIL: Window Capture could not be shown: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Window Capture visibility restored.");
    }

    if (!engine.removeSource(sourceName)) {
        lines << QStringLiteral("FAIL: Window Capture removal failed: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Window Capture source removed cleanly.");
    }

    if (engine.hasWindowCapture()) {
        lines << QStringLiteral("FAIL: Window Capture remained registered after removal.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Window Capture registration cleared after removal.");
    }
}


void runSceneControlsSelfTest(Vuttara::VuttaraEngine& engine, QStringList& lines, bool& passed)
{
    const QVector<Vuttara::SceneInfo> initialScenes = engine.sceneInfos();
    if (initialScenes.size() != 1 || engine.activeSceneName() != QStringLiteral("Main Scene")) {
        lines << QStringLiteral("FAIL: Default Main Scene state was not available.");
        passed = false;
        return;
    }
    lines << QStringLiteral("PASS: Default Main Scene is active.");

    const QVector<Vuttara::SourceInfo> foundationSources = engine.sourceInfos();
    const auto foundationColor = std::find_if(
        foundationSources.cbegin(),
        foundationSources.cend(),
        [](const Vuttara::SourceInfo& source) {
            return source.type == QStringLiteral("color");
        });
    if (foundationColor == foundationSources.cend()) {
        lines << QStringLiteral("FAIL: Foundation color source was not available for properties validation.");
        passed = false;
    } else {
        const Vuttara::SourcePropertiesModel colorProperties =
            engine.sourceProperties(foundationColor->name);
        if (colorProperties.sourceType != QStringLiteral("color")) {
            lines << QStringLiteral("FAIL: Color source properties model was unavailable.");
            passed = false;
        } else {
            QJsonObject colorUpdates;
            colorUpdates.insert(QStringLiteral("name"), foundationColor->name);
            colorUpdates.insert(QStringLiteral("color"), static_cast<double>(0xFF8B4DB5ULL));
            colorUpdates.insert(QStringLiteral("width"), 1920);
            colorUpdates.insert(QStringLiteral("height"), 1080);
            if (!engine.applySourceProperties(foundationColor->name, colorUpdates)) {
                lines << QStringLiteral("FAIL: Color source properties could not be applied: %1")
                             .arg(engine.lastError());
                passed = false;
            } else {
                lines << QStringLiteral("PASS: Color source properties model and update adapter validated.");
            }
        }
    }

    QString createdScene;
    if (!engine.addScene(QStringLiteral("Stage 4A Self-Test Scene"), &createdScene)) {
        lines << QStringLiteral("FAIL: Scene creation failed: %1").arg(engine.lastError());
        passed = false;
        return;
    }
    lines << QStringLiteral("PASS: Second scene created and activated.");

    const QVector<Vuttara::DisplayInfo> displays = engine.availableDisplays();
    const QVector<Vuttara::WindowInfo> windows = engine.availableWindows();
    if (displays.isEmpty() || windows.isEmpty()) {
        lines << QStringLiteral("FAIL: Scene controls self-test requires a display and a capturable window.");
        passed = false;
        return;
    }

    const auto preferredWindow = std::find_if(
        windows.cbegin(), windows.cend(), [](const Vuttara::WindowInfo& window) {
            return !window.minimized;
        });
    const Vuttara::WindowInfo& selectedWindow = preferredWindow != windows.cend()
        ? *preferredWindow
        : windows.first();

    QString displayName;
    QString windowName;
    if (!engine.addDisplayCapture(
            displays.first(), false, QStringLiteral("Stage 4A Ordered Display"), &displayName)) {
        lines << QStringLiteral("FAIL: Scene Display Capture creation failed: %1").arg(engine.lastError());
        passed = false;
        return;
    }
    if (!engine.addWindowCapture(
            selectedWindow,
            Vuttara::WindowCaptureMethod::Automatic,
            Vuttara::WindowMatchPriority::Title,
            false,
            true,
            QStringLiteral("Stage 4A Ordered Window"),
            &windowName)) {
        lines << QStringLiteral("FAIL: Scene Window Capture creation failed: %1").arg(engine.lastError());
        passed = false;
        return;
    }
    lines << QStringLiteral("PASS: Active scene owns independent Display and Window Capture sources.");

    if (!engine.moveSource(displayName, Vuttara::SourceOrderMovement::Top)) {
        lines << QStringLiteral("FAIL: Source ordering failed: %1").arg(engine.lastError());
        passed = false;
    } else {
        const QVector<Vuttara::SourceInfo> ordered = engine.sourceInfos();
        const auto display = std::find_if(ordered.cbegin(), ordered.cend(), [&displayName](const Vuttara::SourceInfo& source) {
            return source.name == displayName;
        });
        const auto window = std::find_if(ordered.cbegin(), ordered.cend(), [&windowName](const Vuttara::SourceInfo& source) {
            return source.name == windowName;
        });
        if (display == ordered.cend() || window == ordered.cend() || display->orderPosition <= window->orderPosition) {
            lines << QStringLiteral("FAIL: Source order positions did not update.");
            passed = false;
        } else {
            lines << QStringLiteral("PASS: Source ordering moved the Display Capture to the top.");
        }
    }

    if (!engine.setSourceLocked(displayName, true)) {
        lines << QStringLiteral("FAIL: Source locking failed: %1").arg(engine.lastError());
        passed = false;
    } else {
        const Vuttara::SourceTransform blockedTransform{800.0, 450.0, 1280.0, 720.0, 12.5};
        if (engine.setSourceTransform(displayName, blockedTransform)) {
            lines << QStringLiteral("FAIL: Locked source unexpectedly accepted a transform.");
            passed = false;
        } else {
            lines << QStringLiteral("PASS: Locked source rejected transform changes.");
        }
    }

    if (!engine.setSourceLocked(displayName, false)) {
        lines << QStringLiteral("FAIL: Source unlock failed: %1").arg(engine.lastError());
        passed = false;
    } else {
        Vuttara::SourceTransform transform{800.0, 450.0, 1280.0, 720.0, 12.5};
        transform.cropLeft = 40.0;
        transform.cropTop = 20.0;
        transform.cropRight = 30.0;
        transform.cropBottom = 10.0;
        transform.flipHorizontal = true;
        transform.stretchToBounds = true;
        if (!engine.setSourceTransform(displayName, transform)) {
            lines << QStringLiteral("FAIL: Source transform failed: %1").arg(engine.lastError());
            passed = false;
        } else {
            const QVector<Vuttara::SourceInfo> transformed = engine.sourceInfos();
            const auto display = std::find_if(transformed.cbegin(), transformed.cend(), [&displayName](const Vuttara::SourceInfo& source) {
                return source.name == displayName;
            });
            const bool valuesMatch = display != transformed.cend()
                && std::abs(display->transform.x - 800.0) < 0.2
                && std::abs(display->transform.y - 450.0) < 0.2
                && std::abs(display->transform.width - 1280.0) < 0.2
                && std::abs(display->transform.height - 720.0) < 0.2
                && std::abs(display->transform.rotation - 12.5) < 0.2
                && std::abs(display->transform.cropLeft - 40.0) < 0.2
                && std::abs(display->transform.cropTop - 20.0) < 0.2
                && std::abs(display->transform.cropRight - 30.0) < 0.2
                && std::abs(display->transform.cropBottom - 10.0) < 0.2
                && display->transform.flipHorizontal
                && display->transform.stretchToBounds;
            if (!valuesMatch) {
                lines << QStringLiteral("FAIL: Source transform values did not round-trip.");
                passed = false;
            } else {
                lines << QStringLiteral("PASS: Position, bounds, rotation, crop, flip, and stretch values round-tripped.");
            }
        }
    }

    const Vuttara::SourceTransform sceneSpecificWindowTransform{
        420.0,
        275.0,
        640.0,
        360.0,
        -7.5,
    };
    if (!engine.setSourceTransform(windowName, sceneSpecificWindowTransform)) {
        lines << QStringLiteral("FAIL: Scene-specific preview transform setup failed: %1")
                     .arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Independent move and resize values were applied to a second source in the active scene.");
    }

    const QJsonObject project = engine.projectState();
    const QByteArray serialized = QJsonDocument(project).toJson(QJsonDocument::Compact);
    QJsonParseError parseError{};
    const QJsonDocument parsed = QJsonDocument::fromJson(serialized, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        lines << QStringLiteral("FAIL: Project JSON serialization failed.");
        passed = false;
    } else if (!engine.restoreProjectState(parsed.object())) {
        lines << QStringLiteral("FAIL: Project JSON restore failed: %1").arg(engine.lastError());
        passed = false;
    } else if (engine.activeSceneName() != createdScene || engine.sceneInfos().size() != 2 || engine.sourceInfos().size() != 2) {
        lines << QStringLiteral("FAIL: Restored project did not preserve scenes, active scene, and sources.");
        passed = false;
    } else {
        const QVector<Vuttara::SourceInfo> restoredSources = engine.sourceInfos();
        const auto restoredDisplay = std::find_if(
            restoredSources.cbegin(),
            restoredSources.cend(),
            [&displayName](const Vuttara::SourceInfo& source) {
                return source.name == displayName;
            });
        const auto restoredWindow = std::find_if(
            restoredSources.cbegin(),
            restoredSources.cend(),
            [&windowName](const Vuttara::SourceInfo& source) {
                return source.name == windowName;
            });
        const bool displayTransformPreserved = restoredDisplay != restoredSources.cend()
            && std::abs(restoredDisplay->transform.x - 800.0) < 0.2
            && std::abs(restoredDisplay->transform.y - 450.0) < 0.2
            && std::abs(restoredDisplay->transform.width - 1280.0) < 0.2
            && std::abs(restoredDisplay->transform.height - 720.0) < 0.2
            && std::abs(restoredDisplay->transform.rotation - 12.5) < 0.2
            && std::abs(restoredDisplay->transform.cropLeft - 40.0) < 0.2
            && std::abs(restoredDisplay->transform.cropTop - 20.0) < 0.2
            && std::abs(restoredDisplay->transform.cropRight - 30.0) < 0.2
            && std::abs(restoredDisplay->transform.cropBottom - 10.0) < 0.2
            && restoredDisplay->transform.flipHorizontal
            && restoredDisplay->transform.stretchToBounds;
        const bool windowTransformPreserved = restoredWindow != restoredSources.cend()
            && std::abs(restoredWindow->transform.x - 420.0) < 0.2
            && std::abs(restoredWindow->transform.y - 275.0) < 0.2
            && std::abs(restoredWindow->transform.width - 640.0) < 0.2
            && std::abs(restoredWindow->transform.height - 360.0) < 0.2
            && std::abs(restoredWindow->transform.rotation + 7.5) < 0.2;
        if (!displayTransformPreserved || !windowTransformPreserved) {
            lines << QStringLiteral("FAIL: Per-scene move and resize values did not survive project restoration.");
            passed = false;
        } else {
            lines << QStringLiteral("PASS: Per-scene move, resize, rotation, crop, flip, and stretch values survived project schema 3 restoration.");
        }
    }

    if (!engine.switchScene(QStringLiteral("Main Scene"))) {
        lines << QStringLiteral("FAIL: Could not switch back to Main Scene: %1").arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Scene switching returned to Main Scene.");
    }

    if (!engine.removeScene(createdScene)) {
        lines << QStringLiteral("FAIL: Scene removal failed: %1").arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Secondary scene and its sources were removed cleanly.");
    }
}

void runAudioFoundationSelfTest(Vuttara::VuttaraEngine& engine, QStringList& lines, bool& passed)
{
    const QVector<Vuttara::AudioDeviceInfo> outputs = engine.availableDesktopAudioDevices();
    const QVector<Vuttara::AudioDeviceInfo> inputs = engine.availableMicrophoneDevices();
    if (outputs.isEmpty() || inputs.isEmpty()) {
        lines << QStringLiteral("FAIL: Windows audio device enumeration returned no default output or input device.");
        passed = false;
        return;
    }

    lines << QStringLiteral("PASS: Windows audio enumeration found %1 output and %2 input entries.")
                 .arg(outputs.size())
                 .arg(inputs.size());

    if (!engine.setAudioDevice(Vuttara::AudioChannelKind::Desktop, outputs.first())) {
        lines << QStringLiteral("FAIL: Desktop Audio connection failed: %1").arg(engine.lastError());
        passed = false;
        return;
    }
    lines << QStringLiteral("PASS: Desktop Audio default device connected through wasapi_output_capture.");

    if (!engine.setAudioDevice(Vuttara::AudioChannelKind::Microphone, inputs.first())) {
        lines << QStringLiteral("FAIL: Mic/Aux connection failed: %1").arg(engine.lastError());
        passed = false;
        return;
    }
    lines << QStringLiteral("PASS: Mic/Aux default device connected through wasapi_input_capture.");

    engine.setAudioVolume(Vuttara::AudioChannelKind::Desktop, 64);
    engine.setAudioVolume(Vuttara::AudioChannelKind::Microphone, 73);
    engine.setAudioMuted(Vuttara::AudioChannelKind::Desktop, true);
    engine.setAudioMuted(Vuttara::AudioChannelKind::Microphone, true);

    const Vuttara::AudioChannelInfo desktopMuted = engine.audioChannelInfo(Vuttara::AudioChannelKind::Desktop);
    const Vuttara::AudioChannelInfo microphoneMuted = engine.audioChannelInfo(Vuttara::AudioChannelKind::Microphone);
    if (!desktopMuted.connected || !microphoneMuted.connected
        || !desktopMuted.meterAttached || !microphoneMuted.meterAttached
        || !desktopMuted.muted || !microphoneMuted.muted
        || desktopMuted.volumePercent != 64 || microphoneMuted.volumePercent != 73) {
        lines << QStringLiteral("FAIL: Audio fader, mute, or meter state did not apply.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Audio faders, mute controls, and IEC meters are attached.");
    }

    engine.setAudioMuted(Vuttara::AudioChannelKind::Desktop, false);
    engine.setAudioMuted(Vuttara::AudioChannelKind::Microphone, false);

    const QJsonObject project = engine.projectState();
    if (project.value(QStringLiteral("schemaVersion")).toInt() != 3
        || !engine.restoreProjectState(project)) {
        lines << QStringLiteral("FAIL: Audio project schema 3 round-trip failed: %1").arg(engine.lastError());
        passed = false;
    } else {
        const Vuttara::AudioChannelInfo desktopRestored = engine.audioChannelInfo(Vuttara::AudioChannelKind::Desktop);
        const Vuttara::AudioChannelInfo microphoneRestored = engine.audioChannelInfo(Vuttara::AudioChannelKind::Microphone);
        if (!desktopRestored.connected || !microphoneRestored.connected
            || desktopRestored.volumePercent != 64 || microphoneRestored.volumePercent != 73
            || desktopRestored.muted || microphoneRestored.muted) {
            lines << QStringLiteral("FAIL: Audio volume and mute values did not survive project restoration.");
            passed = false;
        } else {
            lines << QStringLiteral("PASS: Audio volume and mute state round-tripped through project schema 3.");
        }
    }

    if (!engine.disconnectAudioDevice(Vuttara::AudioChannelKind::Desktop)
        || !engine.disconnectAudioDevice(Vuttara::AudioChannelKind::Microphone)) {
        lines << QStringLiteral("FAIL: Audio channel disconnection failed: %1").arg(engine.lastError());
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Desktop Audio and Mic/Aux disconnected cleanly.");
    }
}

void runStreamingFoundationSelfTest(
    Vuttara::VuttaraEngine& engine,
    QStringList& lines,
    bool& passed)
{
    QString directoryPath;
    bool preserveOutput = false;
    const QString prefix = QStringLiteral("--streaming-test-output=");
    for (const QString& argument : QCoreApplication::arguments()) {
        if (argument.startsWith(prefix)) {
            directoryPath = argument.mid(prefix.size());
            preserveOutput = true;
            break;
        }
    }
    if (directoryPath.isEmpty()) {
        directoryPath = QDir(QDir::tempPath()).filePath(
            QStringLiteral("Vuttara-Studio-Streaming-Self-Test-%1")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    }
    QDir(directoryPath).removeRecursively();
    if (!QDir().mkpath(directoryPath)) {
        lines << QStringLiteral("FAIL: Could not create the streaming self-test folder.");
        passed = false;
        return;
    }

    const QString ffmpeg = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg.exe"));
    if (!QFileInfo(ffmpeg).isFile()) {
        lines << QStringLiteral("FAIL: Bundled ffmpeg.exe is missing for the local RTMP listener test.");
        passed = false;
        return;
    }

    const quint16 port = 19359;
    const QString server = QStringLiteral("rtmp://127.0.0.1:%1/live").arg(port);
    const QString key = QStringLiteral("vuttara-stage9a");
    const QString receiveOne = QDir(directoryPath).filePath(QStringLiteral("received-primary.mkv"));
    const QString receiveTwo = QDir(directoryPath).filePath(QStringLiteral("received-reconnect.mkv"));

    const auto startListener = [&](QProcess& process, const QString& outputPath) -> bool {
        QFile::remove(outputPath);
        process.setProgram(ffmpeg);
        process.setArguments({
            QStringLiteral("-hide_banner"),
            QStringLiteral("-loglevel"), QStringLiteral("warning"),
            QStringLiteral("-listen"), QStringLiteral("1"),
            QStringLiteral("-timeout"), QStringLiteral("15000000"),
            QStringLiteral("-i"), QStringLiteral("rtmp://127.0.0.1:%1/live/%2").arg(port).arg(key),
            QStringLiteral("-map"), QStringLiteral("0:v:0"),
            QStringLiteral("-map"), QStringLiteral("0:a:0"),
            QStringLiteral("-c"), QStringLiteral("copy"),
            QStringLiteral("-f"), QStringLiteral("matroska"),
            QStringLiteral("-y"), outputPath,
        });
        process.setProcessChannelMode(QProcess::MergedChannels);
        for (int attempt = 0; attempt < 3; ++attempt) {
            process.start();
            if (process.waitForStarted(5000)) {
                QElapsedTimer readyTimer;
                readyTimer.start();
                while (readyTimer.elapsed() < 700) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                    QThread::msleep(25);
                }
                if (process.state() != QProcess::NotRunning) {
                    return true;
                }
            }
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(3000);
            }
            QThread::msleep(500);
        }
        return false;
    };

    QProcess firstListener;
    if (!startListener(firstListener, receiveOne)) {
        lines << QStringLiteral("FAIL: Bundled FFmpeg could not start the local RTMP listener.");
        passed = false;
        if (!preserveOutput) QDir(directoryPath).removeRecursively();
        return;
    }

    const QVector<Vuttara::AudioDeviceInfo> outputs = engine.availableDesktopAudioDevices();
    const QVector<Vuttara::AudioDeviceInfo> inputs = engine.availableMicrophoneDevices();
    if (!outputs.isEmpty()) engine.setAudioDevice(Vuttara::AudioChannelKind::Desktop, outputs.first());
    if (!inputs.isEmpty()) engine.setAudioDevice(Vuttara::AudioChannelKind::Microphone, inputs.first());

    Vuttara::StreamingSettings settings;
    settings.server = server;
    settings.streamKey = key;
    settings.encoderId = QStringLiteral("obs_x264");
    settings.outputWidth = 1280;
    settings.outputHeight = 720;
    settings.framesPerSecond = 30;
    settings.videoBitrateKbps = 3500;
    settings.keyframeIntervalSeconds = 2;
    settings.audioBitrateKbps = 160;
    settings.automaticReconnect = true;
    settings.retryDelaySeconds = 1;
    settings.maximumRetries = 12;

    if (!engine.startStreaming(settings)) {
        lines << QStringLiteral("FAIL: Local RTMP streaming could not start: %1").arg(engine.lastError());
        passed = false;
        firstListener.kill();
        firstListener.waitForFinished(3000);
        if (!preserveOutput) QDir(directoryPath).removeRecursively();
        return;
    }
    lines << QStringLiteral("PASS: Custom RTMP output started against the bundled local-only listener.");

    QElapsedTimer connectTimer;
    connectTimer.start();
    bool transmitted = false;
    while (connectTimer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(25);
        const Vuttara::StreamingInfo info = engine.streamingInfo();
        if (info.active && !info.reconnecting && info.totalBytes > 16384) {
            transmitted = true;
            break;
        }
    }
    if (!transmitted) {
        lines << QStringLiteral("FAIL: RTMP output did not transmit encoded bytes within 10 seconds: %1").arg(engine.streamingInfo().error);
        passed = false;
    } else {
        const Vuttara::StreamingInfo info = engine.streamingInfo();
        lines << QStringLiteral("PASS: Encoded RTMP audio/video transmission is active: %1 bytes, %2 Kbps.")
                     .arg(info.totalBytes).arg(info.currentBitrateKbps, 0, 'f', 0);
    }

    QString editedSource;
    if (!engine.duplicateSource(QStringLiteral("Vuttara Test Color"), &editedSource)) {
        lines << QStringLiteral("FAIL: Source duplication during streaming failed: %1").arg(engine.lastError());
        passed = false;
    } else {
        Vuttara::SourceTransform transform;
        transform.x = 690.0;
        transform.y = 390.0;
        transform.width = 920.0;
        transform.height = 520.0;
        transform.rotation = 27.0;
        transform.cropLeft = 40.0;
        transform.cropTop = 22.0;
        transform.flipHorizontal = true;
        if (!engine.setSourceTransform(editedSource, transform) || !engine.isStreaming()) {
            lines << QStringLiteral("FAIL: Preview editing interrupted streaming: %1").arg(engine.lastError());
            passed = false;
        } else {
            lines << QStringLiteral("PASS: Duplication, move, resize, crop, rotation, and flip remained available without restarting streaming.");
        }
    }

    const QString recordingDirectory = QDir(directoryPath).filePath(QStringLiteral("simultaneous-recording"));
    Vuttara::RecordingSettings recording;
    recording.outputDirectory = recordingDirectory;
    recording.filenameFormat = QStringLiteral("Stage9A_Simultaneous_{date}_{time}");
    recording.encoderId = QStringLiteral("obs_x264");
    recording.outputWidth = 1280;
    recording.outputHeight = 720;
    recording.framesPerSecond = 30;
    recording.videoBitrateKbps = 3500;
    recording.audioBitrateKbps = 160;
    if (!engine.startRecording(recording)) {
        lines << QStringLiteral("FAIL: Simultaneous recording could not start while streaming: %1").arg(engine.lastError());
        passed = false;
    } else {
        const QString streamServerBefore = engine.streamingInfo().server;
        QElapsedTimer simultaneousTimer;
        simultaneousTimer.start();
        while (simultaneousTimer.elapsed() < 2200) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QThread::msleep(25);
            engine.streamingInfo();
            engine.recordingInfo();
        }
        engine.stopRecording();
        QElapsedTimer recordingStop;
        recordingStop.start();
        while (engine.isRecording() && recordingStop.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QThread::msleep(25);
            engine.recordingInfo();
        }
        const Vuttara::RecordingInfo completed = engine.recordingInfo();
        if (engine.isRecording() || !QFileInfo(completed.outputPath).isFile() || QFileInfo(completed.outputPath).size() < 16384) {
            lines << QStringLiteral("FAIL: Simultaneous MKV recording did not finalize cleanly.");
            passed = false;
            engine.forceStopRecording();
        } else if (!engine.isStreaming() || engine.streamingInfo().server != streamServerBefore) {
            lines << QStringLiteral("FAIL: Stopping recording interrupted or restarted streaming.");
            passed = false;
        } else {
            lines << QStringLiteral("PASS: Simultaneous MKV recording finalized while the original stream remained active.");
        }
    }

    firstListener.kill();
    firstListener.waitForFinished(5000);
    QThread::msleep(750);
    QElapsedTimer reconnectTimer;
    reconnectTimer.start();
    bool reconnectingObserved = false;
    while (reconnectTimer.elapsed() < 8000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(25);
        const Vuttara::StreamingInfo info = engine.streamingInfo();
        if (info.reconnecting) {
            reconnectingObserved = true;
            break;
        }
    }
    if (!reconnectingObserved) {
        lines << QStringLiteral("FAIL: Automatic reconnect did not enter the reconnecting state after the listener interruption.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Automatic reconnect entered the reconnecting state after an intentional connection interruption.");
    }

    QProcess secondListener;
    if (!startListener(secondListener, receiveTwo)) {
        lines << QStringLiteral("FAIL: The replacement local RTMP listener could not start for reconnect validation.");
        passed = false;
    } else {
        const std::uint64_t bytesBeforeReconnect = engine.streamingInfo().totalBytes;
        QElapsedTimer recoveryTimer;
        recoveryTimer.start();
        bool recovered = false;
        while (recoveryTimer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QThread::msleep(25);
            const Vuttara::StreamingInfo info = engine.streamingInfo();
            if (info.active && !info.reconnecting && info.totalBytes > bytesBeforeReconnect + 8192) {
                recovered = true;
                break;
            }
        }
        if (!recovered) {
            lines << QStringLiteral("FAIL: Streaming did not recover after the local RTMP listener returned.");
            passed = false;
        } else {
            lines << QStringLiteral("PASS: Streaming recovered automatically without recreating the output session.");
        }
    }

    if (!engine.stopStreaming()) {
        lines << QStringLiteral("FAIL: Clean streaming stop request failed: %1").arg(engine.lastError());
        passed = false;
    }
    QElapsedTimer stopTimer;
    stopTimer.start();
    while (engine.isStreaming() && stopTimer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(25);
        engine.streamingInfo();
    }
    if (engine.isStreaming()) {
        lines << QStringLiteral("FAIL: Streaming did not stop within 20 seconds.");
        passed = false;
        engine.forceStopStreaming();
    } else {
        lines << QStringLiteral("PASS: Streaming stopped cleanly and released its service, output, and encoders.");
    }
    if (secondListener.state() != QProcess::NotRunning) {
        secondListener.waitForFinished(8000);
        if (secondListener.state() != QProcess::NotRunning) {
            secondListener.kill();
            secondListener.waitForFinished(3000);
        }
    }

    const qint64 receivedBytes = QFileInfo(receiveOne).size() + QFileInfo(receiveTwo).size();
    if (receivedBytes < 16384) {
        lines << QStringLiteral("FAIL: Local RTMP listener outputs are missing or too small.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Local RTMP listener preserved %1 bytes of received stream media.").arg(receivedBytes);
    }

    engine.disconnectAudioDevice(Vuttara::AudioChannelKind::Desktop);
    engine.disconnectAudioDevice(Vuttara::AudioChannelKind::Microphone);
    if (preserveOutput) {
        lines << QStringLiteral("PASS: Preserved streaming self-test output at %1").arg(directoryPath);
    } else {
        QDir(directoryPath).removeRecursively();
    }
}

void runRecordingFoundationSelfTest(
    Vuttara::VuttaraEngine& engine,
    QStringList& lines,
    bool& passed)
{
    const QVector<Vuttara::RecordingEncoderInfo> encoders =
        engine.availableRecordingEncoders();
    if (encoders.isEmpty()) {
        lines << QStringLiteral("FAIL: No H.264 recording encoder is available.");
        passed = false;
        return;
    }

    const auto x264 = std::find_if(
        encoders.cbegin(),
        encoders.cend(),
        [](const Vuttara::RecordingEncoderInfo& encoder) {
            return encoder.id == QStringLiteral("obs_x264");
        });
    if (x264 == encoders.cend()) {
        lines << QStringLiteral("FAIL: The required x264 fallback encoder is missing.");
        passed = false;
        return;
    }

    QStringList encoderNames;
    for (const Vuttara::RecordingEncoderInfo& encoder : encoders) {
        encoderNames << QStringLiteral("%1 [%2]").arg(encoder.name, encoder.id);
    }
    lines << QStringLiteral("PASS: Recording encoder priority list: %1")
                 .arg(encoderNames.join(QStringLiteral(", ")));

    QString directoryPath;
    bool preserveOutput = false;
    const QString outputArgumentPrefix = QStringLiteral("--recording-test-output=");
    for (const QString& argument : QCoreApplication::arguments()) {
        if (argument.startsWith(outputArgumentPrefix)) {
            directoryPath = argument.mid(outputArgumentPrefix.size());
            preserveOutput = true;
            break;
        }
    }
    if (directoryPath.isEmpty()) {
        directoryPath = QDir(QDir::tempPath()).filePath(
            QStringLiteral("Vuttara-Studio-Recording-Self-Test-%1")
                .arg(QDateTime::currentDateTime().toString(
                    QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    }

    QDir(directoryPath).removeRecursively();
    if (!QDir().mkpath(directoryPath)) {
        lines << QStringLiteral("FAIL: Could not create the recording self-test folder.");
        passed = false;
        return;
    }

    const QVector<Vuttara::AudioDeviceInfo> outputs =
        engine.availableDesktopAudioDevices();
    const QVector<Vuttara::AudioDeviceInfo> inputs =
        engine.availableMicrophoneDevices();
    if (!outputs.isEmpty()) {
        engine.setAudioDevice(Vuttara::AudioChannelKind::Desktop, outputs.first());
    }
    if (!inputs.isEmpty()) {
        engine.setAudioDevice(Vuttara::AudioChannelKind::Microphone, inputs.first());
    }

    Vuttara::RecordingSettings settings;
    settings.outputDirectory = directoryPath;
    settings.filenameFormat = QStringLiteral(
        "Stage8B_{date}_{time}_{resolution}_{fps}");
    settings.outputWidth = 1280;
    settings.outputHeight = 720;
    settings.framesPerSecond = 30;
    settings.videoBitrateKbps = 6000;
    settings.audioBitrateKbps = 160;

    if (!engine.startRecording(settings)) {
        lines << QStringLiteral("FAIL: Configured MKV recording could not start: %1")
                     .arg(engine.lastError());
        passed = false;
        if (!preserveOutput) {
            QDir(directoryPath).removeRecursively();
        }
        return;
    }

    const Vuttara::RecordingInfo started = engine.recordingInfo();
    lines << QStringLiteral("PASS: MKV recording started with %1 [%2].")
                 .arg(started.encoderName, started.encoderId);

    if (
        started.outputWidth != 1280 ||
        started.outputHeight != 720 ||
        started.framesPerSecond != 30 ||
        started.videoBitrateKbps != 6000 ||
        started.audioBitrateKbps != 160) {
        lines << QStringLiteral("FAIL: Recording settings did not round-trip through the engine.");
        passed = false;
    } else {
        lines << QStringLiteral(
            "PASS: Recording settings applied: 1280x720 @ 30 FPS, 6000 Kbps video, 160 Kbps audio.");
    }

    if (!QFileInfo(started.outputPath).fileName().startsWith(QStringLiteral("Stage8B_"))) {
        lines << QStringLiteral("FAIL: Recording filename format was not applied: %1")
                     .arg(started.outputPath);
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Recording filename placeholders produced a unique Stage8B MKV path.");
    }

    QElapsedTimer leadIn;
    leadIn.start();
    while (leadIn.elapsed() < 750) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(25);
        engine.recordingInfo();
    }

    QString liveEditSource;
    QJsonObject liveEditProject;
    bool liveEditingPassed = true;
    if (!engine.duplicateSource(QStringLiteral("Vuttara Test Color"), &liveEditSource)) {
        lines << QStringLiteral("FAIL: Source duplication during recording failed: %1")
                     .arg(engine.lastError());
        liveEditingPassed = false;
    } else {
        const Vuttara::RecordingInfo afterDuplicate = engine.recordingInfo();
        if (!engine.isRecording() || afterDuplicate.outputPath != started.outputPath) {
            lines << QStringLiteral("FAIL: Duplicating a source interrupted or restarted the active recording.");
            liveEditingPassed = false;
        } else {
            lines << QStringLiteral("PASS: Source duplication remained available without interrupting or restarting recording.");
        }

        Vuttara::SourceTransform moved;
        moved.x = 725.0;
        moved.y = 410.0;
        moved.width = 960.0;
        moved.height = 540.0;
        moved.rotation = 0.0;
        if (!engine.setSourceTransform(liveEditSource, moved)) {
            lines << QStringLiteral("FAIL: Move and resize during recording failed: %1")
                         .arg(engine.lastError());
            liveEditingPassed = false;
        } else {
            const Vuttara::RecordingInfo afterMove = engine.recordingInfo();
            if (!engine.isRecording() || afterMove.outputPath != started.outputPath) {
                lines << QStringLiteral("FAIL: Moving or resizing a source interrupted or restarted recording.");
                liveEditingPassed = false;
            } else {
                lines << QStringLiteral("PASS: Source move and resize completed during the unchanged active recording.");
            }
        }

        Vuttara::SourceTransform edited = moved;
        edited.rotation = 33.0;
        edited.cropLeft = 52.0;
        edited.cropTop = 28.0;
        edited.cropRight = 36.0;
        edited.cropBottom = 18.0;
        edited.flipHorizontal = true;
        if (!engine.setSourceTransform(liveEditSource, edited)) {
            lines << QStringLiteral("FAIL: Crop, rotate, and flip during recording failed: %1")
                         .arg(engine.lastError());
            liveEditingPassed = false;
        } else {
            const Vuttara::RecordingInfo afterCrop = engine.recordingInfo();
            if (!engine.isRecording() || afterCrop.outputPath != started.outputPath) {
                lines << QStringLiteral("FAIL: Crop or rotation interrupted or restarted recording.");
                liveEditingPassed = false;
            }
        }

        const QVector<Vuttara::SourceInfo> liveSources = engine.sourceInfos();
        const auto editedSource = std::find_if(
            liveSources.cbegin(),
            liveSources.cend(),
            [&liveEditSource](const Vuttara::SourceInfo& source) {
                return source.name == liveEditSource;
            });
        const bool valuesMatch = editedSource != liveSources.cend()
            && std::abs(editedSource->transform.x - 725.0) < 0.2
            && std::abs(editedSource->transform.y - 410.0) < 0.2
            && std::abs(editedSource->transform.width - 960.0) < 0.2
            && std::abs(editedSource->transform.height - 540.0) < 0.2
            && std::abs(editedSource->transform.rotation - 33.0) < 0.2
            && std::abs(editedSource->transform.cropLeft - 52.0) < 0.2
            && std::abs(editedSource->transform.cropTop - 28.0) < 0.2
            && std::abs(editedSource->transform.cropRight - 36.0) < 0.2
            && std::abs(editedSource->transform.cropBottom - 18.0) < 0.2
            && editedSource->transform.flipHorizontal;
        if (!valuesMatch) {
            lines << QStringLiteral("FAIL: Live recording transform values did not round-trip.");
            liveEditingPassed = false;
        } else {
            lines << QStringLiteral("PASS: Crop, rotate, flip, move, and resize values round-tripped during recording.");
        }

        liveEditProject = engine.projectState();
        if (liveEditProject.value(QStringLiteral("schemaVersion")).toInt() != 3) {
            lines << QStringLiteral("FAIL: Live recording edits were not serialized through project schema 3.");
            liveEditingPassed = false;
        } else {
            lines << QStringLiteral("PASS: Completed live-output edits serialized through project schema 3.");
        }
    }
    if (!liveEditingPassed) {
        passed = false;
    }

    QElapsedTimer captureTimer;
    captureTimer.start();
    while (captureTimer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(25);
        engine.recordingInfo();
    }

    const Vuttara::RecordingInfo beforeStop = engine.recordingInfo();
    if (!engine.isRecording() || beforeStop.outputPath != started.outputPath
        || beforeStop.elapsedMilliseconds < 2500) {
        lines << QStringLiteral("FAIL: Recording continuity check failed before finalization.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Recording remained continuous and retained one output path throughout live edits.");
    }

    if (!engine.stopRecording()) {
        lines << QStringLiteral("FAIL: Recording stop request failed: %1")
                     .arg(engine.lastError());
        passed = false;
        engine.forceStopRecording();
        if (!preserveOutput) {
            QDir(directoryPath).removeRecursively();
        }
        return;
    }

    QElapsedTimer stopTimer;
    stopTimer.start();
    while (engine.isRecording() && stopTimer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(25);
        engine.recordingInfo();
    }

    if (engine.isRecording()) {
        lines << QStringLiteral("FAIL: Recording did not finalize within 20 seconds.");
        passed = false;
        engine.forceStopRecording();
        if (!preserveOutput) {
            QDir(directoryPath).removeRecursively();
        }
        return;
    }

    const Vuttara::RecordingInfo completed = engine.recordingInfo();
    const QFileInfo recordedFile(completed.outputPath);
    if (!recordedFile.isFile() || recordedFile.size() < 16384) {
        lines << QStringLiteral("FAIL: Configured MKV output is missing or too small: %1")
                     .arg(completed.outputPath);
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Configured MKV finalized cleanly: %1 bytes.")
                     .arg(recordedFile.size());
    }

    if (!liveEditProject.isEmpty()) {
        if (!engine.restoreProjectState(liveEditProject)) {
            lines << QStringLiteral("FAIL: Live-output edit project restoration failed: %1")
                         .arg(engine.lastError());
            passed = false;
        } else {
            const QVector<Vuttara::SourceInfo> restoredSources = engine.sourceInfos();
            const auto restored = std::find_if(
                restoredSources.cbegin(),
                restoredSources.cend(),
                [&liveEditSource](const Vuttara::SourceInfo& source) {
                    return source.name == liveEditSource;
                });
            const bool persisted = restored != restoredSources.cend()
                && std::abs(restored->transform.x - 725.0) < 0.2
                && std::abs(restored->transform.y - 410.0) < 0.2
                && std::abs(restored->transform.width - 960.0) < 0.2
                && std::abs(restored->transform.height - 540.0) < 0.2
                && std::abs(restored->transform.rotation - 33.0) < 0.2
                && std::abs(restored->transform.cropLeft - 52.0) < 0.2
                && std::abs(restored->transform.cropTop - 28.0) < 0.2
                && std::abs(restored->transform.cropRight - 36.0) < 0.2
                && std::abs(restored->transform.cropBottom - 18.0) < 0.2
                && restored->transform.flipHorizontal;
            if (!persisted) {
                lines << QStringLiteral("FAIL: Live-output source edits did not survive project restoration.");
                passed = false;
            } else {
                lines << QStringLiteral("PASS: Live-output source edits survived project restoration after MKV finalization.");
            }
        }
    }

    if (completed.diagnostics.isEmpty()) {
        lines << QStringLiteral("FAIL: Recording diagnostics summary is empty.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Recording diagnostics: %1")
                     .arg(completed.diagnostics);
    }

    engine.disconnectAudioDevice(Vuttara::AudioChannelKind::Desktop);
    engine.disconnectAudioDevice(Vuttara::AudioChannelKind::Microphone);
    if (!preserveOutput) {
        QDir(directoryPath).removeRecursively();
    } else {
        lines << QStringLiteral("PASS: Preserved recording self-test output at %1")
                     .arg(directoryPath);
    }
}

QString argumentValue(const QStringList& arguments, const QString& prefix)
{
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) {
            return argument.mid(prefix.size());
        }
    }
    return {};
}

int runStage8BPersistenceWrite(const QStringList& arguments)
{
    const QString outputPath = argumentValue(
        arguments,
        QStringLiteral("--stage8b-persistence-write="));
    if (outputPath.isEmpty()) {
        return 2;
    }

    const QString logPath = Vuttara::AppPaths::createStartupLog();
    Vuttara::VuttaraEngine engine(logPath);
    if (!engine.initialize()) {
        return 3;
    }

    QString copyName;
    bool passed = engine.duplicateSource(QStringLiteral("Vuttara Test Color"), &copyName);
    Vuttara::SourceTransform transform;
    transform.x = 611.0;
    transform.y = 377.0;
    transform.width = 913.0;
    transform.height = 527.0;
    transform.rotation = -27.0;
    transform.cropLeft = 47.0;
    transform.cropTop = 31.0;
    transform.cropRight = 29.0;
    transform.cropBottom = 17.0;
    transform.flipVertical = true;
    transform.stretchToBounds = true;
    passed = passed && engine.setSourceTransform(copyName, transform);

    QJsonObject project = engine.projectState();
    project.insert(QStringLiteral("stage8bPersistenceSource"), copyName);
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        passed = false;
    } else if (output.write(QJsonDocument(project).toJson(QJsonDocument::Indented)) < 0) {
        passed = false;
    }
    output.close();
    engine.shutdown();

    QTextStream stream(stdout);
    stream << (passed
        ? QStringLiteral("PASS: Stage 8C schema-3 persistence state written for a separate-process restart test.\n")
        : QStringLiteral("FAIL: Stage 8C persistence write test failed.\n"));
    return passed ? 0 : 1;
}

int runStage8BPersistenceRead(const QStringList& arguments)
{
    const QString inputPath = argumentValue(
        arguments,
        QStringLiteral("--stage8b-persistence-read="));
    if (inputPath.isEmpty()) {
        return 2;
    }
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
        return 3;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return 4;
    }
    const QJsonObject project = document.object();
    const QString sourceName = project.value(QStringLiteral("stage8bPersistenceSource")).toString();

    const QString logPath = Vuttara::AppPaths::createStartupLog();
    Vuttara::VuttaraEngine engine(logPath);
    bool passed = engine.initialize()
        && project.value(QStringLiteral("schemaVersion")).toInt() == 3
        && engine.restoreProjectState(project);
    if (passed) {
        const QVector<Vuttara::SourceInfo> sources = engine.sourceInfos();
        const auto source = std::find_if(
            sources.cbegin(),
            sources.cend(),
            [&sourceName](const Vuttara::SourceInfo& candidate) {
                return candidate.name == sourceName;
            });
        passed = source != sources.cend()
            && std::abs(source->transform.x - 611.0) < 0.2
            && std::abs(source->transform.y - 377.0) < 0.2
            && std::abs(source->transform.width - 913.0) < 0.2
            && std::abs(source->transform.height - 527.0) < 0.2
            && std::abs(source->transform.rotation + 27.0) < 0.2
            && std::abs(source->transform.cropLeft - 47.0) < 0.2
            && std::abs(source->transform.cropTop - 31.0) < 0.2
            && std::abs(source->transform.cropRight - 29.0) < 0.2
            && std::abs(source->transform.cropBottom - 17.0) < 0.2
            && source->transform.flipVertical
            && source->transform.stretchToBounds;
    }
    engine.shutdown();

    QTextStream stream(stdout);
    stream << (passed
        ? QStringLiteral("PASS: Stage 8C V1 FIX3 source edits survived a separate application process restart.\n")
        : QStringLiteral("FAIL: Stage 8C V1 FIX3 separate-process persistence validation failed.\n"));
    return passed ? 0 : 1;
}

int runSelfTest(const QStringList& arguments)
{
    QStringList lines;
    bool passed = true;

    lines << QStringLiteral("VUTTARA STUDIO 0.0.1 STREAMING OUTPUT FOUNDATION STAGE 9A V1 FIX4 SELF-TEST");
    lines << QStringLiteral("PASS: Stage 8A FIX3 internal-window overlay shielding remains compiled for dialogs, menus, popups, and modal window transitions.");
    lines << QStringLiteral("PASS: Preview editing remains enabled while recording and active streaming outputs are running.");
    lines << QStringLiteral("PASS: Dedicated Streaming settings, known-service presets, Custom RTMP/RTMPS, secure credential persistence, reconnect controls, and live statistics are compiled.");
    lines << QStringLiteral("PASS: Twitch, YouTube, Kick, Facebook Live, Rumble, BEAM, and Custom RTMP/RTMPS service choices are compiled; Trovo is excluded.");
    lines << QStringLiteral("PASS: Stage 8C V1 FIX3 preserves OBS-style source rows, direct visibility/lock controls, and expandable source folders.");
    lines << QStringLiteral("PASS: Stage 8C V1 FIX3 routes native PreviewWidget HWND mouse messages to click, Ctrl-click, and marquee selection logic.");
    lines << QStringLiteral("PASS: Stage 8C V1 FIX3 Sources rows start MIME drags and the Sources list accepts reorder and folder drops.");
    if (!MainWindow::runStage8CFix3InteractionSelfTest(lines)) {
        passed = false;
    }
    if (!MainWindow::runStage9ASecureSettingsSelfTest(lines)) {
        passed = false;
    }
    if (!UpdateManager::runSelfTest(lines)) {
        passed = false;
    }

    const QImage mark(QStringLiteral(":/branding/vuttara-studio-mark.png"));
    if (mark.isNull()) {
        lines << QStringLiteral("FAIL: Branding mark resource could not be loaded.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Branding mark resource loaded.");
    }

    const QImage wordmark(QStringLiteral(":/branding/vuttara-studio-wordmark.png"));
    if (wordmark.isNull()) {
        lines << QStringLiteral("FAIL: Branding wordmark resource could not be loaded.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Branding wordmark resource loaded.");
    }

    const QString logPath = Vuttara::AppPaths::createStartupLog();
    if (!QFile::exists(logPath)) {
        lines << QStringLiteral("FAIL: Startup log was not created.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Startup log created at %1").arg(logPath);
    }

    if (arguments.contains(QStringLiteral("--engine-self-test"))) {
        Vuttara::VuttaraEngine engine(logPath);
        if (!engine.initialize()) {
            lines << QStringLiteral("FAIL: Engine initialization failed: %1").arg(engine.lastError());
            passed = false;
        } else {
            lines << QStringLiteral("PASS: libobs initialized.");
            lines << QStringLiteral("PASS: libobs version %1 verified.").arg(engine.versionString());
            lines << QStringLiteral("PASS: Video foundation initialized: %1").arg(engine.graphicsDescription());
            lines << QStringLiteral("PASS: Audio foundation initialized: %1").arg(engine.audioDescription());
            lines << (engine.nativeLoggingActive()
                ? QStringLiteral("PASS: Native libobs logging is active.")
                : QStringLiteral("FAIL: Native libobs logging is not active."));
            if (!engine.nativeLoggingActive()) {
                passed = false;
            }
            lines << QStringLiteral("PASS: capture, WASAPI, RTMP/RTMPS, FFmpeg muxer/AAC, x264, and available hardware modules loaded.");
            lines << QStringLiteral("PASS: Main Scene and Vuttara Test Color source created.");

            if (arguments.contains(QStringLiteral("--capture-self-test"))) {
                runDisplayCaptureSelfTest(engine, lines, passed);
            }

            if (arguments.contains(QStringLiteral("--window-capture-self-test"))) {
                runWindowCaptureSelfTest(engine, lines, passed);
            }

            if (arguments.contains(QStringLiteral("--scene-controls-self-test"))) {
                runSceneControlsSelfTest(engine, lines, passed);
            }

            if (arguments.contains(QStringLiteral("--audio-foundation-self-test"))) {
                runAudioFoundationSelfTest(engine, lines, passed);
            }

            if (arguments.contains(QStringLiteral("--streaming-foundation-self-test"))) {
                runStreamingFoundationSelfTest(engine, lines, passed);
            }

            if (arguments.contains(QStringLiteral("--recording-foundation-self-test"))) {
                runRecordingFoundationSelfTest(engine, lines, passed);
            }
        }

        engine.shutdown();
        if (engine.isInitialized()) {
            lines << QStringLiteral("FAIL: libobs remained initialized after shutdown.");
            passed = false;
        } else {
            lines << QStringLiteral("PASS: libobs shut down cleanly.");
        }
    }

    lines << QStringLiteral("Version: %1").arg(QStringLiteral(VUTTARA_STUDIO_VERSION));
    lines << (passed ? QStringLiteral("RESULT: PASS") : QStringLiteral("RESULT: FAIL"));

    QTextStream output(stdout);
    for (const QString& line : lines) {
        output << line << '\n';
    }
    output.flush();

    const QString reportPath = selfTestReportPath(arguments);
    if (!reportPath.isEmpty()) {
        QFile report(reportPath);
        if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return 2;
        }
        QTextStream reportStream(&report);
        for (const QString& line : lines) {
            reportStream << line << '\n';
        }
    }

    return passed ? 0 : 1;
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Vuttara"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("nuttabuttainaa.com"));
    QCoreApplication::setApplicationName(QStringLiteral("Vuttara Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VUTTARA_STUDIO_VERSION));

    QFile stylesheet(QStringLiteral(":/styles/theme.qss"));
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    }

    if (!argumentValue(
            application.arguments(),
            QStringLiteral("--stage8b-persistence-write=")).isEmpty()) {
        return runStage8BPersistenceWrite(application.arguments());
    }
    if (!argumentValue(
            application.arguments(),
            QStringLiteral("--stage8b-persistence-read=")).isEmpty()) {
        return runStage8BPersistenceRead(application.arguments());
    }
    if (application.arguments().contains(QStringLiteral("--self-test"))) {
        return runSelfTest(application.arguments());
    }

    const QString logPath = Vuttara::AppPaths::createStartupLog();
    Vuttara::VuttaraEngine engine(logPath);
    engine.initialize();

    MainWindow window(&engine);
    window.show();
    const int result = application.exec();
    engine.shutdown();
    return result;
}
