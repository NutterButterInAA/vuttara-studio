#include "VuttaraEngine.hpp"

#include "../AppPaths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Vuttara {
namespace {

constexpr std::uint32_t CanvasWidth = 1920;
constexpr std::uint32_t CanvasHeight = 1080;
constexpr std::uint32_t PreviewBackground = 0xFF14131A;
constexpr long long TestSourceColor = 0xFF8B4DB5;
constexpr int MaximumScenes = 12;
constexpr int DefaultAudioVolume = 70;

QString controlledRuntimeRoot()
{
    const QByteArray overrideRoot = qgetenv("VUTTARA_OBS_ROOT");
    if (!overrideRoot.isEmpty()) {
        return QDir::cleanPath(QString::fromLocal8Bit(overrideRoot));
    }

    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("obs"));
}

std::uint32_t safeDimension(std::uint32_t value)
{
    return std::max<std::uint32_t>(value, 1U);
}

QString formatNativeMessage(const char* format, va_list arguments)
{
    if (format == nullptr) {
        return QStringLiteral("(null libobs log message)");
    }

    va_list measureArguments;
    va_copy(measureArguments, arguments);
#ifdef _MSC_VER
    const int required = _vscprintf(format, measureArguments);
#else
    const int required = std::vsnprintf(nullptr, 0, format, measureArguments);
#endif
    va_end(measureArguments);

    if (required <= 0) {
        return QString::fromUtf8(format);
    }

    std::vector<char> buffer(static_cast<std::size_t>(required) + 1U, '\0');
    va_list writeArguments;
    va_copy(writeArguments, arguments);
    std::vsnprintf(buffer.data(), buffer.size(), format, writeArguments);
    va_end(writeArguments);
    return QString::fromUtf8(buffer.data());
}

QString nativeLevelName(int level)
{
    if (level <= 100) {
        return QStringLiteral("ERROR");
    }
    if (level <= 200) {
        return QStringLiteral("WARNING");
    }
    if (level <= 300) {
        return QStringLiteral("INFO");
    }
    return QStringLiteral("DEBUG");
}


StreamingSettings normalizedStreamingSettings(StreamingSettings settings)
{
    settings.server = settings.server.trimmed();
    settings.streamKey = settings.streamKey.trimmed();
    settings.username = settings.username.trimmed();
    if (!settings.server.startsWith(QStringLiteral("rtmp://"), Qt::CaseInsensitive) &&
        !settings.server.startsWith(QStringLiteral("rtmps://"), Qt::CaseInsensitive)) {
        settings.server.clear();
    }
    settings.outputWidth = settings.outputWidth == 1280 ? 1280 : 1920;
    settings.outputHeight = settings.outputWidth == 1280 ? 720 : 1080;
    settings.framesPerSecond = settings.framesPerSecond == 30 ? 30 : 60;
    settings.videoBitrateKbps = std::clamp(settings.videoBitrateKbps, 1000, 50000);
    settings.keyframeIntervalSeconds = std::clamp(settings.keyframeIntervalSeconds, 1, 10);
    settings.audioBitrateKbps = std::clamp(settings.audioBitrateKbps, 96, 320);
    settings.retryDelaySeconds = std::clamp(settings.retryDelaySeconds, 1, 60);
    settings.maximumRetries = std::clamp(settings.maximumRetries, 0, 1000);
    if (!settings.automaticReconnect) {
        settings.maximumRetries = 0;
    }
    return settings;
}

RecordingSettings normalizedRecordingSettings(RecordingSettings settings)
{
    settings.outputDirectory = QDir::cleanPath(settings.outputDirectory.trimmed());
    settings.filenameFormat = settings.filenameFormat.trimmed();
    if (settings.filenameFormat.isEmpty()) {
        settings.filenameFormat = QStringLiteral("VuttaraStudio_{date}_{time}");
    }

    const struct Resolution {
        int width;
        int height;
    } supportedResolutions[] = {
        {1920, 1080},
        {1600, 900},
        {1280, 720},
        {854, 480},
    };

    bool resolutionSupported = false;
    for (const Resolution& resolution : supportedResolutions) {
        if (
            settings.outputWidth == resolution.width &&
            settings.outputHeight == resolution.height) {
            resolutionSupported = true;
            break;
        }
    }
    if (!resolutionSupported) {
        settings.outputWidth = 1920;
        settings.outputHeight = 1080;
    }

    if (settings.framesPerSecond != 30 && settings.framesPerSecond != 60) {
        settings.framesPerSecond = 60;
    }

    settings.videoBitrateKbps = std::clamp(settings.videoBitrateKbps, 1000, 50000);
    settings.audioBitrateKbps = std::clamp(settings.audioBitrateKbps, 96, 320);
    return settings;
}

QString safeRecordingStem(QString stem)
{
    stem.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])")), QStringLiteral("_"));
    stem.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    stem = stem.trimmed();
    while (stem.endsWith(QLatin1Char('.')) || stem.endsWith(QLatin1Char(' '))) {
        stem.chop(1);
    }
    if (stem.isEmpty()) {
        stem = QStringLiteral("VuttaraStudio_Recording");
    }
    return stem.left(160);
}

std::uint64_t estimatedBytes(const RecordingSettings& settings, qint64 seconds)
{
    const std::uint64_t totalKbps = static_cast<std::uint64_t>(
        std::max(0, settings.videoBitrateKbps) +
        std::max(0, settings.audioBitrateKbps));
    return totalKbps * 1000ULL * static_cast<std::uint64_t>(std::max<qint64>(0, seconds)) / 8ULL;
}

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

}

VuttaraEngine::VuttaraEngine(QString logPath)
    : logPath_(std::move(logPath))
{
}

VuttaraEngine::~VuttaraEngine()
{
    shutdown();
}

bool VuttaraEngine::initialize()
{
    if (ready_.load()) {
        return true;
    }

    lastError_.clear();
    graphicsDescription_.clear();
    scenes_.clear();
    activeSceneIndex_ = -1;
    log(QStringLiteral("Settings and Source Properties Stage 7A: locating controlled libobs runtime."));

    const QString runtimeRoot = controlledRuntimeRoot();
    QString runtimeError;
    if (!runtime_.load(runtimeRoot, &runtimeError)) {
        return fail(runtimeError);
    }

    const auto& api = runtime_.api();
    api.base_set_log_handler(&VuttaraEngine::libobsLogCallback, this);
    nativeLoggingActive_.store(true);
    log(QStringLiteral("Settings and Source Properties Stage 7A: native libobs logging handler installed."));

    versionString_ = runtime_.versionString();
    if (versionString_ != QStringLiteral("32.2.1")) {
        return fail(QStringLiteral("Controlled libobs version mismatch. Expected 32.2.1, found %1.")
                        .arg(versionString_.isEmpty() ? QStringLiteral("unknown") : versionString_));
    }
    log(QStringLiteral("Settings and Source Properties Stage 7A: loaded libobs %1.").arg(versionString_));

    const QString coreDataPath = QDir(runtime_.dataDirectory()).filePath(QStringLiteral("libobs"));
    if (!validateCoreData(coreDataPath)) {
        return false;
    }

    QString coreDataSearchPrefix = QDir::fromNativeSeparators(QDir::cleanPath(coreDataPath));
    if (!coreDataSearchPrefix.endsWith(QLatin1Char('/'))) {
        coreDataSearchPrefix.append(QLatin1Char('/'));
    }
    coreDataPathUtf8_ = coreDataSearchPrefix.toUtf8();
    api.obs_add_data_path(coreDataPathUtf8_.constData());
    log(QStringLiteral(
            "Settings and Source Properties Stage 7A: registered separator-normalized libobs core data search prefix: %1")
            .arg(coreDataSearchPrefix));

    const QString configPath = AppPaths::engineConfigDirectory();
    configPathUtf8_ = QDir::fromNativeSeparators(configPath).toUtf8();
    if (!api.obs_startup("en-US", configPathUtf8_.constData(), nullptr)) {
        return fail(QStringLiteral("obs_startup failed. Review the native libobs lines in %1.").arg(logPath_));
    }
    log(QStringLiteral("Settings and Source Properties Stage 7A: obs_startup completed."));

    const QString graphicsModule = QDir(runtime_.binaryDirectory()).filePath(QStringLiteral("libobs-d3d11.dll"));
    graphicsModuleUtf8_ = QDir::fromNativeSeparators(graphicsModule).toUtf8();

    ObsAbi::obs_video_info videoInfo{};
    videoInfo.graphics_module = graphicsModuleUtf8_.constData();
    videoInfo.fps_num = 60;
    videoInfo.fps_den = 1;
    videoInfo.base_width = CanvasWidth;
    videoInfo.base_height = CanvasHeight;
    videoInfo.output_width = CanvasWidth;
    videoInfo.output_height = CanvasHeight;
    videoInfo.output_format = ObsAbi::VIDEO_FORMAT_BGRA;
    videoInfo.adapter = 0;
    videoInfo.gpu_conversion = false;
    videoInfo.colorspace = ObsAbi::VIDEO_CS_SRGB;
    videoInfo.range = ObsAbi::VIDEO_RANGE_FULL;
    videoInfo.scale_type = ObsAbi::OBS_SCALE_DISABLE;

    log(QStringLiteral(
        "Settings and Source Properties Stage 7A: starting Direct3D 11 with BGRA foundation output and GPU conversion disabled."));
    const int videoResult = api.obs_reset_video(&videoInfo);
    if (videoResult != 0) {
        return fail(QStringLiteral(
            "obs_reset_video failed with code %1 using the validated BGRA foundation. "
            "The native libobs reason is recorded in %2.")
                        .arg(videoResult)
                        .arg(logPath_));
    }
    graphicsDescription_ = QStringLiteral("Direct3D 11 — 1920x1080 @ 60 FPS (BGRA foundation)");
    log(QStringLiteral("Settings and Source Properties Stage 7A: Direct3D 11 BGRA video foundation initialized."));

    ObsAbi::obs_audio_info audioInfo{};
    audioInfo.samples_per_sec = 48000;
    audioInfo.speakers = ObsAbi::SPEAKERS_STEREO;
    if (!api.obs_reset_audio(&audioInfo)) {
        return fail(QStringLiteral("obs_reset_audio failed. Review the native libobs lines in %1.").arg(logPath_));
    }
    log(QStringLiteral("Settings and Source Properties Stage 7A: 48 kHz stereo audio initialized."));

    const QString recordingMuxHelper = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("obs-ffmpeg-mux.exe"));
    if (!QFileInfo(recordingMuxHelper).isFile()) {
        return fail(QStringLiteral(
            "Recording helper is missing beside VuttaraStudio.exe: %1")
                        .arg(recordingMuxHelper));
    }

    if (!loadModule(
            QStringLiteral("image-source.dll"),
            QStringLiteral("obs-plugins/image-source"),
            &imageSourceModule_)) {
        return false;
    }

    if (!loadModule(
            QStringLiteral("win-capture.dll"),
            QStringLiteral("obs-plugins/win-capture"),
            &winCaptureModule_)) {
        return false;
    }

    if (!loadModule(
            QStringLiteral("win-wasapi.dll"),
            QStringLiteral("obs-plugins/win-wasapi"),
            &wasapiModule_)) {
        return false;
    }

    if (!loadModule(
            QStringLiteral("obs-ffmpeg.dll"),
            QStringLiteral("obs-plugins/obs-ffmpeg"),
            &ffmpegModule_)) {
        return false;
    }

    if (!loadModule(
            QStringLiteral("obs-x264.dll"),
            QStringLiteral("obs-plugins/obs-x264"),
            &x264Module_)) {
        return false;
    }

    if (!loadModule(
            QStringLiteral("obs-outputs.dll"),
            QStringLiteral("obs-plugins/obs-outputs"),
            &outputsModule_)) {
        return false;
    }

    if (!loadModule(
            QStringLiteral("rtmp-services.dll"),
            QStringLiteral("obs-plugins/rtmp-services"),
            &rtmpServicesModule_)) {
        return false;
    }

    loadOptionalModule(
        QStringLiteral("obs-nvenc.dll"),
        QStringLiteral("obs-plugins/obs-nvenc"),
        nvencModulePathUtf8_,
        nvencModuleDataPathUtf8_,
        &nvencModule_);
    loadOptionalModule(
        QStringLiteral("obs-qsv11.dll"),
        QStringLiteral("obs-plugins/obs-qsv11"),
        qsvModulePathUtf8_,
        qsvModuleDataPathUtf8_,
        &qsvModule_);

    api.obs_post_load_modules();
    api.obs_log_loaded_modules();

    if (!registeredOutputType("rtmp_output")) {
        return fail(QStringLiteral("The rtmp_output streaming output was not registered."));
    }
    if (!registeredServiceType("rtmp_custom")) {
        return fail(QStringLiteral("The rtmp_custom streaming service was not registered."));
    }
    if (!registeredOutputType("ffmpeg_muxer")) {
        return fail(QStringLiteral("The ffmpeg_muxer recording output was not registered."));
    }
    if (!registeredEncoderType("ffmpeg_aac")) {
        return fail(QStringLiteral("The ffmpeg_aac recording encoder was not registered."));
    }
    if (!registeredEncoderType("obs_x264")) {
        return fail(QStringLiteral("The obs_x264 fallback encoder was not registered."));
    }

    log(QStringLiteral(
        "Streaming Output Foundation Stage 9A: capture, WASAPI, RTMP/RTMPS, FFmpeg muxer/AAC, x264, and available hardware modules loaded."));

    if (!createSceneInternal(QStringLiteral("Main Scene"), false, true)) {
        return fail(QStringLiteral("Could not create the protected Main Scene."));
    }

    activeSceneIndex_ = 0;
    api.obs_set_output_source(0, scenes_.first().source);
    ready_.store(true);
    log(QStringLiteral(
        "Settings and Source Properties Stage 7A: scenes, sources, Windows audio devices, faders, mute controls, and IEC meters are ready."));
    return true;
}

void VuttaraEngine::shutdown()
{
    if (!runtime_.isLoaded()) {
        ready_.store(false);
        nativeLoggingActive_.store(false);
        scenes_.clear();
        activeSceneIndex_ = -1;
        return;
    }

    log(QStringLiteral("Engine shutdown: beginning orderly shutdown."));
    ready_.store(false);
    detachPreview();

    const auto& api = runtime_.api();
    if (api.obs_initialized != nullptr && api.obs_initialized()) {
        if (streamingOutput_ != nullptr) {
            if (api.obs_output_active(streamingOutput_) || api.obs_output_reconnecting(streamingOutput_)) {
                api.obs_output_force_stop(streamingOutput_);
            }
            releaseStreamingResources();
        }
        if (recordingOutput_ != nullptr) {
            if (api.obs_output_active(recordingOutput_)) {
                api.obs_output_force_stop(recordingOutput_);
            }
            releaseRecordingResources();
        }
        releaseAllAudioChannels();
        api.obs_set_output_source(0, nullptr);
        releaseAllScenes();
        api.obs_shutdown();
        log(QStringLiteral("Engine shutdown: obs_shutdown completed."));
    }

    if (api.base_set_log_handler != nullptr) {
        api.base_set_log_handler(nullptr, nullptr);
    }

    nativeLoggingActive_.store(false);
    imageSourceModule_ = nullptr;
    winCaptureModule_ = nullptr;
    wasapiModule_ = nullptr;
    ffmpegModule_ = nullptr;
    x264Module_ = nullptr;
    nvencModule_ = nullptr;
    qsvModule_ = nullptr;
    outputsModule_ = nullptr;
    rtmpServicesModule_ = nullptr;
    streamingInfo_ = {};
    recordingInfo_ = {};
    versionString_.clear();
    graphicsDescription_.clear();
    runtime_.unload();
    log(QStringLiteral("Engine shutdown: controlled runtime unloaded."));
}

bool VuttaraEngine::attachPreview(void* nativeWindow, std::uint32_t width, std::uint32_t height)
{
    if (!ready_.load() || nativeWindow == nullptr) {
        return false;
    }

    detachPreview();

    ObsAbi::gs_init_data displayInfo{};
    displayInfo.window.hwnd = nativeWindow;
    displayInfo.cx = safeDimension(width);
    displayInfo.cy = safeDimension(height);
    displayInfo.num_backbuffers = 2;
    displayInfo.format = ObsAbi::GS_BGRA;
    displayInfo.zsformat = ObsAbi::GS_ZS_NONE;
    displayInfo.adapter = 0;

    const auto& api = runtime_.api();
    display_ = api.obs_display_create(&displayInfo, PreviewBackground);
    if (display_ == nullptr) {
        operationFail(QStringLiteral("Could not create the libobs preview display. Review %1.").arg(logPath_));
        return false;
    }

    api.obs_display_add_draw_callback(display_, &VuttaraEngine::renderPreview, this);
    log(QStringLiteral("Preview: libobs display attached to the Qt native window."));
    return true;
}

void VuttaraEngine::resizePreview(std::uint32_t width, std::uint32_t height)
{
    if (display_ != nullptr) {
        runtime_.api().obs_display_resize(display_, safeDimension(width), safeDimension(height));
    }
}

void VuttaraEngine::detachPreview()
{
    if (display_ == nullptr || !runtime_.isLoaded()) {
        display_ = nullptr;
        return;
    }

    const auto& api = runtime_.api();
    api.obs_display_remove_draw_callback(display_, &VuttaraEngine::renderPreview, this);
    api.obs_display_destroy(display_);
    display_ = nullptr;
    log(QStringLiteral("Preview: libobs display detached."));
}

QVector<DisplayInfo> VuttaraEngine::availableDisplays() const
{
    return enumerateWindowsDisplays();
}

QVector<WindowInfo> VuttaraEngine::availableWindows() const
{
    return enumerateCapturableWindows();
}

QVector<AudioDeviceInfo> VuttaraEngine::availableDesktopAudioDevices() const
{
    return enumerateDesktopAudioDevices();
}

QVector<AudioDeviceInfo> VuttaraEngine::availableMicrophoneDevices() const
{
    return enumerateMicrophoneDevices();
}

AudioChannelInfo VuttaraEngine::audioChannelInfo(AudioChannelKind kind) const
{
    return audioChannelInfo(audioChannel(kind));
}

QVector<SceneInfo> VuttaraEngine::sceneInfos() const
{
    QVector<SceneInfo> result;
    result.reserve(scenes_.size());

    for (qsizetype index = 0; index < scenes_.size(); ++index) {
        const ManagedScene& scene = scenes_.at(index);
        result.append(SceneInfo{
            scene.name,
            static_cast<int>(index) == activeSceneIndex_,
            scene.removable,
            static_cast<int>(scene.sources.size()),
        });
    }

    return result;
}

QVector<SourceInfo> VuttaraEngine::sourceInfos() const
{
    QVector<SourceInfo> result;
    const ManagedScene* scene = activeScene();
    if (scene == nullptr) {
        return result;
    }

    result.reserve(scene->sources.size());
    for (const ManagedSource& source : scene->sources) {
        result.append(sourceInfo(source));
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const SourceInfo& left, const SourceInfo& right) {
            return left.orderPosition > right.orderPosition;
        });
    return result;
}

SourcePropertiesModel VuttaraEngine::sourceProperties(
    const QString& sourceName) const
{
    SourcePropertiesModel model;
    const ManagedScene* scene = activeScene();
    const ManagedSource* source =
        scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return model;
    }

    model.sourceName = source->name;
    model.sourceType = source->type;
    model.sourceTypeName =
        source->type == QStringLiteral("display_capture")
        ? QStringLiteral("Display Capture")
        : source->type == QStringLiteral("window_capture")
            ? QStringLiteral("Window Capture")
            : source->type == QStringLiteral("color")
                ? QStringLiteral("Color Source")
                : QStringLiteral("Source");

    const auto& api = runtime_.api();
    ObsAbi::obs_source_t* obsSource =
        source->item != nullptr ? api.obs_sceneitem_get_source(source->item) : nullptr;
    ObsAbi::obs_data_t* sourceSettings =
        obsSource != nullptr ? api.obs_source_get_settings(obsSource) : nullptr;

    const auto settingString = [&api, sourceSettings](
                                   const char* key,
                                   const QString& fallback) {
        if (sourceSettings == nullptr) {
            return fallback;
        }
        const char* value = api.obs_data_get_string(sourceSettings, key);
        return value != nullptr && value[0] != '\0'
            ? QString::fromUtf8(value)
            : fallback;
    };
    const auto settingInteger = [&api, sourceSettings](
                                    const char* key,
                                    long long fallback) {
        return sourceSettings != nullptr
            ? api.obs_data_get_int(sourceSettings, key)
            : fallback;
    };
    const auto settingBoolean = [&api, sourceSettings](
                                    const char* key,
                                    bool fallback) {
        return sourceSettings != nullptr
            ? api.obs_data_get_bool(sourceSettings, key)
            : fallback;
    };

    auto addText = [&model](
                       const QString& key,
                       const QString& label,
                       const QString& description,
                       const QString& value,
                       bool readOnly = false) {
        model.properties.append(SourcePropertyDefinition{
            key,
            label,
            description,
            SourcePropertyKind::Text,
            value,
            value,
            {},
            0,
            0,
            1,
            readOnly,
        });
    };
    auto addBoolean = [&model](
                          const QString& key,
                          const QString& label,
                          const QString& description,
                          bool value,
                          bool defaultValue) {
        model.properties.append(SourcePropertyDefinition{
            key,
            label,
            description,
            SourcePropertyKind::Boolean,
            value,
            defaultValue,
        });
    };
    auto addInteger = [&model](
                          const QString& key,
                          const QString& label,
                          const QString& description,
                          int value,
                          int defaultValue,
                          int minimum,
                          int maximum,
                          int step) {
        model.properties.append(SourcePropertyDefinition{
            key,
            label,
            description,
            SourcePropertyKind::Integer,
            value,
            defaultValue,
            {},
            minimum,
            maximum,
            step,
            false,
        });
    };
    auto addChoice = [&model](
                         const QString& key,
                         const QString& label,
                         const QString& description,
                         const QJsonValue& value,
                         const QJsonValue& defaultValue,
                         QVector<SourcePropertyOption> options) {
        model.properties.append(SourcePropertyDefinition{
            key,
            label,
            description,
            SourcePropertyKind::Choice,
            value,
            defaultValue,
            std::move(options),
        });
    };

    addText(
        QStringLiteral("name"),
        QStringLiteral("Source name"),
        QStringLiteral("The unique name shown in the Sources dock."),
        source->name,
        !source->removable && source->type == QStringLiteral("color"));

    if (source->type == QStringLiteral("display_capture")) {
        const QString monitorId = settingString("monitor_id", source->persistentId);
        const bool captureCursor = settingBoolean("capture_cursor", source->captureCursor);
        QVector<SourcePropertyOption> displayOptions;
        for (const DisplayInfo& display : availableDisplays()) {
            displayOptions.append(SourcePropertyOption{
                display.description,
                display.monitorId,
            });
        }
        if (std::none_of(
                displayOptions.cbegin(),
                displayOptions.cend(),
                [&monitorId](const SourcePropertyOption& option) {
                    return option.value.toString() == monitorId;
                })) {
            displayOptions.prepend(SourcePropertyOption{
                QStringLiteral("Saved display (currently unavailable)"),
                monitorId,
            });
        }
        addChoice(
            QStringLiteral("monitorId"),
            QStringLiteral("Display"),
            QStringLiteral("The Windows display captured by this source."),
            monitorId,
            monitorId,
            displayOptions);
        addBoolean(
            QStringLiteral("captureCursor"),
            QStringLiteral("Capture mouse cursor"),
            QStringLiteral("Include the Windows pointer in the captured image."),
            captureCursor,
            true);
    } else if (source->type == QStringLiteral("window_capture")) {
        const QString encodedWindow = settingString("window", source->persistentId);
        const auto method = static_cast<WindowCaptureMethod>(settingInteger(
            "method",
            static_cast<long long>(source->windowMethod)));
        const auto priority = static_cast<WindowMatchPriority>(settingInteger(
            "priority",
            static_cast<long long>(source->windowPriority)));
        const bool captureCursor = settingBoolean("cursor", source->captureCursor);
        const bool clientArea = settingBoolean("client_area", source->clientArea);
        QVector<SourcePropertyOption> windowOptions;
        for (const WindowInfo& window : availableWindows()) {
            windowOptions.append(SourcePropertyOption{
                window.description,
                window.encodedValue,
            });
        }
        if (std::none_of(
                windowOptions.cbegin(),
                windowOptions.cend(),
                [&encodedWindow](const SourcePropertyOption& option) {
                    return option.value.toString() == encodedWindow;
                })) {
            windowOptions.prepend(SourcePropertyOption{
                QStringLiteral("Saved window (currently unavailable)"),
                encodedWindow,
            });
        }
        addChoice(
            QStringLiteral("window"),
            QStringLiteral("Window"),
            QStringLiteral("The open Windows application window captured by this source."),
            encodedWindow,
            encodedWindow,
            windowOptions);
        addChoice(
            QStringLiteral("method"),
            QStringLiteral("Capture method"),
            QStringLiteral("Automatic is recommended; choose a specific method only for compatibility."),
            static_cast<int>(method),
            static_cast<int>(WindowCaptureMethod::Automatic),
            {
                {QStringLiteral("Automatic"), static_cast<int>(WindowCaptureMethod::Automatic)},
                {QStringLiteral("BitBlt"), static_cast<int>(WindowCaptureMethod::BitBlt)},
                {QStringLiteral("Windows Graphics Capture"), static_cast<int>(WindowCaptureMethod::WindowsGraphicsCapture)},
            });
        addChoice(
            QStringLiteral("priority"),
            QStringLiteral("Window match priority"),
            QStringLiteral("Controls how the source reconnects when the target window changes."),
            static_cast<int>(priority),
            static_cast<int>(WindowMatchPriority::Title),
            {
                {QStringLiteral("Match window class"), static_cast<int>(WindowMatchPriority::Class)},
                {QStringLiteral("Match window title"), static_cast<int>(WindowMatchPriority::Title)},
                {QStringLiteral("Match executable"), static_cast<int>(WindowMatchPriority::Executable)},
            });
        addBoolean(
            QStringLiteral("captureCursor"),
            QStringLiteral("Capture mouse cursor"),
            QStringLiteral("Include the Windows pointer in the captured image."),
            captureCursor,
            true);
        addBoolean(
            QStringLiteral("clientArea"),
            QStringLiteral("Capture client area only"),
            QStringLiteral("Exclude the window border and title bar when supported."),
            clientArea,
            true);
    } else if (source->type == QStringLiteral("color")) {
        const std::uint32_t color = static_cast<std::uint32_t>(settingInteger(
            "color",
            static_cast<long long>(source->color)));
        const int sourceWidth = static_cast<int>(settingInteger("width", source->sourceWidth));
        const int sourceHeight = static_cast<int>(settingInteger("height", source->sourceHeight));
        model.properties.append(SourcePropertyDefinition{
            QStringLiteral("color"),
            QStringLiteral("Color"),
            QStringLiteral("The background color produced by the libobs color source."),
            SourcePropertyKind::Color,
            static_cast<double>(color),
            static_cast<double>(TestSourceColor),
        });
        addInteger(
            QStringLiteral("width"),
            QStringLiteral("Source width"),
            QStringLiteral("Native width of the generated color source."),
            sourceWidth,
            static_cast<int>(CanvasWidth),
            1,
            7680,
            1);
        addInteger(
            QStringLiteral("height"),
            QStringLiteral("Source height"),
            QStringLiteral("Native height of the generated color source."),
            sourceHeight,
            static_cast<int>(CanvasHeight),
            1,
            7680,
            1);
    } else {
        model.properties.append(SourcePropertyDefinition{
            QStringLiteral("information"),
            QStringLiteral("Properties"),
            QStringLiteral("This source type does not yet expose editable properties."),
            SourcePropertyKind::Information,
            QStringLiteral("No editable properties are registered for this source type."),
            {},
            {},
            0,
            0,
            1,
            true,
        });
    }

    if (sourceSettings != nullptr) {
        api.obs_data_release(sourceSettings);
    }
    return model;
}

QString VuttaraEngine::activeSceneName() const
{
    const ManagedScene* scene = activeScene();
    return scene != nullptr ? scene->name : QString{};
}

bool VuttaraEngine::hasDisplayCapture() const
{
    const ManagedScene* scene = activeScene();
    if (scene == nullptr) {
        return false;
    }

    return std::any_of(
        scene->sources.cbegin(),
        scene->sources.cend(),
        [](const ManagedSource& source) {
            return source.type == QStringLiteral("display_capture");
        });
}

bool VuttaraEngine::hasWindowCapture() const
{
    const ManagedScene* scene = activeScene();
    if (scene == nullptr) {
        return false;
    }

    return std::any_of(
        scene->sources.cbegin(),
        scene->sources.cend(),
        [](const ManagedSource& source) {
            return source.type == QStringLiteral("window_capture");
        });
}

bool VuttaraEngine::addScene(const QString& requestedName, QString* createdName)
{
    if (!ready_.load()) {
        return operationFail(QStringLiteral("The engine is not ready to add a scene."));
    }
    if (scenes_.size() >= MaximumScenes) {
        return operationFail(QStringLiteral("Stage 4A supports up to %1 scenes.").arg(MaximumScenes));
    }

    const QString name = uniqueSceneName(requestedName);
    if (!createSceneInternal(name, true, false)) {
        return operationFail(QStringLiteral("Could not create scene: %1").arg(name));
    }

    if (!switchScene(name)) {
        return false;
    }

    if (createdName != nullptr) {
        *createdName = name;
    }

    log(QStringLiteral("Scene created and activated: %1").arg(name));
    return true;
}

bool VuttaraEngine::removeScene(const QString& sceneName)
{
    const auto iterator = std::find_if(
        scenes_.begin(),
        scenes_.end(),
        [&sceneName](const ManagedScene& scene) {
            return scene.name == sceneName;
        });

    if (iterator == scenes_.end()) {
        return operationFail(QStringLiteral("Scene not found: %1").arg(sceneName));
    }
    if (!iterator->removable) {
        return operationFail(QStringLiteral("%1 is the protected foundation scene.").arg(sceneName));
    }
    if (scenes_.size() <= 1) {
        return operationFail(QStringLiteral("At least one scene must remain."));
    }

    const int removedIndex = static_cast<int>(std::distance(scenes_.begin(), iterator));
    if (removedIndex == activeSceneIndex_) {
        const int fallbackIndex = removedIndex == 0 ? 1 : 0;
        activeSceneIndex_ = fallbackIndex;
        runtime_.api().obs_set_output_source(0, scenes_.at(fallbackIndex).source);
    }

    runtime_.api().obs_scene_release(iterator->scene);
    scenes_.erase(iterator);

    if (removedIndex < activeSceneIndex_) {
        --activeSceneIndex_;
    }
    activeSceneIndex_ = std::clamp(activeSceneIndex_, 0, static_cast<int>(scenes_.size()) - 1);
    runtime_.api().obs_set_output_source(0, scenes_.at(activeSceneIndex_).source);

    lastError_.clear();
    log(QStringLiteral("Scene removed: %1; active scene is now %2")
            .arg(sceneName, activeSceneName()));
    return true;
}

bool VuttaraEngine::switchScene(const QString& sceneName)
{
    const auto iterator = std::find_if(
        scenes_.cbegin(),
        scenes_.cend(),
        [&sceneName](const ManagedScene& scene) {
            return scene.name == sceneName;
        });

    if (iterator == scenes_.cend()) {
        return operationFail(QStringLiteral("Scene not found: %1").arg(sceneName));
    }

    activeSceneIndex_ = static_cast<int>(std::distance(scenes_.cbegin(), iterator));
    runtime_.api().obs_set_output_source(0, scenes_.at(activeSceneIndex_).source);
    lastError_.clear();
    log(QStringLiteral("Active scene switched to: %1").arg(sceneName));
    return true;
}

bool VuttaraEngine::addDisplayCapture(
    const DisplayInfo& display,
    bool captureCursor,
    const QString& requestedName,
    QString* createdName)
{
    ManagedScene* scene = activeScene();
    if (!ready_.load() || scene == nullptr) {
        return operationFail(QStringLiteral("The engine is not ready to add a Display Capture source."));
    }

    return addDisplayCaptureToScene(*scene, display, captureCursor, requestedName, true, createdName);
}

bool VuttaraEngine::addWindowCapture(
    const WindowInfo& window,
    WindowCaptureMethod method,
    WindowMatchPriority priority,
    bool captureCursor,
    bool clientArea,
    const QString& requestedName,
    QString* createdName)
{
    ManagedScene* scene = activeScene();
    if (!ready_.load() || scene == nullptr) {
        return operationFail(QStringLiteral("The engine is not ready to add a Window Capture source."));
    }

    return addWindowCaptureToScene(
        *scene,
        window,
        method,
        priority,
        captureCursor,
        clientArea,
        requestedName,
        true,
        createdName);
}

bool VuttaraEngine::removeSource(const QString& sourceName)
{
    ManagedScene* scene = activeScene();
    if (scene == nullptr) {
        return operationFail(QStringLiteral("No active scene is available."));
    }

    const auto iterator = std::find_if(
        scene->sources.begin(),
        scene->sources.end(),
        [&sourceName](const ManagedSource& source) {
            return source.name == sourceName;
        });

    if (iterator == scene->sources.end()) {
        return operationFail(QStringLiteral("Source not found in %1: %2").arg(scene->name, sourceName));
    }
    if (!iterator->removable) {
        return operationFail(QStringLiteral("%1 is a protected foundation source.").arg(sourceName));
    }

    runtime_.api().obs_sceneitem_remove(iterator->item);
    scene->sources.erase(iterator);
    lastError_.clear();
    log(QStringLiteral("Source removed from %1: %2").arg(scene->name, sourceName));
    return true;
}



bool VuttaraEngine::duplicateSource(const QString& sourceName, QString* createdName)
{
    ManagedScene* scene = activeScene();
    ManagedSource* source = scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return operationFail(QStringLiteral("Source not found in the active scene: %1").arg(sourceName));
    }

    const QString type = source->type;
    const QString persistentId = source->persistentId;
    const bool visible = runtime_.api().obs_sceneitem_visible(source->item);
    const bool captureCursor = source->captureCursor;
    const bool clientArea = source->clientArea;
    const WindowCaptureMethod method = source->windowMethod;
    const WindowMatchPriority priority = source->windowPriority;
    const std::uint32_t color = source->color;
    const int sourceWidth = source->sourceWidth;
    const int sourceHeight = source->sourceHeight;
    SourceTransform transform = sourceTransform(*source);
    transform.x += 24.0;
    transform.y += 24.0;

    QString duplicateName;
    bool created = false;
    if (type == QStringLiteral("color")) {
        created = addColorSourceToScene(
            *scene,
            QStringLiteral("%1 Copy").arg(sourceName),
            color,
            sourceWidth,
            sourceHeight,
            true,
            false,
            &duplicateName);
    } else if (type == QStringLiteral("display_capture")) {
        DisplayInfo display;
        display.monitorId = persistentId;
        display.description = QStringLiteral("duplicated Windows display");
        created = addDisplayCaptureToScene(
            *scene,
            display,
            captureCursor,
            QStringLiteral("%1 Copy").arg(sourceName),
            false,
            &duplicateName);
    } else if (type == QStringLiteral("window_capture")) {
        WindowInfo window;
        window.encodedValue = persistentId;
        window.description = QStringLiteral("duplicated Windows application window");
        created = addWindowCaptureToScene(
            *scene,
            window,
            method,
            priority,
            captureCursor,
            clientArea,
            QStringLiteral("%1 Copy").arg(sourceName),
            false,
            &duplicateName);
    }

    if (!created) {
        return operationFail(
            lastError_.isEmpty()
                ? QStringLiteral("This source type cannot be duplicated.")
                : lastError_);
    }

    ManagedSource* duplicate = findSource(*scene, duplicateName);
    if (duplicate == nullptr) {
        return operationFail(QStringLiteral("The duplicated source could not be found."));
    }

    runtime_.api().obs_sceneitem_set_visible(duplicate->item, visible);
    duplicate->visible = runtime_.api().obs_sceneitem_visible(duplicate->item);
    runtime_.api().obs_sceneitem_set_locked(duplicate->item, false);
    duplicate->locked = false;
    if (!applySourceTransform(*duplicate, transform, true)) {
        removeSource(duplicateName);
        return false;
    }
    runtime_.api().obs_sceneitem_set_order(duplicate->item, ObsAbi::OBS_ORDER_MOVE_TOP);

    if (createdName != nullptr) {
        *createdName = duplicateName;
    }
    lastError_.clear();
    log(QStringLiteral("Source duplicated in %1: %2 -> %3")
            .arg(scene->name, sourceName, duplicateName));
    return true;
}

bool VuttaraEngine::setSourceVisible(const QString& sourceName, bool visible)
{
    ManagedScene* scene = activeScene();
    ManagedSource* source = scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return operationFail(QStringLiteral("Source not found in the active scene: %1").arg(sourceName));
    }

    runtime_.api().obs_sceneitem_set_visible(source->item, visible);
    source->visible = runtime_.api().obs_sceneitem_visible(source->item);
    lastError_.clear();
    log(QStringLiteral("Source visibility: %1 -> %2")
            .arg(sourceName, source->visible ? QStringLiteral("visible") : QStringLiteral("hidden")));
    return source->visible == visible;
}

bool VuttaraEngine::setSourceLocked(const QString& sourceName, bool locked)
{
    ManagedScene* scene = activeScene();
    ManagedSource* source = scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return operationFail(QStringLiteral("Source not found in the active scene: %1").arg(sourceName));
    }

    runtime_.api().obs_sceneitem_set_locked(source->item, locked);
    source->locked = runtime_.api().obs_sceneitem_locked(source->item);
    lastError_.clear();
    log(QStringLiteral("Source lock: %1 -> %2")
            .arg(sourceName, source->locked ? QStringLiteral("locked") : QStringLiteral("unlocked")));
    return source->locked == locked;
}

bool VuttaraEngine::moveSource(const QString& sourceName, SourceOrderMovement movement)
{
    ManagedScene* scene = activeScene();
    ManagedSource* source = scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return operationFail(QStringLiteral("Source not found in the active scene: %1").arg(sourceName));
    }

    ObsAbi::obs_order_movement nativeMovement = ObsAbi::OBS_ORDER_MOVE_UP;
    switch (movement) {
    case SourceOrderMovement::Up:
        nativeMovement = ObsAbi::OBS_ORDER_MOVE_UP;
        break;
    case SourceOrderMovement::Down:
        nativeMovement = ObsAbi::OBS_ORDER_MOVE_DOWN;
        break;
    case SourceOrderMovement::Top:
        nativeMovement = ObsAbi::OBS_ORDER_MOVE_TOP;
        break;
    case SourceOrderMovement::Bottom:
        nativeMovement = ObsAbi::OBS_ORDER_MOVE_BOTTOM;
        break;
    }

    runtime_.api().obs_sceneitem_set_order(source->item, nativeMovement);
    lastError_.clear();
    log(QStringLiteral("Source order changed in %1: %2 -> movement %3")
            .arg(scene->name, sourceName)
            .arg(static_cast<int>(movement)));
    return true;
}

bool VuttaraEngine::setSourceTransform(const QString& sourceName, const SourceTransform& transform)
{
    ManagedScene* scene = activeScene();
    ManagedSource* source = scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return operationFail(QStringLiteral("Source not found in the active scene: %1").arg(sourceName));
    }
    if (source->locked) {
        return operationFail(QStringLiteral("Unlock %1 before changing its transform.").arg(sourceName));
    }

    return applySourceTransform(*source, transform, false);
}

bool VuttaraEngine::fitSourceToCanvas(const QString& sourceName)
{
    ManagedScene* scene = activeScene();
    ManagedSource* source = scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return operationFail(QStringLiteral("Source not found in the active scene: %1").arg(sourceName));
    }
    if (source->locked) {
        return operationFail(QStringLiteral("Unlock %1 before fitting it to the canvas.").arg(sourceName));
    }

    fitSceneItemToCanvas(source->item);
    lastError_.clear();
    log(QStringLiteral("Source fitted to canvas: %1").arg(sourceName));
    return true;
}

bool VuttaraEngine::centerSource(const QString& sourceName)
{
    ManagedScene* scene = activeScene();
    ManagedSource* source = scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr) {
        return operationFail(QStringLiteral("Source not found in the active scene: %1").arg(sourceName));
    }
    if (source->locked) {
        return operationFail(QStringLiteral("Unlock %1 before centering it.").arg(sourceName));
    }

    SourceTransform transform = sourceTransform(*source);
    transform.x = static_cast<double>(CanvasWidth) / 2.0;
    transform.y = static_cast<double>(CanvasHeight) / 2.0;
    return applySourceTransform(*source, transform, false);
}

bool VuttaraEngine::applySourceProperties(
    const QString& sourceName,
    const QJsonObject& values,
    QString* updatedName)
{
    if (!ready_.load()) {
        return operationFail(QStringLiteral("The engine is not ready to update source properties."));
    }
    if (isRecording()) {
        return operationFail(QStringLiteral(
            "Stop recording before changing source properties."));
    }

    ManagedScene* scene = activeScene();
    ManagedSource* source =
        scene != nullptr ? findSource(*scene, sourceName) : nullptr;
    if (source == nullptr || source->item == nullptr) {
        return operationFail(QStringLiteral(
            "Source not found in the active scene: %1").arg(sourceName));
    }

    QString requestedName = values.value(QStringLiteral("name"))
                                .toString(source->name)
                                .trimmed();
    if (requestedName.isEmpty()) {
        return operationFail(QStringLiteral("Source names cannot be empty."));
    }
    if (requestedName.size() > 80) {
        return operationFail(QStringLiteral("Source names are limited to 80 characters."));
    }
    const bool duplicateName = std::any_of(
        scene->sources.cbegin(),
        scene->sources.cend(),
        [&requestedName, source](const ManagedSource& candidate) {
            return &candidate != source &&
                candidate.name.compare(requestedName, Qt::CaseInsensitive) == 0;
        });
    if (duplicateName) {
        return operationFail(QStringLiteral(
            "Another source in this scene is already named %1.").arg(requestedName));
    }

    const auto& api = runtime_.api();
    ObsAbi::obs_source_t* obsSource = api.obs_sceneitem_get_source(source->item);
    if (obsSource == nullptr) {
        return operationFail(QStringLiteral(
            "libobs did not return the source object for %1.").arg(sourceName));
    }

    ObsAbi::obs_data_t* settings = api.obs_source_get_settings(obsSource);
    if (settings == nullptr) {
        settings = api.obs_data_create();
    }
    if (settings == nullptr) {
        return operationFail(QStringLiteral("Could not allocate source settings."));
    }

    bool recognized = true;
    if (source->type == QStringLiteral("display_capture")) {
        const QString monitorId = values.value(QStringLiteral("monitorId"))
                                      .toString(source->persistentId)
                                      .trimmed();
        if (monitorId.isEmpty()) {
            api.obs_data_release(settings);
            return operationFail(QStringLiteral("Select a valid display."));
        }
        const bool captureCursor = values.value(QStringLiteral("captureCursor"))
                                       .toBool(source->captureCursor);
        const QByteArray monitorUtf8 = monitorId.toUtf8();
        api.obs_data_set_string(settings, "monitor_id", monitorUtf8.constData());
        api.obs_data_set_int(settings, "method", 0);
        api.obs_data_set_bool(settings, "capture_cursor", captureCursor);
        api.obs_data_set_bool(settings, "force_sdr", false);
        source->persistentId = monitorId;
        source->captureCursor = captureCursor;
    } else if (source->type == QStringLiteral("window_capture")) {
        const QString encodedWindow = values.value(QStringLiteral("window"))
                                          .toString(source->persistentId)
                                          .trimmed();
        if (encodedWindow.isEmpty()) {
            api.obs_data_release(settings);
            return operationFail(QStringLiteral("Select a valid application window."));
        }
        const auto method = static_cast<WindowCaptureMethod>(
            values.value(QStringLiteral("method"))
                .toInt(static_cast<int>(source->windowMethod)));
        const auto priority = static_cast<WindowMatchPriority>(
            values.value(QStringLiteral("priority"))
                .toInt(static_cast<int>(source->windowPriority)));
        const bool captureCursor = values.value(QStringLiteral("captureCursor"))
                                       .toBool(source->captureCursor);
        const bool clientArea = values.value(QStringLiteral("clientArea"))
                                    .toBool(source->clientArea);
        const QByteArray windowUtf8 = encodedWindow.toUtf8();
        api.obs_data_set_string(settings, "window", windowUtf8.constData());
        api.obs_data_set_int(settings, "method", static_cast<long long>(method));
        api.obs_data_set_int(settings, "priority", static_cast<long long>(priority));
        api.obs_data_set_bool(settings, "cursor", captureCursor);
        api.obs_data_set_bool(settings, "client_area", clientArea);
        api.obs_data_set_bool(settings, "compatibility", false);
        api.obs_data_set_bool(settings, "force_sdr", false);
        api.obs_data_set_bool(settings, "capture_audio", false);
        source->persistentId = encodedWindow;
        source->windowMethod = method;
        source->windowPriority = priority;
        source->captureCursor = captureCursor;
        source->clientArea = clientArea;
    } else if (source->type == QStringLiteral("color")) {
        const std::uint32_t color = static_cast<std::uint32_t>(
            values.value(QStringLiteral("color"))
                .toDouble(static_cast<double>(source->color)));
        const int width = std::clamp(
            values.value(QStringLiteral("width")).toInt(source->sourceWidth),
            1,
            7680);
        const int height = std::clamp(
            values.value(QStringLiteral("height")).toInt(source->sourceHeight),
            1,
            7680);
        api.obs_data_set_int(settings, "color", static_cast<long long>(color));
        api.obs_data_set_int(settings, "width", width);
        api.obs_data_set_int(settings, "height", height);
        source->color = color;
        source->sourceWidth = width;
        source->sourceHeight = height;
    } else {
        recognized = false;
    }

    if (!recognized) {
        api.obs_data_release(settings);
        return operationFail(QStringLiteral(
            "No source-properties adapter is registered for %1.")
                                 .arg(source->type));
    }

    api.obs_source_update(obsSource, settings);
    api.obs_data_release(settings);

    if (requestedName != source->name && source->removable) {
        const QByteArray nameUtf8 = requestedName.toUtf8();
        api.obs_source_set_name(obsSource, nameUtf8.constData());
        const char* actualName = api.obs_source_get_name(obsSource);
        source->name = actualName != nullptr
            ? QString::fromUtf8(actualName)
            : requestedName;
    }

    if (updatedName != nullptr) {
        *updatedName = source->name;
    }

    lastError_.clear();
    log(QStringLiteral("Source properties updated: %1 (%2)")
            .arg(source->name, source->type));
    return true;
}

bool VuttaraEngine::setAudioDevice(AudioChannelKind kind, const AudioDeviceInfo& device)
{
    if (!ready_.load()) {
        return operationFail(QStringLiteral("The engine is not ready to connect an audio device."));
    }
    if (device.deviceId.trimmed().isEmpty()) {
        return disconnectAudioDevice(kind);
    }

    AudioChannelState& channel = audioChannel(kind);
    return createAudioChannel(channel, device);
}

bool VuttaraEngine::disconnectAudioDevice(AudioChannelKind kind)
{
    if (!runtime_.isLoaded()) {
        return operationFail(QStringLiteral("libobs is not loaded."));
    }

    AudioChannelState& channel = audioChannel(kind);
    releaseAudioChannel(channel);
    lastError_.clear();
    log(QStringLiteral("Audio channel disconnected: %1").arg(channel.name));
    return true;
}

bool VuttaraEngine::setAudioVolume(AudioChannelKind kind, int volumePercent)
{
    AudioChannelState& channel = audioChannel(kind);
    channel.volumePercent = std::clamp(volumePercent, 0, 100);
    if (channel.source == nullptr || channel.fader == nullptr) {
        lastError_.clear();
        return true;
    }

    const float deflection = static_cast<float>(channel.volumePercent) / 100.0F;
    runtime_.api().obs_fader_set_deflection(channel.fader, deflection);
    lastError_.clear();
    return true;
}

bool VuttaraEngine::setAudioMuted(AudioChannelKind kind, bool muted)
{
    AudioChannelState& channel = audioChannel(kind);
    channel.muted = muted;
    if (channel.source != nullptr) {
        runtime_.api().obs_source_set_muted(channel.source, muted);
        channel.muted = runtime_.api().obs_source_muted(channel.source);
    }
    lastError_.clear();
    return true;
}

QVector<RecordingEncoderInfo> VuttaraEngine::availableRecordingEncoders() const
{
    QVector<RecordingEncoderInfo> result;
    if (!ready_.load()) {
        return result;
    }

    const auto& api = runtime_.api();
    std::unordered_set<std::string> registered;
    for (std::size_t index = 0;; ++index) {
        const char* id = nullptr;
        if (!api.obs_enum_encoder_types(index, &id)) {
            break;
        }
        if (id != nullptr) {
            registered.emplace(id);
        }
    }

    const struct Candidate {
        const char* id;
        const char* fallbackName;
        bool hardware;
        int priority;
    } candidates[] = {
        {"obs_nvenc_h264_tex", "NVIDIA NVENC H.264", true, 400},
        {"h264_texture_amf", "AMD AMF H.264", true, 300},
        {"obs_qsv11_v2", "Intel Quick Sync H.264", true, 200},
        {"obs_x264", "x264 Software H.264", false, 100},
    };

    for (const Candidate& candidate : candidates) {
        if (!registered.contains(candidate.id)) {
            continue;
        }

        const char* codec = api.obs_get_encoder_codec(candidate.id);
        if (codec == nullptr || QString::fromLatin1(codec).compare(
                QStringLiteral("h264"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        const char* displayName = api.obs_encoder_get_display_name(candidate.id);
        result.append(RecordingEncoderInfo{
            QString::fromLatin1(candidate.id),
            displayName != nullptr && *displayName != '\0'
                ? QString::fromUtf8(displayName)
                : QString::fromLatin1(candidate.fallbackName),
            candidate.hardware,
            candidate.priority,
        });
    }

    return result;
}

bool VuttaraEngine::startStreaming(const StreamingSettings& requestedSettings)
{
    finalizeStreamingIfStopped();
    if (!ready_.load()) {
        return operationFail(QStringLiteral("The engine is not ready to stream."));
    }
    if (streamingOutput_ != nullptr) {
        return operationFail(QStringLiteral("A stream is already active, reconnecting, or stopping."));
    }

    streamingInfo_ = {};
    activeStreamingSettings_ = normalizedStreamingSettings(requestedSettings);
    if (activeStreamingSettings_.server.isEmpty()) {
        return operationFail(QStringLiteral("Enter a valid RTMP or RTMPS server URL."));
    }
    if (activeStreamingSettings_.streamKey.isEmpty()) {
        return operationFail(QStringLiteral("Enter a stream key."));
    }

    QVector<RecordingEncoderInfo> encoders = availableRecordingEncoders();
    if (!activeStreamingSettings_.encoderId.isEmpty()) {
        std::stable_sort(encoders.begin(), encoders.end(), [this](const auto& left, const auto& right) {
            return left.id == activeStreamingSettings_.encoderId && right.id != activeStreamingSettings_.encoderId;
        });
    }

    QStringList failures;
    for (const RecordingEncoderInfo& encoder : encoders) {
        QString failure;
        if (tryStartStreamingWithEncoder(encoder, activeStreamingSettings_, &failure)) {
            streamingInfo_.state = StreamingState::Connecting;
            streamingInfo_.active = true;
            streamingInfo_.reconnecting = false;
            streamingInfo_.stopping = false;
            streamingInfo_.server = activeStreamingSettings_.server;
            streamingInfo_.encoderId = encoder.id;
            streamingInfo_.encoderName = encoder.name;
            streamingInfo_.outputWidth = activeStreamingSettings_.outputWidth;
            streamingInfo_.outputHeight = activeStreamingSettings_.outputHeight;
            streamingInfo_.framesPerSecond = activeStreamingSettings_.framesPerSecond;
            streamingInfo_.videoBitrateKbps = activeStreamingSettings_.videoBitrateKbps;
            streamingInfo_.keyframeIntervalSeconds = activeStreamingSettings_.keyframeIntervalSeconds;
            streamingInfo_.audioBitrateKbps = activeStreamingSettings_.audioBitrateKbps;
            streamingInfo_.diagnostics = QStringLiteral(
                "%1x%2 @ %3 FPS | video %4 Kbps | keyframe %5 s | audio %6 Kbps | encoder %7 [%8] | reconnect %9/%10 s")
                .arg(streamingInfo_.outputWidth)
                .arg(streamingInfo_.outputHeight)
                .arg(streamingInfo_.framesPerSecond)
                .arg(streamingInfo_.videoBitrateKbps)
                .arg(streamingInfo_.keyframeIntervalSeconds)
                .arg(streamingInfo_.audioBitrateKbps)
                .arg(streamingInfo_.encoderName)
                .arg(streamingInfo_.encoderId)
                .arg(activeStreamingSettings_.maximumRetries)
                .arg(activeStreamingSettings_.retryDelaySeconds);
            streamingStartedAt_ = std::chrono::steady_clock::now();
            streamingStatsUpdatedAt_ = streamingStartedAt_;
            streamingStatsLastBytes_ = 0;
            lastError_.clear();
            log(QStringLiteral("Streaming connection started: %1 | %2")
                    .arg(streamingInfo_.server, streamingInfo_.diagnostics));
            return true;
        }
        failures << QStringLiteral("%1: %2").arg(encoder.name, failure);
    }
    return operationFail(QStringLiteral("Streaming could not start. %1")
                             .arg(failures.join(QStringLiteral(" | "))));
}

bool VuttaraEngine::stopStreaming()
{
    finalizeStreamingIfStopped();
    if (streamingOutput_ == nullptr) {
        return operationFail(QStringLiteral("No stream is active."));
    }
    if (streamingInfo_.stopping) {
        lastError_.clear();
        return true;
    }
    streamingInfo_.state = StreamingState::Stopping;
    streamingInfo_.stopping = true;
    runtime_.api().obs_output_stop(streamingOutput_);
    lastError_.clear();
    log(QStringLiteral("Streaming stop requested."));
    return true;
}

void VuttaraEngine::forceStopStreaming()
{
    if (streamingOutput_ == nullptr || !runtime_.isLoaded()) {
        return;
    }
    streamingInfo_.state = StreamingState::Stopping;
    streamingInfo_.stopping = true;
    runtime_.api().obs_output_force_stop(streamingOutput_);
    finalizeStreamingIfStopped();
}

StreamingInfo VuttaraEngine::streamingInfo()
{
    finalizeStreamingIfStopped();
    if (streamingOutput_ != nullptr) {
        const auto& api = runtime_.api();
        const bool reconnecting = api.obs_output_reconnecting(streamingOutput_);
        const bool active = api.obs_output_active(streamingOutput_);
        streamingInfo_.active = active || reconnecting;
        streamingInfo_.reconnecting = reconnecting;
        streamingInfo_.state = streamingInfo_.stopping
            ? StreamingState::Stopping
            : reconnecting ? StreamingState::Reconnecting
            : active ? StreamingState::Streaming
            : StreamingState::Connecting;
        streamingInfo_.totalBytes = api.obs_output_get_total_bytes(streamingOutput_);
        streamingInfo_.droppedFrames = api.obs_output_get_frames_dropped(streamingOutput_);
        streamingInfo_.totalFrames = api.obs_output_get_total_frames(streamingOutput_);
        streamingInfo_.congestion = std::clamp<double>(api.obs_output_get_congestion(streamingOutput_), 0.0, 1.0);
        streamingInfo_.connectTimeMilliseconds = api.obs_output_get_connect_time_ms(streamingOutput_);
        const auto now = std::chrono::steady_clock::now();
        streamingInfo_.elapsedMilliseconds = static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - streamingStartedAt_).count());
        const qint64 sampleMilliseconds = static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - streamingStatsUpdatedAt_).count());
        if (sampleMilliseconds >= 250) {
            const std::uint64_t byteDelta = streamingInfo_.totalBytes >= streamingStatsLastBytes_
                ? streamingInfo_.totalBytes - streamingStatsLastBytes_
                : 0;
            streamingInfo_.currentBitrateKbps = static_cast<double>(byteDelta) * 8.0 / static_cast<double>(sampleMilliseconds);
            streamingStatsLastBytes_ = streamingInfo_.totalBytes;
            streamingStatsUpdatedAt_ = now;
        }
    }
    return streamingInfo_;
}

bool VuttaraEngine::isStreaming() const
{
    return streamingOutput_ != nullptr;
}

bool VuttaraEngine::startRecording(const RecordingSettings& requestedSettings)
{
    finalizeRecordingIfStopped();
    if (!ready_.load()) {
        return operationFail(QStringLiteral("The engine is not ready to record."));
    }
    if (recordingOutput_ != nullptr) {
        return operationFail(QStringLiteral("A recording is already active or finalizing."));
    }

    recordingInfo_ = {};
    activeRecordingSettings_ = normalizedRecordingSettings(requestedSettings);

    if (activeRecordingSettings_.outputDirectory.isEmpty()) {
        return operationFail(QStringLiteral("Choose a recording folder first."));
    }

    QDir directory(activeRecordingSettings_.outputDirectory);
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        return operationFail(QStringLiteral("Could not create the recording folder: %1")
                                 .arg(directory.absolutePath()));
    }

    const QFileInfo directoryInfo(directory.absolutePath());
    if (!directoryInfo.isDir() || !directoryInfo.isWritable()) {
        return operationFail(QStringLiteral("The recording folder is not writable: %1")
                                 .arg(directory.absolutePath()));
    }

    QStorageInfo storage(directory.absolutePath());
    storage.refresh();
    constexpr std::uint64_t MinimumFreeBytes = 256ULL * 1024ULL * 1024ULL;
    if (
        storage.isValid() &&
        storage.isReady() &&
        static_cast<std::uint64_t>(std::max<qint64>(0, storage.bytesAvailable())) < MinimumFreeBytes) {
        return operationFail(QStringLiteral(
            "The recording drive has less than 256 MB available. Free space before recording."));
    }

    const QString outputPath = uniqueRecordingPath(activeRecordingSettings_);
    QVector<RecordingEncoderInfo> encoders = availableRecordingEncoders();
    if (encoders.isEmpty()) {
        return operationFail(QStringLiteral("No compatible H.264 recording encoder is registered."));
    }

    if (!activeRecordingSettings_.encoderId.isEmpty()) {
        const auto requested = std::find_if(
            encoders.cbegin(),
            encoders.cend(),
            [this](const RecordingEncoderInfo& encoder) {
                return encoder.id == activeRecordingSettings_.encoderId;
            });
        if (requested == encoders.cend()) {
            return operationFail(QStringLiteral("The selected recording encoder is unavailable: %1")
                                     .arg(activeRecordingSettings_.encoderId));
        }
        const RecordingEncoderInfo selected = *requested;
        encoders.clear();
        encoders.append(selected);
    }

    QStringList failures;
    for (const RecordingEncoderInfo& encoder : encoders) {
        QString failure;
        if (tryStartRecordingWithEncoder(
                encoder,
                activeRecordingSettings_,
                outputPath,
                &failure)) {
            recordingInfo_.state = RecordingState::Recording;
            recordingInfo_.active = true;
            recordingInfo_.stopping = false;
            recordingInfo_.outputPath = outputPath;
            recordingInfo_.encoderId = encoder.id;
            recordingInfo_.encoderName = encoder.name;
            recordingInfo_.outputWidth = activeRecordingSettings_.outputWidth;
            recordingInfo_.outputHeight = activeRecordingSettings_.outputHeight;
            recordingInfo_.framesPerSecond = activeRecordingSettings_.framesPerSecond;
            recordingInfo_.videoBitrateKbps = activeRecordingSettings_.videoBitrateKbps;
            recordingInfo_.audioBitrateKbps = activeRecordingSettings_.audioBitrateKbps;
            recordingInfo_.elapsedMilliseconds = 0;
            recordingInfo_.totalBytes = 0;
            recordingInfo_.diagnostics = QStringLiteral(
                "%1x%2 @ %3 FPS | video %4 Kbps | audio %5 Kbps | encoder %6 [%7] | estimated %8 MB/hour")
                .arg(activeRecordingSettings_.outputWidth)
                .arg(activeRecordingSettings_.outputHeight)
                .arg(activeRecordingSettings_.framesPerSecond)
                .arg(activeRecordingSettings_.videoBitrateKbps)
                .arg(activeRecordingSettings_.audioBitrateKbps)
                .arg(encoder.name)
                .arg(encoder.id)
                .arg(estimatedBytes(activeRecordingSettings_, 3600) / (1024ULL * 1024ULL));
            recordingInfo_.error.clear();
            recordingStartedAt_ = std::chrono::steady_clock::now();
            lastError_.clear();
            log(QStringLiteral("Recording started: %1 | %2")
                    .arg(outputPath, recordingInfo_.diagnostics));
            return true;
        }
        failures << QStringLiteral("%1: %2").arg(encoder.name, failure);
    }

    QFile::remove(outputPath);
    return operationFail(QStringLiteral("Recording could not start with the selected settings. %1")
                             .arg(failures.join(QStringLiteral(" | "))));
}
bool VuttaraEngine::stopRecording()
{
    finalizeRecordingIfStopped();
    if (recordingOutput_ == nullptr) {
        return operationFail(QStringLiteral("No recording is active."));
    }
    if (recordingInfo_.stopping) {
        lastError_.clear();
        return true;
    }

    recordingInfo_.state = RecordingState::Stopping;
    recordingInfo_.stopping = true;
    runtime_.api().obs_output_stop(recordingOutput_);
    lastError_.clear();
    log(QStringLiteral("Recording stop requested: %1").arg(recordingInfo_.outputPath));
    return true;
}

void VuttaraEngine::forceStopRecording()
{
    if (recordingOutput_ == nullptr || !runtime_.isLoaded()) {
        return;
    }

    recordingInfo_.state = RecordingState::Stopping;
    recordingInfo_.stopping = true;
    runtime_.api().obs_output_force_stop(recordingOutput_);
    finalizeRecordingIfStopped();
}

RecordingInfo VuttaraEngine::recordingInfo()
{
    finalizeRecordingIfStopped();
    if (recordingOutput_ != nullptr) {
        recordingInfo_.active = runtime_.api().obs_output_active(recordingOutput_);
        recordingInfo_.totalBytes = runtime_.api().obs_output_get_total_bytes(recordingOutput_);
        recordingInfo_.elapsedMilliseconds = static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - recordingStartedAt_)
                .count());
    }
    return recordingInfo_;
}

bool VuttaraEngine::isRecording() const
{
    return recordingOutput_ != nullptr;
}

QJsonObject VuttaraEngine::projectState() const
{
    QJsonArray scenesArray;

    for (const ManagedScene& scene : scenes_) {
        QJsonArray sourcesArray;
        QVector<SourceInfo> orderedSources;
        orderedSources.reserve(scene.sources.size());
        for (const ManagedSource& source : scene.sources) {
            orderedSources.append(sourceInfo(source));
        }
        std::sort(
            orderedSources.begin(),
            orderedSources.end(),
            [](const SourceInfo& left, const SourceInfo& right) {
                return left.orderPosition < right.orderPosition;
            });

        for (const SourceInfo& info : orderedSources) {
            const ManagedSource* source = findSource(scene, info.name);
            if (source == nullptr) {
                continue;
            }

            QJsonObject transformObject;
            transformObject.insert(QStringLiteral("x"), info.transform.x);
            transformObject.insert(QStringLiteral("y"), info.transform.y);
            transformObject.insert(QStringLiteral("width"), info.transform.width);
            transformObject.insert(QStringLiteral("height"), info.transform.height);
            transformObject.insert(QStringLiteral("rotation"), info.transform.rotation);
            transformObject.insert(QStringLiteral("cropLeft"), info.transform.cropLeft);
            transformObject.insert(QStringLiteral("cropTop"), info.transform.cropTop);
            transformObject.insert(QStringLiteral("cropRight"), info.transform.cropRight);
            transformObject.insert(QStringLiteral("cropBottom"), info.transform.cropBottom);
            transformObject.insert(QStringLiteral("flipHorizontal"), info.transform.flipHorizontal);
            transformObject.insert(QStringLiteral("flipVertical"), info.transform.flipVertical);
            transformObject.insert(QStringLiteral("stretchToBounds"), info.transform.stretchToBounds);

            QJsonObject sourceObject;
            sourceObject.insert(QStringLiteral("name"), source->name);
            sourceObject.insert(QStringLiteral("type"), source->type);
            sourceObject.insert(QStringLiteral("persistentId"), source->persistentId);
            sourceObject.insert(QStringLiteral("visible"), info.visible);
            sourceObject.insert(QStringLiteral("removable"), source->removable);
            sourceObject.insert(QStringLiteral("locked"), info.locked);
            sourceObject.insert(QStringLiteral("orderPosition"), info.orderPosition);
            sourceObject.insert(QStringLiteral("captureCursor"), source->captureCursor);
            sourceObject.insert(QStringLiteral("clientArea"), source->clientArea);
            sourceObject.insert(QStringLiteral("windowMethod"), static_cast<int>(source->windowMethod));
            sourceObject.insert(QStringLiteral("windowPriority"), static_cast<int>(source->windowPriority));
            sourceObject.insert(QStringLiteral("color"), static_cast<double>(source->color));
            sourceObject.insert(QStringLiteral("sourceWidth"), info.sourceWidth);
            sourceObject.insert(QStringLiteral("sourceHeight"), info.sourceHeight);
            sourceObject.insert(QStringLiteral("transform"), transformObject);
            sourcesArray.append(sourceObject);
        }

        QJsonObject sceneObject;
        sceneObject.insert(QStringLiteral("name"), scene.name);
        sceneObject.insert(QStringLiteral("removable"), scene.removable);
        sceneObject.insert(QStringLiteral("sources"), sourcesArray);
        scenesArray.append(sceneObject);
    }

    auto audioStateObject = [this](const AudioChannelState& channel) {
        const AudioChannelInfo info = audioChannelInfo(channel);
        QJsonObject state;
        state.insert(QStringLiteral("connected"), info.connected);
        state.insert(QStringLiteral("deviceId"), info.deviceId);
        state.insert(QStringLiteral("deviceName"), info.deviceName);
        state.insert(QStringLiteral("volumePercent"), info.volumePercent);
        state.insert(QStringLiteral("muted"), info.muted);
        return state;
    };

    QJsonObject audioObject;
    audioObject.insert(QStringLiteral("desktop"), audioStateObject(desktopAudio_));
    audioObject.insert(QStringLiteral("microphone"), audioStateObject(microphoneAudio_));

    QJsonObject project;
    project.insert(QStringLiteral("schemaVersion"), 3);
    project.insert(QStringLiteral("applicationVersion"), QStringLiteral("0.0.1"));
    project.insert(QStringLiteral("stage"), QStringLiteral("sources-organization-preview-selection-stage8c-v1"));
    project.insert(QStringLiteral("activeScene"), activeSceneName());
    project.insert(QStringLiteral("scenes"), scenesArray);
    project.insert(QStringLiteral("audio"), audioObject);
    return project;
}

bool VuttaraEngine::restoreProjectState(const QJsonObject& project)
{
    if (!ready_.load()) {
        return operationFail(QStringLiteral("The engine is not ready to restore a project."));
    }
    const int schemaVersion = project.value(QStringLiteral("schemaVersion")).toInt();
    if (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != 3) {
        return operationFail(QStringLiteral("Unsupported Vuttara project schema."));
    }

    const QJsonArray sceneArray = project.value(QStringLiteral("scenes")).toArray();
    if (sceneArray.isEmpty() || sceneArray.size() > MaximumScenes) {
        return operationFail(QStringLiteral("The project scene list is empty or exceeds the Stage 5A limit."));
    }

    releaseAllAudioChannels();
    releaseAllScenes();

    bool mainSceneCreated = false;
    for (const QJsonValue& sceneValue : sceneArray) {
        const QJsonObject sceneObject = sceneValue.toObject();
        QString sceneName = sceneObject.value(QStringLiteral("name")).toString().trimmed();
        if (sceneName.isEmpty()) {
            continue;
        }

        const bool isMain = sceneName.compare(QStringLiteral("Main Scene"), Qt::CaseInsensitive) == 0;
        if (isMain) {
            sceneName = QStringLiteral("Main Scene");
            mainSceneCreated = true;
        } else {
            sceneName = uniqueSceneName(sceneName);
        }

        if (!createSceneInternal(sceneName, !isMain, isMain)) {
            resetProjectToDefault();
            return operationFail(QStringLiteral("Could not restore scene: %1").arg(sceneName));
        }

        ManagedScene& scene = scenes_.last();
        const QJsonArray sourcesArray = sceneObject.value(QStringLiteral("sources")).toArray();
        for (const QJsonValue& sourceValue : sourcesArray) {
            const QJsonObject sourceObject = sourceValue.toObject();
            const QString type = sourceObject.value(QStringLiteral("type")).toString();
            const QString sourceName = sourceObject.value(QStringLiteral("name")).toString();
            const QString persistentId = sourceObject.value(QStringLiteral("persistentId")).toString();
            const bool captureCursor = sourceObject.value(QStringLiteral("captureCursor")).toBool(true);
            QString createdName;
            bool created = false;

            if (type == QStringLiteral("color")) {
                const bool removable = sourceObject.value(QStringLiteral("removable")).toBool(false);
                const std::uint32_t color = static_cast<std::uint32_t>(
                    sourceObject.value(QStringLiteral("color")).toDouble(TestSourceColor));
                const int width = sourceObject.value(QStringLiteral("sourceWidth")).toInt(CanvasWidth);
                const int height = sourceObject.value(QStringLiteral("sourceHeight")).toInt(CanvasHeight);
                if (!removable) {
                    const auto existingColor = std::find_if(
                        scene.sources.begin(),
                        scene.sources.end(),
                        [](const ManagedSource& candidate) {
                            return candidate.type == QStringLiteral("color") && !candidate.removable;
                        });
                    if (existingColor != scene.sources.end()) {
                        created = true;
                        createdName = existingColor->name;
                        QJsonObject colorValues;
                        colorValues.insert(QStringLiteral("name"), sourceName);
                        colorValues.insert(QStringLiteral("color"), static_cast<double>(color));
                        colorValues.insert(QStringLiteral("width"), width);
                        colorValues.insert(QStringLiteral("height"), height);
                        if (!applySourceProperties(
                                existingColor->name,
                                colorValues,
                                &createdName)) {
                            created = false;
                        }
                    }
                } else {
                    created = addColorSourceToScene(
                        scene,
                        sourceName,
                        color,
                        width,
                        height,
                        true,
                        false,
                        &createdName);
                }
            } else if (type == QStringLiteral("display_capture")) {
                DisplayInfo display;
                display.monitorId = persistentId;
                display.description = QStringLiteral("saved Windows display");
                created = addDisplayCaptureToScene(
                    scene,
                    display,
                    captureCursor,
                    sourceName,
                    false,
                    &createdName);
            } else if (type == QStringLiteral("window_capture")) {
                WindowInfo window;
                window.encodedValue = persistentId;
                window.description = QStringLiteral("saved Windows application window");
                const auto method = static_cast<WindowCaptureMethod>(
                    sourceObject.value(QStringLiteral("windowMethod")).toInt(0));
                const auto priority = static_cast<WindowMatchPriority>(
                    sourceObject.value(QStringLiteral("windowPriority")).toInt(1));
                created = addWindowCaptureToScene(
                    scene,
                    window,
                    method,
                    priority,
                    captureCursor,
                    sourceObject.value(QStringLiteral("clientArea")).toBool(true),
                    sourceName,
                    false,
                    &createdName);
            }

            if (!created) {
                log(QStringLiteral("Project restore skipped source %1: %2").arg(sourceName, lastError_));
                lastError_.clear();
                continue;
            }

            ManagedSource* source = findSource(scene, createdName);
            if (source == nullptr) {
                continue;
            }

            runtime_.api().obs_sceneitem_set_visible(
                source->item,
                sourceObject.value(QStringLiteral("visible")).toBool(true));
            source->visible = runtime_.api().obs_sceneitem_visible(source->item);

            const QJsonObject transformObject = sourceObject.value(QStringLiteral("transform")).toObject();
            SourceTransform transform;
            transform.x = transformObject.value(QStringLiteral("x")).toDouble(960.0);
            transform.y = transformObject.value(QStringLiteral("y")).toDouble(540.0);
            transform.width = transformObject.value(QStringLiteral("width")).toDouble(1920.0);
            transform.height = transformObject.value(QStringLiteral("height")).toDouble(1080.0);
            transform.rotation = transformObject.value(QStringLiteral("rotation")).toDouble(0.0);
            if (schemaVersion >= 3) {
                transform.cropLeft = transformObject.value(QStringLiteral("cropLeft")).toDouble(0.0);
                transform.cropTop = transformObject.value(QStringLiteral("cropTop")).toDouble(0.0);
                transform.cropRight = transformObject.value(QStringLiteral("cropRight")).toDouble(0.0);
                transform.cropBottom = transformObject.value(QStringLiteral("cropBottom")).toDouble(0.0);
                transform.flipHorizontal = transformObject.value(QStringLiteral("flipHorizontal")).toBool(false);
                transform.flipVertical = transformObject.value(QStringLiteral("flipVertical")).toBool(false);
                transform.stretchToBounds = transformObject.value(QStringLiteral("stretchToBounds")).toBool(false);
            }
            applySourceTransform(*source, transform, true);

            runtime_.api().obs_sceneitem_set_order_position(
                source->item,
                sourceObject.value(QStringLiteral("orderPosition")).toInt(0));
            runtime_.api().obs_sceneitem_set_locked(
                source->item,
                sourceObject.value(QStringLiteral("locked")).toBool(false));
            source->locked = runtime_.api().obs_sceneitem_locked(source->item);
        }
    }

    if (!mainSceneCreated) {
        if (!createSceneInternal(QStringLiteral("Main Scene"), false, true)) {
            return operationFail(QStringLiteral("Could not recover the required Main Scene."));
        }
    }

    const QString requestedActive = project.value(QStringLiteral("activeScene")).toString();
    if (!switchScene(requestedActive)) {
        lastError_.clear();
        switchScene(QStringLiteral("Main Scene"));
    }

    if (schemaVersion >= 2) {
        const QJsonObject audioObject = project.value(QStringLiteral("audio")).toObject();
        restoreAudioChannel(AudioChannelKind::Desktop, audioObject.value(QStringLiteral("desktop")).toObject());
        restoreAudioChannel(AudioChannelKind::Microphone, audioObject.value(QStringLiteral("microphone")).toObject());
    }

    lastError_.clear();
    log(QStringLiteral("Project state restored: %1 scene(s), active=%2, audio schema=%3")
            .arg(scenes_.size())
            .arg(activeSceneName())
            .arg(schemaVersion));
    return true;
}

bool VuttaraEngine::resetProjectToDefault()
{
    if (!runtime_.isLoaded() || runtime_.api().obs_initialized == nullptr || !runtime_.api().obs_initialized()) {
        return operationFail(QStringLiteral("libobs is not initialized."));
    }

    releaseAllAudioChannels();
    releaseAllScenes();
    if (!createSceneInternal(QStringLiteral("Main Scene"), false, true)) {
        return operationFail(QStringLiteral("Could not reset the default Main Scene."));
    }

    activeSceneIndex_ = 0;
    runtime_.api().obs_set_output_source(0, scenes_.first().source);
    lastError_.clear();
    log(QStringLiteral("Project reset to the default Main Scene."));
    return true;
}

bool VuttaraEngine::isReady() const
{
    return ready_.load();
}

bool VuttaraEngine::isInitialized() const
{
    return runtime_.isLoaded() && runtime_.api().obs_initialized != nullptr && runtime_.api().obs_initialized();
}

bool VuttaraEngine::nativeLoggingActive() const
{
    return nativeLoggingActive_.load();
}

QString VuttaraEngine::versionString() const
{
    return versionString_;
}

QString VuttaraEngine::graphicsDescription() const
{
    return isReady() ? graphicsDescription_ : QStringLiteral("Unavailable");
}

QString VuttaraEngine::audioDescription() const
{
    if (!isReady()) {
        return QStringLiteral("Unavailable");
    }

    const AudioChannelInfo desktop = audioChannelInfo(desktopAudio_);
    const AudioChannelInfo microphone = audioChannelInfo(microphoneAudio_);
    return QStringLiteral("48 kHz stereo | Desktop: %1 | Mic: %2")
        .arg(desktop.connected ? desktop.deviceName : QStringLiteral("disabled"))
        .arg(microphone.connected ? microphone.deviceName : QStringLiteral("disabled"));
}

QString VuttaraEngine::lastError() const
{
    return lastError_;
}

void VuttaraEngine::renderPreview(void* context, std::uint32_t width, std::uint32_t height)
{
    auto* engine = static_cast<VuttaraEngine*>(context);
    if (engine != nullptr) {
        engine->render(width, height);
    }
}

void VuttaraEngine::libobsLogCallback(int level, const char* format, va_list arguments, void* context)
{
    auto* engine = static_cast<VuttaraEngine*>(context);
    if (engine != nullptr) {
        engine->appendNativeLog(level, formatNativeMessage(format, arguments));
    }
}

void VuttaraEngine::audioMeterCallback(
    void* context,
    const float[ObsAbi::MaxAudioChannels],
    const float peak[ObsAbi::MaxAudioChannels],
    const float[ObsAbi::MaxAudioChannels])
{
    auto* channel = static_cast<AudioChannelState*>(context);
    if (channel == nullptr) {
        return;
    }

    float maximum = -std::numeric_limits<float>::infinity();
    for (int index = 0; index < ObsAbi::MaxAudioChannels; ++index) {
        if (std::isfinite(peak[index])) {
            maximum = std::max(maximum, peak[index]);
        }
    }
    channel->peakDb.store(maximum, std::memory_order_relaxed);
}

void VuttaraEngine::render(std::uint32_t width, std::uint32_t height)
{
    const ManagedScene* scene = activeScene();
    if (!ready_.load() || scene == nullptr || scene->source == nullptr || width == 0 || height == 0) {
        return;
    }

    const double scale = std::min(
        static_cast<double>(width) / static_cast<double>(CanvasWidth),
        static_cast<double>(height) / static_cast<double>(CanvasHeight));
    const int scaledWidth = std::max(1, static_cast<int>(std::lround(CanvasWidth * scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::lround(CanvasHeight * scale)));
    const int offsetX = (static_cast<int>(width) - scaledWidth) / 2;
    const int offsetY = (static_cast<int>(height) - scaledHeight) / 2;

    const auto& api = runtime_.api();
    api.gs_viewport_push();
    api.gs_projection_push();
    api.gs_set_viewport(offsetX, offsetY, scaledWidth, scaledHeight);
    api.gs_ortho(0.0F, static_cast<float>(CanvasWidth), 0.0F, static_cast<float>(CanvasHeight), -100.0F, 100.0F);
    api.gs_matrix_push();
    api.gs_matrix_identity();
    api.obs_source_video_render(scene->source);
    api.gs_matrix_pop();
    api.gs_projection_pop();
    api.gs_viewport_pop();
}

void VuttaraEngine::appendNativeLog(int level, const QString& message) const
{
    const QString prefix = QStringLiteral("libobs [%1]: ").arg(nativeLevelName(level));
    QString normalized = message;
    normalized.remove('\r');
    const QStringList lines = normalized.split('\n', Qt::SkipEmptyParts);

    std::scoped_lock lock(logMutex_);
    if (lines.isEmpty()) {
        AppPaths::appendLogLine(logPath_, prefix + QStringLiteral("(empty message)"));
        return;
    }
    for (const QString& line : lines) {
        AppPaths::appendLogLine(logPath_, prefix + line);
    }
}

void VuttaraEngine::log(const QString& message) const
{
    std::scoped_lock lock(logMutex_);
    AppPaths::appendLogLine(logPath_, message);
}

bool VuttaraEngine::fail(const QString& message)
{
    lastError_ = message;
    log(QStringLiteral("ENGINE FAILURE: %1").arg(message));
    shutdown();
    lastError_ = message;
    return false;
}

bool VuttaraEngine::operationFail(const QString& message)
{
    lastError_ = message;
    log(QStringLiteral("SOURCE OPERATION FAILURE: %1").arg(message));
    return false;
}

bool VuttaraEngine::validateCoreData(const QString& coreDataPath)
{
    if (!QFileInfo(coreDataPath).isDir()) {
        return fail(QStringLiteral("libobs core data directory is missing: %1").arg(coreDataPath));
    }

    const QStringList requiredEffects = {
        QStringLiteral("default.effect"),
        QStringLiteral("opaque.effect"),
        QStringLiteral("solid.effect"),
        QStringLiteral("repeat.effect"),
        QStringLiteral("format_conversion.effect"),
        QStringLiteral("bicubic_scale.effect"),
        QStringLiteral("lanczos_scale.effect"),
        QStringLiteral("area.effect"),
        QStringLiteral("bilinear_lowres_scale.effect"),
        QStringLiteral("premultiplied_alpha.effect"),
    };

    QStringList missing;
    const QDir coreDataDirectory(coreDataPath);
    for (const QString& effect : requiredEffects) {
        if (!QFileInfo(coreDataDirectory.filePath(effect)).isFile()) {
            missing << effect;
        }
    }

    if (!missing.isEmpty()) {
        return fail(QStringLiteral("Required libobs effect files are missing from %1: %2")
                        .arg(coreDataPath, missing.join(QStringLiteral(", "))));
    }

    log(QStringLiteral("Settings and Source Properties Stage 7A: verified %1 required libobs effect files.")
            .arg(requiredEffects.size()));
    return true;
}

bool VuttaraEngine::loadModule(
    const QString& moduleFileName,
    const QString& moduleDataDirectory,
    ObsAbi::obs_module_t** module)
{
    const QString modulePath = QDir(runtime_.pluginDirectory()).filePath(moduleFileName);
    const QString dataPath = QDir(runtime_.dataDirectory()).filePath(moduleDataDirectory);

    if (!QFileInfo(modulePath).isFile()) {
        return fail(QStringLiteral("Required libobs module is missing: %1").arg(modulePath));
    }
    if (!QFileInfo(dataPath).isDir()) {
        return fail(QStringLiteral("Required libobs module data directory is missing: %1").arg(dataPath));
    }

    QByteArray* modulePathStorage = nullptr;
    QByteArray* moduleDataStorage = nullptr;

    if (moduleFileName == QStringLiteral("image-source.dll")) {
        modulePathStorage = &imageModulePathUtf8_;
        moduleDataStorage = &imageModuleDataPathUtf8_;
    } else if (moduleFileName == QStringLiteral("win-capture.dll")) {
        modulePathStorage = &winCaptureModulePathUtf8_;
        moduleDataStorage = &winCaptureModuleDataPathUtf8_;
    } else if (moduleFileName == QStringLiteral("win-wasapi.dll")) {
        modulePathStorage = &wasapiModulePathUtf8_;
        moduleDataStorage = &wasapiModuleDataPathUtf8_;
    } else if (moduleFileName == QStringLiteral("obs-ffmpeg.dll")) {
        modulePathStorage = &ffmpegModulePathUtf8_;
        moduleDataStorage = &ffmpegModuleDataPathUtf8_;
    } else if (moduleFileName == QStringLiteral("obs-x264.dll")) {
        modulePathStorage = &x264ModulePathUtf8_;
        moduleDataStorage = &x264ModuleDataPathUtf8_;
    } else if (moduleFileName == QStringLiteral("obs-outputs.dll")) {
        modulePathStorage = &outputsModulePathUtf8_;
        moduleDataStorage = &outputsModuleDataPathUtf8_;
    } else if (moduleFileName == QStringLiteral("rtmp-services.dll")) {
        modulePathStorage = &rtmpServicesModulePathUtf8_;
        moduleDataStorage = &rtmpServicesModuleDataPathUtf8_;
    } else {
        return fail(QStringLiteral("Unexpected module requested by the controlled loader: %1").arg(moduleFileName));
    }

    *modulePathStorage = QDir::fromNativeSeparators(modulePath).toUtf8();
    *moduleDataStorage = QDir::fromNativeSeparators(dataPath).toUtf8();

    const int moduleResult = runtime_.api().obs_open_module(
        module,
        modulePathStorage->constData(),
        moduleDataStorage->constData());

    if (moduleResult != 0 || *module == nullptr) {
        return fail(QStringLiteral("%1 failed to open with code %2. Review %3.")
                        .arg(moduleFileName)
                        .arg(moduleResult)
                        .arg(logPath_));
    }

    if (!runtime_.api().obs_init_module(*module)) {
        return fail(QStringLiteral("%1 failed to initialize. Review %2.").arg(moduleFileName, logPath_));
    }

    return true;
}

bool VuttaraEngine::loadOptionalModule(
    const QString& moduleFileName,
    const QString& moduleDataDirectory,
    QByteArray& modulePathStorage,
    QByteArray& moduleDataStorage,
    ObsAbi::obs_module_t** module)
{
    const QString modulePath = QDir(runtime_.pluginDirectory()).filePath(moduleFileName);
    const QString dataPath = QDir(runtime_.dataDirectory()).filePath(moduleDataDirectory);

    if (!QFileInfo(modulePath).isFile() || !QFileInfo(dataPath).isDir()) {
        log(QStringLiteral("Optional recording module unavailable: %1").arg(moduleFileName));
        *module = nullptr;
        return false;
    }

    modulePathStorage = QDir::fromNativeSeparators(modulePath).toUtf8();
    moduleDataStorage = QDir::fromNativeSeparators(dataPath).toUtf8();
    const int moduleResult = runtime_.api().obs_open_module(
        module,
        modulePathStorage.constData(),
        moduleDataStorage.constData());

    if (moduleResult != 0 || *module == nullptr) {
        log(QStringLiteral("Optional recording module did not open: %1 (code %2)")
                .arg(moduleFileName)
                .arg(moduleResult));
        *module = nullptr;
        return false;
    }

    if (!runtime_.api().obs_init_module(*module)) {
        log(QStringLiteral("Optional recording module did not initialize: %1")
                .arg(moduleFileName));
        *module = nullptr;
        return false;
    }

    log(QStringLiteral("Optional recording module loaded: %1").arg(moduleFileName));
    return true;
}

bool VuttaraEngine::registeredOutputType(const char* id) const
{
    if (id == nullptr || !runtime_.isLoaded()) {
        return false;
    }

    const auto& api = runtime_.api();
    for (std::size_t index = 0;; ++index) {
        const char* registeredId = nullptr;
        if (!api.obs_enum_output_types(index, &registeredId)) {
            break;
        }
        if (registeredId != nullptr && std::strcmp(registeredId, id) == 0) {
            return true;
        }
    }
    return false;
}

bool VuttaraEngine::registeredServiceType(const char* id) const
{
    if (id == nullptr || !runtime_.isLoaded()) {
        return false;
    }
    const auto& api = runtime_.api();
    for (std::size_t index = 0;; ++index) {
        const char* registeredId = nullptr;
        if (!api.obs_enum_service_types(index, &registeredId)) {
            break;
        }
        if (registeredId != nullptr && std::strcmp(registeredId, id) == 0) {
            return true;
        }
    }
    return false;
}

bool VuttaraEngine::registeredEncoderType(const char* id) const
{
    if (id == nullptr || !runtime_.isLoaded()) {
        return false;
    }

    const auto& api = runtime_.api();
    for (std::size_t index = 0;; ++index) {
        const char* registeredId = nullptr;
        if (!api.obs_enum_encoder_types(index, &registeredId)) {
            break;
        }
        if (registeredId != nullptr && std::strcmp(registeredId, id) == 0) {
            return true;
        }
    }
    return false;
}

bool VuttaraEngine::tryStartStreamingWithEncoder(
    const RecordingEncoderInfo& encoder,
    const StreamingSettings& requestedSettings,
    QString* failureReason)
{
    const StreamingSettings settings = normalizedStreamingSettings(requestedSettings);
    releaseStreamingResources();
    const auto& api = runtime_.api();

    ObsAbi::obs_data_t* serviceSettings = api.obs_data_create();
    if (serviceSettings == nullptr) {
        if (failureReason != nullptr) *failureReason = QStringLiteral("could not allocate service settings");
        return false;
    }
    const QByteArray serverUtf8 = settings.server.toUtf8();
    const QByteArray keyUtf8 = settings.streamKey.toUtf8();
    const QByteArray usernameUtf8 = settings.username.toUtf8();
    const QByteArray passwordUtf8 = settings.password.toUtf8();
    api.obs_data_set_string(serviceSettings, "server", serverUtf8.constData());
    api.obs_data_set_string(serviceSettings, "key", keyUtf8.constData());
    api.obs_data_set_bool(serviceSettings, "use_auth", settings.useAuthentication);
    api.obs_data_set_string(serviceSettings, "username", usernameUtf8.constData());
    api.obs_data_set_string(serviceSettings, "password", passwordUtf8.constData());
    streamingService_ = api.obs_service_create("rtmp_custom", "Vuttara Custom RTMP Service", serviceSettings, nullptr);
    api.obs_data_release(serviceSettings);
    if (streamingService_ == nullptr) {
        if (failureReason != nullptr) *failureReason = QStringLiteral("rtmp_custom service creation failed");
        releaseStreamingResources();
        return false;
    }

    ObsAbi::obs_data_t* videoSettings = api.obs_encoder_defaults(encoder.id.toUtf8().constData());
    if (videoSettings == nullptr) videoSettings = api.obs_data_create();
    ObsAbi::obs_data_t* audioSettings = api.obs_encoder_defaults("ffmpeg_aac");
    if (audioSettings == nullptr) audioSettings = api.obs_data_create();
    if (videoSettings == nullptr || audioSettings == nullptr) {
        if (videoSettings != nullptr) api.obs_data_release(videoSettings);
        if (audioSettings != nullptr) api.obs_data_release(audioSettings);
        if (failureReason != nullptr) *failureReason = QStringLiteral("could not allocate encoder settings");
        releaseStreamingResources();
        return false;
    }

    api.obs_data_set_int(videoSettings, "bitrate", settings.videoBitrateKbps);
    api.obs_data_set_int(videoSettings, "keyint_sec", settings.keyframeIntervalSeconds);
    api.obs_data_set_string(videoSettings, "profile", "high");
    api.obs_data_set_string(videoSettings, "rate_control", "CBR");
    if (encoder.id == QStringLiteral("obs_x264")) {
        api.obs_data_set_string(videoSettings, "preset", "veryfast");
        api.obs_data_set_bool(videoSettings, "use_bufsize", true);
        api.obs_data_set_int(videoSettings, "buffer_size", settings.videoBitrateKbps);
    }
    api.obs_data_set_int(audioSettings, "bitrate", settings.audioBitrateKbps);
    api.obs_service_apply_encoder_settings(streamingService_, videoSettings, audioSettings);

    const QByteArray encoderIdUtf8 = encoder.id.toUtf8();
    streamingVideoEncoder_ = api.obs_video_encoder_create(
        encoderIdUtf8.constData(), "Vuttara Streaming Video", videoSettings, nullptr);
    streamingAudioEncoder_ = api.obs_audio_encoder_create(
        "ffmpeg_aac", "Vuttara Streaming Audio", audioSettings, 0, nullptr);
    api.obs_data_release(videoSettings);
    api.obs_data_release(audioSettings);
    if (streamingVideoEncoder_ == nullptr || streamingAudioEncoder_ == nullptr) {
        if (failureReason != nullptr) *failureReason = QStringLiteral("streaming encoder creation failed");
        releaseStreamingResources();
        return false;
    }

    api.obs_encoder_set_video(streamingVideoEncoder_, api.obs_get_video());
    api.obs_encoder_set_audio(streamingAudioEncoder_, api.obs_get_audio());
    if (settings.outputWidth == static_cast<int>(CanvasWidth) && settings.outputHeight == static_cast<int>(CanvasHeight)) {
        api.obs_encoder_set_scaled_size(streamingVideoEncoder_, 0, 0);
    } else {
        api.obs_encoder_set_scaled_size(streamingVideoEncoder_, settings.outputWidth, settings.outputHeight);
    }
    if (!api.obs_encoder_set_frame_rate_divisor(streamingVideoEncoder_, settings.framesPerSecond == 30 ? 2U : 1U)) {
        if (failureReason != nullptr) *failureReason = QStringLiteral("could not apply streaming frame rate");
        releaseStreamingResources();
        return false;
    }

    ObsAbi::obs_data_t* outputSettings = api.obs_data_create();
    streamingOutput_ = api.obs_output_create("rtmp_output", "Vuttara RTMP Stream", outputSettings, nullptr);
    if (outputSettings != nullptr) api.obs_data_release(outputSettings);
    if (streamingOutput_ == nullptr) {
        if (failureReason != nullptr) *failureReason = QStringLiteral("rtmp_output creation failed");
        releaseStreamingResources();
        return false;
    }

    api.obs_output_set_service(streamingOutput_, streamingService_);
    api.obs_output_set_video_encoder(streamingOutput_, streamingVideoEncoder_);
    api.obs_output_set_audio_encoder(streamingOutput_, streamingAudioEncoder_, 0);
    api.obs_output_set_reconnect_settings(
        streamingOutput_, settings.automaticReconnect ? settings.maximumRetries : 0, settings.retryDelaySeconds);

    if (!api.obs_output_start(streamingOutput_)) {
        QString error;
        if (const char* outputError = api.obs_output_get_last_error(streamingOutput_); outputError != nullptr && *outputError != '\0') {
            error = QString::fromUtf8(outputError);
        }
        if (error.isEmpty()) error = QStringLiteral("libobs rejected the RTMP output or service configuration");
        if (failureReason != nullptr) *failureReason = error;
        releaseStreamingResources();
        return false;
    }
    return true;
}

void VuttaraEngine::finalizeStreamingIfStopped()
{
    if (streamingOutput_ == nullptr || !runtime_.isLoaded()) return;
    const auto& api = runtime_.api();
    if (api.obs_output_active(streamingOutput_) || api.obs_output_reconnecting(streamingOutput_)) return;
    const auto now = std::chrono::steady_clock::now();
    if (!streamingInfo_.stopping && streamingInfo_.state == StreamingState::Connecting &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - streamingStartedAt_).count() < 10000) {
        return;
    }
    streamingInfo_.elapsedMilliseconds = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - streamingStartedAt_).count());
    streamingInfo_.totalBytes = api.obs_output_get_total_bytes(streamingOutput_);
    QString outputError;
    if (const char* error = api.obs_output_get_last_error(streamingOutput_); error != nullptr && *error != '\0') {
        outputError = QString::fromUtf8(error);
    }
    const bool expectedStop = streamingInfo_.stopping;
    releaseStreamingResources();
    streamingInfo_.active = false;
    streamingInfo_.reconnecting = false;
    streamingInfo_.stopping = false;
    if (!expectedStop && !outputError.isEmpty()) {
        streamingInfo_.state = StreamingState::Error;
        streamingInfo_.error = outputError;
        lastError_ = outputError;
        log(QStringLiteral("Streaming stopped with an error: %1").arg(outputError));
    } else if (!expectedStop && streamingInfo_.elapsedMilliseconds > 1500) {
        streamingInfo_.state = StreamingState::Error;
        streamingInfo_.error = QStringLiteral("Streaming stopped unexpectedly.");
        lastError_ = streamingInfo_.error;
        log(streamingInfo_.error);
    } else {
        streamingInfo_.state = StreamingState::Ready;
        streamingInfo_.error.clear();
        lastError_.clear();
        log(QStringLiteral("Streaming stopped cleanly."));
    }
}

void VuttaraEngine::releaseStreamingResources()
{
    if (!runtime_.isLoaded()) {
        streamingOutput_ = nullptr;
        streamingVideoEncoder_ = nullptr;
        streamingAudioEncoder_ = nullptr;
        streamingService_ = nullptr;
        return;
    }
    const auto& api = runtime_.api();
    if (streamingOutput_ != nullptr) { api.obs_output_release(streamingOutput_); streamingOutput_ = nullptr; }
    if (streamingVideoEncoder_ != nullptr) { api.obs_encoder_release(streamingVideoEncoder_); streamingVideoEncoder_ = nullptr; }
    if (streamingAudioEncoder_ != nullptr) { api.obs_encoder_release(streamingAudioEncoder_); streamingAudioEncoder_ = nullptr; }
    if (streamingService_ != nullptr) { api.obs_service_release(streamingService_); streamingService_ = nullptr; }
}

QString VuttaraEngine::uniqueRecordingPath(const RecordingSettings& requestedSettings) const
{
    const RecordingSettings settings = normalizedRecordingSettings(requestedSettings);
    const QDateTime now = QDateTime::currentDateTime();
    QString stem = settings.filenameFormat;
    stem.replace(QStringLiteral("{date}"), now.toString(QStringLiteral("yyyy-MM-dd")));
    stem.replace(QStringLiteral("{time}"), now.toString(QStringLiteral("HH-mm-ss")));
    stem.replace(QStringLiteral("{scene}"), activeSceneName());
    stem.replace(
        QStringLiteral("{resolution}"),
        QStringLiteral("%1x%2").arg(settings.outputWidth).arg(settings.outputHeight));
    stem.replace(
        QStringLiteral("{fps}"),
        QString::number(settings.framesPerSecond));
    stem = safeRecordingStem(stem);

    QDir directory(settings.outputDirectory);
    QString candidate = directory.filePath(stem + QStringLiteral(".mkv"));
    int suffix = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = directory.filePath(
            QStringLiteral("%1_%2.mkv").arg(stem).arg(suffix++));
    }
    return QDir::cleanPath(candidate);
}

bool VuttaraEngine::tryStartRecordingWithEncoder(
    const RecordingEncoderInfo& encoder,
    const RecordingSettings& requestedSettings,
    const QString& outputPath,
    QString* failureReason)
{
    const RecordingSettings settings = normalizedRecordingSettings(requestedSettings);
    releaseRecordingResources();
    const auto& api = runtime_.api();

    ObsAbi::obs_data_t* videoSettings = api.obs_encoder_defaults(encoder.id.toUtf8().constData());
    if (videoSettings == nullptr) {
        videoSettings = api.obs_data_create();
    }
    if (videoSettings == nullptr) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("could not allocate video encoder settings");
        }
        return false;
    }

    api.obs_data_set_int(videoSettings, "bitrate", settings.videoBitrateKbps);
    api.obs_data_set_int(videoSettings, "keyint_sec", 2);
    api.obs_data_set_string(videoSettings, "profile", "high");
    api.obs_data_set_string(videoSettings, "rate_control", "CBR");
    if (encoder.id == QStringLiteral("obs_x264")) {
        api.obs_data_set_string(videoSettings, "preset", "veryfast");
        api.obs_data_set_bool(videoSettings, "use_bufsize", true);
        api.obs_data_set_int(videoSettings, "buffer_size", settings.videoBitrateKbps);
    }

    const QByteArray videoId = encoder.id.toUtf8();
    recordingVideoEncoder_ = api.obs_video_encoder_create(
        videoId.constData(),
        "Vuttara Recording Video",
        videoSettings,
        nullptr);
    api.obs_data_release(videoSettings);

    if (recordingVideoEncoder_ == nullptr) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("video encoder creation failed");
        }
        releaseRecordingResources();
        return false;
    }

    api.obs_encoder_set_video(recordingVideoEncoder_, api.obs_get_video());
    if (settings.outputWidth == static_cast<int>(CanvasWidth) &&
        settings.outputHeight == static_cast<int>(CanvasHeight)) {
        api.obs_encoder_set_scaled_size(recordingVideoEncoder_, 0, 0);
    } else {
        api.obs_encoder_set_scaled_size(
            recordingVideoEncoder_,
            static_cast<std::uint32_t>(settings.outputWidth),
            static_cast<std::uint32_t>(settings.outputHeight));
    }

    const std::uint32_t frameRateDivisor =
        settings.framesPerSecond == 30 ? 2U : 1U;
    if (!api.obs_encoder_set_frame_rate_divisor(
            recordingVideoEncoder_,
            frameRateDivisor)) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("could not apply the requested recording frame rate");
        }
        releaseRecordingResources();
        return false;
    }

    ObsAbi::obs_data_t* audioSettings = api.obs_encoder_defaults("ffmpeg_aac");
    if (audioSettings == nullptr) {
        audioSettings = api.obs_data_create();
    }
    if (audioSettings == nullptr) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("could not allocate AAC settings");
        }
        releaseRecordingResources();
        return false;
    }
    api.obs_data_set_int(audioSettings, "bitrate", settings.audioBitrateKbps);
    recordingAudioEncoder_ = api.obs_audio_encoder_create(
        "ffmpeg_aac",
        "Vuttara Recording Audio",
        audioSettings,
        0,
        nullptr);
    api.obs_data_release(audioSettings);

    if (recordingAudioEncoder_ == nullptr) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("AAC encoder creation failed");
        }
        releaseRecordingResources();
        return false;
    }

    api.obs_encoder_set_audio(recordingAudioEncoder_, api.obs_get_audio());

    ObsAbi::obs_data_t* outputSettings = api.obs_data_create();
    if (outputSettings == nullptr) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("could not allocate MKV output settings");
        }
        releaseRecordingResources();
        return false;
    }

    const QByteArray pathUtf8 = QDir::toNativeSeparators(outputPath).toUtf8();
    api.obs_data_set_string(outputSettings, "path", pathUtf8.constData());
    api.obs_data_set_bool(outputSettings, "allow_overwrite", false);
    recordingOutput_ = api.obs_output_create(
        "ffmpeg_muxer",
        "Vuttara MKV Recording",
        outputSettings,
        nullptr);
    api.obs_data_release(outputSettings);

    if (recordingOutput_ == nullptr) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("ffmpeg_muxer output creation failed");
        }
        releaseRecordingResources();
        return false;
    }

    api.obs_output_set_video_encoder(recordingOutput_, recordingVideoEncoder_);
    api.obs_output_set_audio_encoder(recordingOutput_, recordingAudioEncoder_, 0);

    if (!api.obs_output_start(recordingOutput_)) {
        QString error;
        if (const char* outputError = api.obs_output_get_last_error(recordingOutput_);
            outputError != nullptr && *outputError != '\0') {
            error = QString::fromUtf8(outputError);
        }
        if (error.isEmpty()) {
            if (const char* encoderError = api.obs_encoder_get_last_error(recordingVideoEncoder_);
                encoderError != nullptr && *encoderError != '\0') {
                error = QString::fromUtf8(encoderError);
            }
        }
        if (error.isEmpty()) {
            error = QStringLiteral("libobs rejected the encoder/output combination");
        }
        if (failureReason != nullptr) {
            *failureReason = error;
        }
        releaseRecordingResources();
        QFile::remove(outputPath);
        return false;
    }

    return true;
}
void VuttaraEngine::finalizeRecordingIfStopped()
{
    if (recordingOutput_ == nullptr || !runtime_.isLoaded()) {
        return;
    }

    const auto& api = runtime_.api();
    if (api.obs_output_active(recordingOutput_)) {
        return;
    }

    recordingInfo_.elapsedMilliseconds = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recordingStartedAt_)
            .count());
    recordingInfo_.totalBytes = api.obs_output_get_total_bytes(recordingOutput_);

    QString outputError;
    if (const char* error = api.obs_output_get_last_error(recordingOutput_);
        error != nullptr && *error != '\0') {
        outputError = QString::fromUtf8(error);
    }

    const bool expectedStop = recordingInfo_.stopping;
    const QFileInfo outputFile(recordingInfo_.outputPath);
    if (outputFile.isFile()) {
        recordingInfo_.totalBytes = std::max<std::uint64_t>(
            recordingInfo_.totalBytes,
            static_cast<std::uint64_t>(outputFile.size()));
    }

    releaseRecordingResources();
    recordingInfo_.active = false;
    recordingInfo_.stopping = false;

    QString finalizationError = outputError;
    if (finalizationError.isEmpty() && !expectedStop) {
        finalizationError = QStringLiteral("Recording stopped unexpectedly.");
    }
    if (finalizationError.isEmpty() && (!outputFile.isFile() || outputFile.size() <= 0)) {
        finalizationError = QStringLiteral("Recording finalized without a usable output file.");
    }

    if (!finalizationError.isEmpty()) {
        recordingInfo_.state = RecordingState::Error;
        recordingInfo_.error = finalizationError;
        lastError_ = finalizationError;
        log(QStringLiteral("Recording stopped with an error: %1").arg(finalizationError));
    } else {
        recordingInfo_.state = RecordingState::Ready;
        recordingInfo_.error.clear();
        lastError_.clear();
        log(QStringLiteral("Recording finalized: %1 | bytes=%2")
                .arg(recordingInfo_.outputPath)
                .arg(recordingInfo_.totalBytes));
    }
}

void VuttaraEngine::releaseRecordingResources()
{
    if (!runtime_.isLoaded()) {
        recordingOutput_ = nullptr;
        recordingVideoEncoder_ = nullptr;
        recordingAudioEncoder_ = nullptr;
        return;
    }

    const auto& api = runtime_.api();
    if (recordingOutput_ != nullptr) {
        api.obs_output_release(recordingOutput_);
        recordingOutput_ = nullptr;
    }
    if (recordingVideoEncoder_ != nullptr) {
        api.obs_encoder_release(recordingVideoEncoder_);
        recordingVideoEncoder_ = nullptr;
    }
    if (recordingAudioEncoder_ != nullptr) {
        api.obs_encoder_release(recordingAudioEncoder_);
        recordingAudioEncoder_ = nullptr;
    }
}

VuttaraEngine::AudioChannelState& VuttaraEngine::audioChannel(AudioChannelKind kind)
{
    return kind == AudioChannelKind::Desktop ? desktopAudio_ : microphoneAudio_;
}

const VuttaraEngine::AudioChannelState& VuttaraEngine::audioChannel(AudioChannelKind kind) const
{
    return kind == AudioChannelKind::Desktop ? desktopAudio_ : microphoneAudio_;
}

AudioChannelInfo VuttaraEngine::audioChannelInfo(const AudioChannelState& channel) const
{
    AudioChannelInfo info;
    info.kind = channel.kind;
    info.name = channel.name;
    info.deviceId = channel.deviceId;
    info.deviceName = channel.deviceName;
    info.connected = channel.source != nullptr;
    info.muted = channel.muted;
    info.meterAttached = channel.meter != nullptr;
    info.volumePercent = channel.volumePercent;
    info.peakDb = channel.peakDb.load(std::memory_order_relaxed);
    return info;
}

bool VuttaraEngine::createAudioChannel(AudioChannelState& channel, const AudioDeviceInfo& device)
{
    const int preservedVolume = channel.volumePercent;
    const bool preservedMute = channel.muted;
    releaseAudioChannel(channel);
    channel.volumePercent = preservedVolume;
    channel.muted = preservedMute;

    const auto& api = runtime_.api();
    ObsAbi::obs_data_t* settings = api.obs_data_create();
    if (settings == nullptr) {
        return operationFail(QStringLiteral("Could not allocate settings for %1.").arg(channel.name));
    }

    const QByteArray deviceId = device.deviceId.toUtf8();
    api.obs_data_set_string(settings, "device_id", deviceId.constData());
    api.obs_data_set_bool(settings, "use_device_timing", false);

    const QByteArray sourceName = channel.name.toUtf8();
    const char* sourceType = channel.kind == AudioChannelKind::Desktop
        ? "wasapi_output_capture"
        : "wasapi_input_capture";

    channel.source = api.obs_source_create(sourceType, sourceName.constData(), settings, nullptr);
    api.obs_data_release(settings);
    if (channel.source == nullptr) {
        return operationFail(QStringLiteral("Could not create %1 through %2.")
                                 .arg(channel.name, QString::fromLatin1(sourceType)));
    }

    channel.fader = api.obs_fader_create(ObsAbi::OBS_FADER_IEC);
    channel.meter = api.obs_volmeter_create(ObsAbi::OBS_FADER_IEC);
    if (channel.fader == nullptr || channel.meter == nullptr) {
        releaseAudioChannel(channel);
        return operationFail(QStringLiteral("Could not create the fader or meter for %1.").arg(channel.name));
    }

    if (!api.obs_fader_attach_source(channel.fader, channel.source)
        || !api.obs_volmeter_attach_source(channel.meter, channel.source)) {
        releaseAudioChannel(channel);
        return operationFail(QStringLiteral("Could not attach the fader or IEC meter to %1.").arg(channel.name));
    }

    api.obs_volmeter_set_peak_meter_type(channel.meter, ObsAbi::SAMPLE_PEAK_METER);
    api.obs_volmeter_add_callback(channel.meter, &VuttaraEngine::audioMeterCallback, &channel);
    api.obs_fader_set_deflection(channel.fader, static_cast<float>(channel.volumePercent) / 100.0F);
    api.obs_source_set_muted(channel.source, channel.muted);
    api.obs_set_output_source(channel.outputIndex, channel.source);

    channel.deviceId = device.deviceId;
    channel.deviceName = device.name;
    channel.muted = api.obs_source_muted(channel.source);
    channel.peakDb.store(-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
    lastError_.clear();
    log(QStringLiteral("Audio channel connected: %1 -> %2 (%3)")
            .arg(channel.name, channel.deviceName, channel.deviceId));
    return true;
}

void VuttaraEngine::releaseAudioChannel(AudioChannelState& channel)
{
    if (!runtime_.isLoaded()) {
        channel.source = nullptr;
        channel.fader = nullptr;
        channel.meter = nullptr;
        channel.deviceId.clear();
        channel.deviceName.clear();
        channel.peakDb.store(-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
        return;
    }

    const auto& api = runtime_.api();
    if (api.obs_initialized != nullptr && api.obs_initialized()) {
        api.obs_set_output_source(channel.outputIndex, nullptr);
    }

    if (channel.meter != nullptr) {
        api.obs_volmeter_remove_callback(channel.meter, &VuttaraEngine::audioMeterCallback, &channel);
        api.obs_volmeter_detach_source(channel.meter);
        api.obs_volmeter_destroy(channel.meter);
        channel.meter = nullptr;
    }

    if (channel.fader != nullptr) {
        api.obs_fader_detach_source(channel.fader);
        api.obs_fader_destroy(channel.fader);
        channel.fader = nullptr;
    }

    if (channel.source != nullptr) {
        api.obs_source_release(channel.source);
        channel.source = nullptr;
    }

    channel.deviceId.clear();
    channel.deviceName.clear();
    channel.peakDb.store(-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
}

void VuttaraEngine::releaseAllAudioChannels()
{
    releaseAudioChannel(desktopAudio_);
    releaseAudioChannel(microphoneAudio_);
}

bool VuttaraEngine::restoreAudioChannel(AudioChannelKind kind, const QJsonObject& state)
{
    AudioChannelState& channel = audioChannel(kind);
    channel.volumePercent = std::clamp(state.value(QStringLiteral("volumePercent")).toInt(DefaultAudioVolume), 0, 100);
    channel.muted = state.value(QStringLiteral("muted")).toBool(false);

    if (!state.value(QStringLiteral("connected")).toBool(false)) {
        releaseAudioChannel(channel);
        channel.volumePercent = std::clamp(state.value(QStringLiteral("volumePercent")).toInt(DefaultAudioVolume), 0, 100);
        channel.muted = state.value(QStringLiteral("muted")).toBool(false);
        return true;
    }

    AudioDeviceInfo device;
    device.deviceId = state.value(QStringLiteral("deviceId")).toString();
    device.name = state.value(QStringLiteral("deviceName")).toString();
    device.defaultDevice = device.deviceId == QStringLiteral("default");
    if (device.deviceId.isEmpty()) {
        return true;
    }
    if (device.name.isEmpty()) {
        device.name = device.defaultDevice
            ? QStringLiteral("Windows default device")
            : QStringLiteral("Saved Windows audio device");
    }

    if (!createAudioChannel(channel, device)) {
        log(QStringLiteral("Audio project restore skipped %1: %2").arg(channel.name, lastError_));
        lastError_.clear();
        return false;
    }
    return true;
}

VuttaraEngine::ManagedScene* VuttaraEngine::activeScene()
{
    return activeSceneIndex_ >= 0 && activeSceneIndex_ < scenes_.size()
        ? &scenes_[activeSceneIndex_]
        : nullptr;
}

const VuttaraEngine::ManagedScene* VuttaraEngine::activeScene() const
{
    return activeSceneIndex_ >= 0 && activeSceneIndex_ < scenes_.size()
        ? &scenes_.at(activeSceneIndex_)
        : nullptr;
}

VuttaraEngine::ManagedScene* VuttaraEngine::findScene(const QString& sceneName)
{
    const auto iterator = std::find_if(
        scenes_.begin(),
        scenes_.end(),
        [&sceneName](const ManagedScene& scene) {
            return scene.name.compare(sceneName, Qt::CaseInsensitive) == 0;
        });
    return iterator != scenes_.end() ? &(*iterator) : nullptr;
}

const VuttaraEngine::ManagedScene* VuttaraEngine::findScene(const QString& sceneName) const
{
    const auto iterator = std::find_if(
        scenes_.cbegin(),
        scenes_.cend(),
        [&sceneName](const ManagedScene& scene) {
            return scene.name.compare(sceneName, Qt::CaseInsensitive) == 0;
        });
    return iterator != scenes_.cend() ? &(*iterator) : nullptr;
}

VuttaraEngine::ManagedSource* VuttaraEngine::findSource(ManagedScene& scene, const QString& sourceName)
{
    const auto iterator = std::find_if(
        scene.sources.begin(),
        scene.sources.end(),
        [&sourceName](const ManagedSource& source) {
            return source.name == sourceName;
        });
    return iterator != scene.sources.end() ? &(*iterator) : nullptr;
}

const VuttaraEngine::ManagedSource* VuttaraEngine::findSource(
    const ManagedScene& scene,
    const QString& sourceName) const
{
    const auto iterator = std::find_if(
        scene.sources.cbegin(),
        scene.sources.cend(),
        [&sourceName](const ManagedSource& source) {
            return source.name == sourceName;
        });
    return iterator != scene.sources.cend() ? &(*iterator) : nullptr;
}

QString VuttaraEngine::uniqueSceneName(const QString& requestedName) const
{
    const QString baseName = requestedName.trimmed().isEmpty()
        ? QStringLiteral("Scene")
        : requestedName.trimmed();

    const auto exists = [this](const QString& candidate) {
        if (findScene(candidate) != nullptr) {
            return true;
        }
        for (const ManagedScene& scene : scenes_) {
            if (std::any_of(
                    scene.sources.cbegin(),
                    scene.sources.cend(),
                    [&candidate](const ManagedSource& source) {
                        return source.name.compare(candidate, Qt::CaseInsensitive) == 0;
                    })) {
                return true;
            }
        }
        return false;
    };

    if (!exists(baseName)) {
        return baseName;
    }

    for (int suffix = 2; suffix < 1000; ++suffix) {
        const QString candidate = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
        if (!exists(candidate)) {
            return candidate;
        }
    }

    return QStringLiteral("Scene %1").arg(scenes_.size() + 1);
}

QString VuttaraEngine::uniqueSourceName(const QString& requestedName, const QString& fallbackBaseName) const
{
    const QString baseName = requestedName.trimmed().isEmpty()
        ? fallbackBaseName
        : requestedName.trimmed();

    const auto exists = [this](const QString& candidate) {
        if (findScene(candidate) != nullptr) {
            return true;
        }
        for (const ManagedScene& scene : scenes_) {
            if (std::any_of(
                    scene.sources.cbegin(),
                    scene.sources.cend(),
                    [&candidate](const ManagedSource& source) {
                        return source.name.compare(candidate, Qt::CaseInsensitive) == 0;
                    })) {
                return true;
            }
        }
        return false;
    };

    if (!exists(baseName)) {
        return baseName;
    }

    for (int suffix = 2; suffix < 1000; ++suffix) {
        const QString candidate = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
        if (!exists(candidate)) {
            return candidate;
        }
    }

    return QStringLiteral("%1 %2").arg(fallbackBaseName).arg(scenes_.size() + 1);
}

bool VuttaraEngine::createSceneInternal(const QString& name, bool removable, bool addFoundationColor)
{
    const QByteArray nameUtf8 = name.toUtf8();
    ObsAbi::obs_scene_t* sceneHandle = runtime_.api().obs_scene_create(nameUtf8.constData());
    if (sceneHandle == nullptr) {
        return false;
    }

    ManagedScene scene;
    scene.name = name;
    scene.removable = removable;
    scene.scene = sceneHandle;
    scene.source = runtime_.api().obs_scene_get_source(sceneHandle);
    scenes_.append(scene);

    if (addFoundationColor && !createFoundationColor(scenes_.last())) {
        runtime_.api().obs_scene_release(sceneHandle);
        scenes_.removeLast();
        return false;
    }

    return true;
}

bool VuttaraEngine::createFoundationColor(ManagedScene& scene)
{
    return addColorSourceToScene(
        scene,
        scene.name == QStringLiteral("Main Scene")
            ? QStringLiteral("Vuttara Test Color")
            : QStringLiteral("%1 Background").arg(scene.name),
        static_cast<std::uint32_t>(TestSourceColor),
        static_cast<int>(CanvasWidth),
        static_cast<int>(CanvasHeight),
        false,
        true,
        nullptr);
}

bool VuttaraEngine::addColorSourceToScene(
    ManagedScene& scene,
    const QString& requestedName,
    std::uint32_t color,
    int sourceWidth,
    int sourceHeight,
    bool removable,
    bool locked,
    QString* createdName)
{
    const auto& api = runtime_.api();
    ObsAbi::obs_data_t* settings = api.obs_data_create();
    if (settings == nullptr) {
        return operationFail(QStringLiteral("Could not create Color Source settings."));
    }

    sourceWidth = std::clamp(sourceWidth, 1, 7680);
    sourceHeight = std::clamp(sourceHeight, 1, 7680);
    api.obs_data_set_int(settings, "color", static_cast<long long>(color));
    api.obs_data_set_int(settings, "width", sourceWidth);
    api.obs_data_set_int(settings, "height", sourceHeight);

    const QString sourceName = uniqueSourceName(requestedName, QStringLiteral("Color Source"));
    const QByteArray sourceNameUtf8 = sourceName.toUtf8();
    ObsAbi::obs_source_t* colorSource = api.obs_source_create(
        "color_source_v3", sourceNameUtf8.constData(), settings, nullptr);
    api.obs_data_release(settings);
    if (colorSource == nullptr) {
        return operationFail(QStringLiteral("libobs could not create the color source."));
    }

    ObsAbi::obs_sceneitem_t* colorItem = api.obs_scene_add(scene.scene, colorSource);
    api.obs_source_release(colorSource);
    if (colorItem == nullptr) {
        return operationFail(QStringLiteral("Could not add the Color Source to %1.").arg(scene.name));
    }

    scene.sources.append(ManagedSource{
        sourceName,
        QStringLiteral("color"),
        QString{},
        true,
        removable,
        locked,
        false,
        true,
        WindowCaptureMethod::Automatic,
        WindowMatchPriority::Title,
        color,
        sourceWidth,
        sourceHeight,
        colorItem,
    });
    api.obs_sceneitem_set_locked(colorItem, locked);
    fitSceneItemToCanvas(colorItem);
    if (createdName != nullptr) {
        *createdName = sourceName;
    }
    lastError_.clear();
    return true;
}

void VuttaraEngine::releaseAllScenes()
{
    if (runtime_.isLoaded() && runtime_.api().obs_initialized != nullptr && runtime_.api().obs_initialized()) {
        runtime_.api().obs_set_output_source(0, nullptr);
    }

    if (!runtime_.isLoaded()) {
        scenes_.clear();
        activeSceneIndex_ = -1;
        return;
    }

    for (ManagedScene& scene : scenes_) {
        scene.sources.clear();
        if (scene.scene != nullptr) {
            runtime_.api().obs_scene_release(scene.scene);
            scene.scene = nullptr;
            scene.source = nullptr;
        }
    }
    scenes_.clear();
    activeSceneIndex_ = -1;
}

bool VuttaraEngine::addDisplayCaptureToScene(
    ManagedScene& scene,
    const DisplayInfo& display,
    bool captureCursor,
    const QString& requestedName,
    bool enforceLimit,
    QString* createdName)
{
    if (display.monitorId.trimmed().isEmpty()) {
        return operationFail(QStringLiteral("The selected display does not have a valid Windows monitor identifier."));
    }
    if (enforceLimit && std::any_of(
            scene.sources.cbegin(),
            scene.sources.cend(),
            [](const ManagedSource& source) {
                return source.type == QStringLiteral("display_capture");
            })) {
        return operationFail(QStringLiteral(
            "Stage 4A supports one Display Capture source per scene. Remove the existing one first."));
    }

    const QString sourceName = uniqueSourceName(requestedName, QStringLiteral("Display Capture"));
    const QByteArray sourceNameUtf8 = sourceName.toUtf8();
    const QByteArray monitorIdUtf8 = display.monitorId.toUtf8();
    const auto& api = runtime_.api();

    ObsAbi::obs_data_t* settings = api.obs_data_create();
    if (settings == nullptr) {
        return operationFail(QStringLiteral("Could not create Display Capture settings."));
    }

    api.obs_data_set_string(settings, "monitor_id", monitorIdUtf8.constData());
    api.obs_data_set_int(settings, "method", 0);
    api.obs_data_set_bool(settings, "capture_cursor", captureCursor);
    api.obs_data_set_bool(settings, "force_sdr", false);

    ObsAbi::obs_source_t* captureSource = api.obs_source_create(
        "monitor_capture",
        sourceNameUtf8.constData(),
        settings,
        nullptr);
    api.obs_data_release(settings);

    if (captureSource == nullptr) {
        return operationFail(QStringLiteral(
            "libobs could not create the monitor_capture source. Review the native log at %1.")
                                 .arg(logPath_));
    }

    ObsAbi::obs_sceneitem_t* item = api.obs_scene_add(scene.scene, captureSource);
    api.obs_source_release(captureSource);
    if (item == nullptr) {
        return operationFail(QStringLiteral("Could not add the Display Capture source to %1.").arg(scene.name));
    }

    fitSceneItemToCanvas(item);
    scene.sources.append(ManagedSource{
        sourceName,
        QStringLiteral("display_capture"),
        display.monitorId,
        true,
        true,
        false,
        captureCursor,
        true,
        WindowCaptureMethod::Automatic,
        WindowMatchPriority::Title,
        0U,
        static_cast<int>(CanvasWidth),
        static_cast<int>(CanvasHeight),
        item,
    });

    if (createdName != nullptr) {
        *createdName = sourceName;
    }

    lastError_.clear();
    log(QStringLiteral(
            "Display Capture: added %1 to %2 using %3; cursor=%4; fitted to 1920x1080 canvas.")
            .arg(
                sourceName,
                scene.name,
                display.description,
                captureCursor ? QStringLiteral("true") : QStringLiteral("false")));
    return true;
}

bool VuttaraEngine::addWindowCaptureToScene(
    ManagedScene& scene,
    const WindowInfo& window,
    WindowCaptureMethod method,
    WindowMatchPriority priority,
    bool captureCursor,
    bool clientArea,
    const QString& requestedName,
    bool enforceLimit,
    QString* createdName)
{
    if (window.encodedValue.trimmed().isEmpty()) {
        return operationFail(QStringLiteral("The selected window does not have a valid OBS window identifier."));
    }
    if (enforceLimit && std::any_of(
            scene.sources.cbegin(),
            scene.sources.cend(),
            [](const ManagedSource& source) {
                return source.type == QStringLiteral("window_capture");
            })) {
        return operationFail(QStringLiteral(
            "Stage 4A supports one Window Capture source per scene. Remove the existing one first."));
    }

    const QString sourceName = uniqueSourceName(requestedName, QStringLiteral("Window Capture"));
    const QByteArray sourceNameUtf8 = sourceName.toUtf8();
    const QByteArray encodedWindowUtf8 = window.encodedValue.toUtf8();
    const auto& api = runtime_.api();

    ObsAbi::obs_data_t* settings = api.obs_data_create();
    if (settings == nullptr) {
        return operationFail(QStringLiteral("Could not create Window Capture settings."));
    }

    api.obs_data_set_string(settings, "window", encodedWindowUtf8.constData());
    api.obs_data_set_int(settings, "method", static_cast<long long>(method));
    api.obs_data_set_int(settings, "priority", static_cast<long long>(priority));
    api.obs_data_set_bool(settings, "cursor", captureCursor);
    api.obs_data_set_bool(settings, "client_area", clientArea);
    api.obs_data_set_bool(settings, "compatibility", false);
    api.obs_data_set_bool(settings, "force_sdr", false);
    api.obs_data_set_bool(settings, "capture_audio", false);

    ObsAbi::obs_source_t* captureSource = api.obs_source_create(
        "window_capture",
        sourceNameUtf8.constData(),
        settings,
        nullptr);
    api.obs_data_release(settings);

    if (captureSource == nullptr) {
        return operationFail(QStringLiteral(
            "libobs could not create the window_capture source. Review the native log at %1.")
                                 .arg(logPath_));
    }

    ObsAbi::obs_sceneitem_t* item = api.obs_scene_add(scene.scene, captureSource);
    api.obs_source_release(captureSource);
    if (item == nullptr) {
        return operationFail(QStringLiteral("Could not add the Window Capture source to %1.").arg(scene.name));
    }

    fitSceneItemToCanvas(item);
    scene.sources.append(ManagedSource{
        sourceName,
        QStringLiteral("window_capture"),
        window.encodedValue,
        true,
        true,
        false,
        captureCursor,
        clientArea,
        method,
        priority,
        0U,
        static_cast<int>(CanvasWidth),
        static_cast<int>(CanvasHeight),
        item,
    });

    if (createdName != nullptr) {
        *createdName = sourceName;
    }

    lastError_.clear();
    log(QStringLiteral(
            "Window Capture: added %1 to %2 for %3; method=%4; priority=%5; cursor=%6; client-area=%7; "
            "minimized-at-selection=%8; fitted to 1920x1080 canvas.")
            .arg(
                sourceName,
                scene.name,
                window.description,
                QString::number(static_cast<long long>(method)),
                QString::number(static_cast<long long>(priority)),
                captureCursor ? QStringLiteral("true") : QStringLiteral("false"),
                clientArea ? QStringLiteral("true") : QStringLiteral("false"),
                window.minimized ? QStringLiteral("true") : QStringLiteral("false")));
    return true;
}

SourceInfo VuttaraEngine::sourceInfo(const ManagedSource& source) const
{
    const auto& api = runtime_.api();
    ObsAbi::obs_source_t* obsSource =
        source.item != nullptr ? api.obs_sceneitem_get_source(source.item) : nullptr;
    const std::uint32_t nativeWidth =
        obsSource != nullptr ? api.obs_source_get_width(obsSource) : 0U;
    const std::uint32_t nativeHeight =
        obsSource != nullptr ? api.obs_source_get_height(obsSource) : 0U;
    return SourceInfo{
        source.name,
        source.type,
        source.persistentId,
        api.obs_sceneitem_visible(source.item),
        source.removable,
        api.obs_sceneitem_locked(source.item),
        api.obs_sceneitem_get_order_position(source.item),
        nativeWidth > 0U ? static_cast<int>(nativeWidth) : source.sourceWidth,
        nativeHeight > 0U ? static_cast<int>(nativeHeight) : source.sourceHeight,
        sourceTransform(source),
    };
}

SourceTransform VuttaraEngine::sourceTransform(const ManagedSource& source) const
{
    ObsAbi::vec2 position{};
    ObsAbi::vec2 bounds{};
    ObsAbi::vec2 scale{1.0F, 1.0F};
    ObsAbi::obs_sceneitem_crop crop{};
    const auto& api = runtime_.api();
    api.obs_sceneitem_get_pos(source.item, &position);
    api.obs_sceneitem_get_bounds(source.item, &bounds);
    api.obs_sceneitem_get_scale(source.item, &scale);
    api.obs_sceneitem_get_crop(source.item, &crop);

    SourceTransform transform;
    transform.x = static_cast<double>(position.x);
    transform.y = static_cast<double>(position.y);
    transform.width = static_cast<double>(bounds.x);
    transform.height = static_cast<double>(bounds.y);
    transform.rotation = static_cast<double>(api.obs_sceneitem_get_rot(source.item));
    transform.cropLeft = static_cast<double>(crop.left);
    transform.cropTop = static_cast<double>(crop.top);
    transform.cropRight = static_cast<double>(crop.right);
    transform.cropBottom = static_cast<double>(crop.bottom);
    transform.flipHorizontal = scale.x < 0.0F;
    transform.flipVertical = scale.y < 0.0F;
    transform.stretchToBounds =
        api.obs_sceneitem_get_bounds_type(source.item) == ObsAbi::OBS_BOUNDS_STRETCH;
    return transform;
}

void VuttaraEngine::fitSceneItemToCanvas(ObsAbi::obs_sceneitem_t* item)
{
    if (item == nullptr) {
        return;
    }

    const ObsAbi::vec2 position{
        static_cast<float>(CanvasWidth) / 2.0F,
        static_cast<float>(CanvasHeight) / 2.0F,
    };
    const ObsAbi::vec2 bounds{
        static_cast<float>(CanvasWidth),
        static_cast<float>(CanvasHeight),
    };

    const auto& api = runtime_.api();
    const ObsAbi::obs_sceneitem_crop crop{};
    const ObsAbi::vec2 scale{1.0F, 1.0F};
    api.obs_sceneitem_set_crop(item, &crop);
    api.obs_sceneitem_set_scale(item, &scale);
    api.obs_sceneitem_set_pos(item, &position);
    api.obs_sceneitem_set_rot(item, 0.0F);
    api.obs_sceneitem_set_alignment(item, 0);
    api.obs_sceneitem_set_bounds_type(item, ObsAbi::OBS_BOUNDS_SCALE_INNER);
    api.obs_sceneitem_set_bounds_alignment(item, 0);
    api.obs_sceneitem_set_bounds(item, &bounds);
}

bool VuttaraEngine::applySourceTransform(
    ManagedSource& source,
    const SourceTransform& transform,
    bool allowLocked)
{
    if (source.locked && !allowLocked) {
        return operationFail(QStringLiteral("Unlock %1 before changing its transform.").arg(source.name));
    }

    const auto& api = runtime_.api();
    ObsAbi::obs_source_t* obsSource =
        source.item != nullptr ? api.obs_sceneitem_get_source(source.item) : nullptr;
    const std::uint32_t nativeWidth =
        obsSource != nullptr ? api.obs_source_get_width(obsSource) : 0U;
    const std::uint32_t nativeHeight =
        obsSource != nullptr ? api.obs_source_get_height(obsSource) : 0U;
    if (nativeWidth > 0U) {
        source.sourceWidth = static_cast<int>(nativeWidth);
    }
    if (nativeHeight > 0U) {
        source.sourceHeight = static_cast<int>(nativeHeight);
    }

    const double x = std::clamp(finiteOr(transform.x, 960.0), -10000.0, 10000.0);
    const double y = std::clamp(finiteOr(transform.y, 540.0), -10000.0, 10000.0);
    const double width = std::clamp(finiteOr(transform.width, 1920.0), 1.0, 7680.0);
    const double height = std::clamp(finiteOr(transform.height, 1080.0), 1.0, 7680.0);
    const double rotation = std::clamp(finiteOr(transform.rotation, 0.0), -3600.0, 3600.0);
    const int maximumHorizontalCrop = std::max(0, source.sourceWidth - 1);
    const int maximumVerticalCrop = std::max(0, source.sourceHeight - 1);
    int cropLeft = std::clamp(
        static_cast<int>(std::lround(finiteOr(transform.cropLeft, 0.0))),
        0,
        maximumHorizontalCrop);
    int cropRight = std::clamp(
        static_cast<int>(std::lround(finiteOr(transform.cropRight, 0.0))),
        0,
        std::max(0, maximumHorizontalCrop - cropLeft));
    int cropTop = std::clamp(
        static_cast<int>(std::lround(finiteOr(transform.cropTop, 0.0))),
        0,
        maximumVerticalCrop);
    int cropBottom = std::clamp(
        static_cast<int>(std::lround(finiteOr(transform.cropBottom, 0.0))),
        0,
        std::max(0, maximumVerticalCrop - cropTop));

    const ObsAbi::vec2 position{
        static_cast<float>(x),
        static_cast<float>(y),
    };
    const ObsAbi::vec2 bounds{
        static_cast<float>(width),
        static_cast<float>(height),
    };
    const ObsAbi::vec2 scale{
        transform.flipHorizontal ? -1.0F : 1.0F,
        transform.flipVertical ? -1.0F : 1.0F,
    };
    const ObsAbi::obs_sceneitem_crop crop{
        cropLeft,
        cropTop,
        cropRight,
        cropBottom,
    };

    api.obs_sceneitem_set_crop(source.item, &crop);
    api.obs_sceneitem_set_scale(source.item, &scale);
    api.obs_sceneitem_set_pos(source.item, &position);
    api.obs_sceneitem_set_rot(source.item, static_cast<float>(rotation));
    api.obs_sceneitem_set_alignment(source.item, 0);
    api.obs_sceneitem_set_bounds_type(
        source.item,
        transform.stretchToBounds
            ? ObsAbi::OBS_BOUNDS_STRETCH
            : ObsAbi::OBS_BOUNDS_SCALE_INNER);
    api.obs_sceneitem_set_bounds_alignment(source.item, 0);
    api.obs_sceneitem_set_bounds(source.item, &bounds);

    lastError_.clear();
    log(QStringLiteral(
            "Source transform: %1 x=%2 y=%3 width=%4 height=%5 rotation=%6 crop=%7,%8,%9,%10 flip=%11,%12 stretch=%13")
            .arg(source.name)
            .arg(x)
            .arg(y)
            .arg(width)
            .arg(height)
            .arg(rotation)
            .arg(cropLeft)
            .arg(cropTop)
            .arg(cropRight)
            .arg(cropBottom)
            .arg(transform.flipHorizontal)
            .arg(transform.flipVertical)
            .arg(transform.stretchToBounds));
    return true;
}

}
