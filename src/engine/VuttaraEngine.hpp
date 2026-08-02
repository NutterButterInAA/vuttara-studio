#pragma once

#include "AudioDeviceEnumerator.hpp"
#include "DisplayEnumerator.hpp"
#include "ObsRuntime.hpp"
#include "WindowEnumerator.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QString>
#include <QVector>

#include <atomic>
#include <chrono>
#include <limits>
#include <cstdint>
#include <cstdarg>
#include <mutex>

namespace Vuttara {

struct SceneInfo
{
    QString name;
    bool active = false;
    bool removable = true;
    int sourceCount = 0;
};

struct SourceTransform
{
    double x = 960.0;
    double y = 540.0;
    double width = 1920.0;
    double height = 1080.0;
    double rotation = 0.0;
    double cropLeft = 0.0;
    double cropTop = 0.0;
    double cropRight = 0.0;
    double cropBottom = 0.0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    bool stretchToBounds = false;

    bool operator==(const SourceTransform&) const = default;
};

struct SourceInfo
{
    QString name;
    QString type;
    QString persistentId;
    bool visible = true;
    bool removable = false;
    bool locked = false;
    int orderPosition = 0;
    int sourceWidth = 1920;
    int sourceHeight = 1080;
    SourceTransform transform;
};

enum class SourcePropertyKind
{
    Text,
    Boolean,
    Integer,
    Choice,
    Color,
    Information,
};

struct SourcePropertyOption
{
    QString label;
    QJsonValue value;
};

struct SourcePropertyDefinition
{
    QString key;
    QString label;
    QString description;
    SourcePropertyKind kind = SourcePropertyKind::Text;
    QJsonValue value;
    QJsonValue defaultValue;
    QVector<SourcePropertyOption> options;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    bool readOnly = false;
};

struct SourcePropertiesModel
{
    QString sourceName;
    QString sourceType;
    QString sourceTypeName;
    QVector<SourcePropertyDefinition> properties;
};

enum class WindowCaptureMethod : long long
{
    Automatic = 0,
    BitBlt = 1,
    WindowsGraphicsCapture = 2,
};

enum class WindowMatchPriority : long long
{
    Class = 0,
    Title = 1,
    Executable = 2,
};

enum class AudioChannelKind
{
    Desktop,
    Microphone,
};

struct AudioChannelInfo
{
    AudioChannelKind kind = AudioChannelKind::Desktop;
    QString name;
    QString deviceId;
    QString deviceName;
    bool connected = false;
    bool muted = false;
    bool meterAttached = false;
    int volumePercent = 70;
    float peakDb = -std::numeric_limits<float>::infinity();
};

enum class SourceOrderMovement
{
    Up,
    Down,
    Top,
    Bottom,
};

struct StreamingSettings
{
    QString server;
    QString streamKey;
    bool useAuthentication = false;
    QString username;
    QString password;
    QString encoderId;
    int outputWidth = 1920;
    int outputHeight = 1080;
    int framesPerSecond = 60;
    int videoBitrateKbps = 6000;
    int keyframeIntervalSeconds = 2;
    int audioBitrateKbps = 160;
    bool automaticReconnect = true;
    int retryDelaySeconds = 2;
    int maximumRetries = 20;
};

enum class StreamingState
{
    Ready,
    Connecting,
    Streaming,
    Reconnecting,
    Stopping,
    Error,
};

struct StreamingInfo
{
    StreamingState state = StreamingState::Ready;
    bool active = false;
    bool reconnecting = false;
    bool stopping = false;
    QString server;
    QString encoderId;
    QString encoderName;
    int outputWidth = 1920;
    int outputHeight = 1080;
    int framesPerSecond = 60;
    int videoBitrateKbps = 6000;
    int keyframeIntervalSeconds = 2;
    int audioBitrateKbps = 160;
    qint64 elapsedMilliseconds = 0;
    std::uint64_t totalBytes = 0;
    double currentBitrateKbps = 0.0;
    int droppedFrames = 0;
    int totalFrames = 0;
    double congestion = 0.0;
    int connectTimeMilliseconds = 0;
    QString diagnostics;
    QString error;
};

struct RecordingEncoderInfo
{
    QString id;
    QString name;
    bool hardware = false;
    int priority = 0;
};

struct RecordingSettings
{
    QString outputDirectory;
    QString filenameFormat = QStringLiteral("VuttaraStudio_{date}_{time}");
    QString encoderId;
    int outputWidth = 1920;
    int outputHeight = 1080;
    int framesPerSecond = 60;
    int videoBitrateKbps = 12000;
    int audioBitrateKbps = 192;
    bool automaticRemuxToMp4 = false;
};

enum class RecordingState
{
    Ready,
    Recording,
    Stopping,
    Error,
};

struct RecordingInfo
{
    RecordingState state = RecordingState::Ready;
    bool active = false;
    bool stopping = false;
    QString outputPath;
    QString encoderId;
    QString encoderName;
    int outputWidth = 1920;
    int outputHeight = 1080;
    int framesPerSecond = 60;
    int videoBitrateKbps = 12000;
    int audioBitrateKbps = 192;
    qint64 elapsedMilliseconds = 0;
    std::uint64_t totalBytes = 0;
    QString diagnostics;
    QString error;
};

class VuttaraEngine final
{
public:
    explicit VuttaraEngine(QString logPath);
    ~VuttaraEngine();

    VuttaraEngine(const VuttaraEngine&) = delete;
    VuttaraEngine& operator=(const VuttaraEngine&) = delete;

    bool initialize();
    void shutdown();

    bool attachPreview(void* nativeWindow, std::uint32_t width, std::uint32_t height);
    void resizePreview(std::uint32_t width, std::uint32_t height);
    void detachPreview();

    [[nodiscard]] QVector<DisplayInfo> availableDisplays() const;
    [[nodiscard]] QVector<WindowInfo> availableWindows() const;
    [[nodiscard]] QVector<AudioDeviceInfo> availableDesktopAudioDevices() const;
    [[nodiscard]] QVector<AudioDeviceInfo> availableMicrophoneDevices() const;
    [[nodiscard]] AudioChannelInfo audioChannelInfo(AudioChannelKind kind) const;
    [[nodiscard]] QVector<SceneInfo> sceneInfos() const;
    [[nodiscard]] QVector<SourceInfo> sourceInfos() const;
    [[nodiscard]] SourcePropertiesModel sourceProperties(const QString& sourceName) const;
    [[nodiscard]] QString activeSceneName() const;
    [[nodiscard]] bool hasDisplayCapture() const;
    [[nodiscard]] bool hasWindowCapture() const;

    bool addScene(const QString& requestedName, QString* createdName = nullptr);
    bool removeScene(const QString& sceneName);
    bool switchScene(const QString& sceneName);

    bool addDisplayCapture(
        const DisplayInfo& display,
        bool captureCursor,
        const QString& requestedName,
        QString* createdName = nullptr);
    bool addWindowCapture(
        const WindowInfo& window,
        WindowCaptureMethod method,
        WindowMatchPriority priority,
        bool captureCursor,
        bool clientArea,
        const QString& requestedName,
        QString* createdName = nullptr);
    bool removeSource(const QString& sourceName);
    bool duplicateSource(const QString& sourceName, QString* createdName = nullptr);
    bool setSourceVisible(const QString& sourceName, bool visible);
    bool setSourceLocked(const QString& sourceName, bool locked);
    bool moveSource(const QString& sourceName, SourceOrderMovement movement);
    bool setSourceTransform(const QString& sourceName, const SourceTransform& transform);
    bool fitSourceToCanvas(const QString& sourceName);
    bool centerSource(const QString& sourceName);
    bool applySourceProperties(
        const QString& sourceName,
        const QJsonObject& values,
        QString* updatedName = nullptr);

    bool setAudioDevice(AudioChannelKind kind, const AudioDeviceInfo& device);
    bool disconnectAudioDevice(AudioChannelKind kind);
    bool setAudioVolume(AudioChannelKind kind, int volumePercent);
    bool setAudioMuted(AudioChannelKind kind, bool muted);

    [[nodiscard]] QVector<RecordingEncoderInfo> availableRecordingEncoders() const;
    bool startStreaming(const StreamingSettings& settings);
    bool stopStreaming();
    void forceStopStreaming();
    StreamingInfo streamingInfo();
    [[nodiscard]] bool isStreaming() const;
    bool startRecording(const RecordingSettings& settings);
    bool stopRecording();
    void forceStopRecording();
    RecordingInfo recordingInfo();
    [[nodiscard]] bool isRecording() const;

    [[nodiscard]] QJsonObject projectState() const;
    bool restoreProjectState(const QJsonObject& project);
    bool resetProjectToDefault();

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] bool nativeLoggingActive() const;
    [[nodiscard]] QString versionString() const;
    [[nodiscard]] QString graphicsDescription() const;
    [[nodiscard]] QString audioDescription() const;
    [[nodiscard]] QString lastError() const;

private:
    struct ManagedSource
    {
        QString name;
        QString type;
        QString persistentId;
        bool visible = true;
        bool removable = false;
        bool locked = false;
        bool captureCursor = true;
        bool clientArea = true;
        WindowCaptureMethod windowMethod = WindowCaptureMethod::Automatic;
        WindowMatchPriority windowPriority = WindowMatchPriority::Title;
        std::uint32_t color = 0;
        int sourceWidth = 1920;
        int sourceHeight = 1080;
        ObsAbi::obs_sceneitem_t* item = nullptr;
    };

    struct AudioChannelState
    {
        AudioChannelKind kind = AudioChannelKind::Desktop;
        std::uint32_t outputIndex = 1;
        QString name;
        QString deviceId;
        QString deviceName;
        int volumePercent = 70;
        bool muted = false;
        ObsAbi::obs_source_t* source = nullptr;
        ObsAbi::obs_fader_t* fader = nullptr;
        ObsAbi::obs_volmeter_t* meter = nullptr;
        std::atomic<float> peakDb{-std::numeric_limits<float>::infinity()};
    };

    struct ManagedScene
    {
        QString name;
        bool removable = true;
        ObsAbi::obs_scene_t* scene = nullptr;
        ObsAbi::obs_source_t* source = nullptr;
        QVector<ManagedSource> sources;
    };

    static void renderPreview(void* context, std::uint32_t width, std::uint32_t height);
    static void libobsLogCallback(int level, const char* format, va_list arguments, void* context);
    static void audioMeterCallback(
        void* context,
        const float magnitude[ObsAbi::MaxAudioChannels],
        const float peak[ObsAbi::MaxAudioChannels],
        const float inputPeak[ObsAbi::MaxAudioChannels]);

    void render(std::uint32_t width, std::uint32_t height);
    void appendNativeLog(int level, const QString& message) const;
    void log(const QString& message) const;
    bool fail(const QString& message);
    bool operationFail(const QString& message);
    bool validateCoreData(const QString& coreDataPath);
    bool loadModule(
        const QString& moduleFileName,
        const QString& moduleDataDirectory,
        ObsAbi::obs_module_t** module);
    bool loadOptionalModule(
        const QString& moduleFileName,
        const QString& moduleDataDirectory,
        QByteArray& modulePathStorage,
        QByteArray& moduleDataStorage,
        ObsAbi::obs_module_t** module);
    bool registeredOutputType(const char* id) const;
    bool registeredServiceType(const char* id) const;
    bool registeredEncoderType(const char* id) const;
    bool tryStartStreamingWithEncoder(
        const RecordingEncoderInfo& encoder,
        const StreamingSettings& settings,
        QString* failureReason);
    void finalizeStreamingIfStopped();
    void releaseStreamingResources();
    QString uniqueRecordingPath(const RecordingSettings& settings) const;
    bool tryStartRecordingWithEncoder(
        const RecordingEncoderInfo& encoder,
        const RecordingSettings& settings,
        const QString& outputPath,
        QString* failureReason);
    void finalizeRecordingIfStopped();
    void releaseRecordingResources();

    ManagedScene* activeScene();
    const ManagedScene* activeScene() const;
    ManagedScene* findScene(const QString& sceneName);
    const ManagedScene* findScene(const QString& sceneName) const;
    ManagedSource* findSource(ManagedScene& scene, const QString& sourceName);
    const ManagedSource* findSource(const ManagedScene& scene, const QString& sourceName) const;

    QString uniqueSceneName(const QString& requestedName) const;
    QString uniqueSourceName(const QString& requestedName, const QString& fallbackBaseName) const;
    bool createSceneInternal(const QString& name, bool removable, bool addFoundationColor);
    bool createFoundationColor(ManagedScene& scene);
    bool addColorSourceToScene(
        ManagedScene& scene,
        const QString& requestedName,
        std::uint32_t color,
        int sourceWidth,
        int sourceHeight,
        bool removable,
        bool locked,
        QString* createdName = nullptr);
    void releaseAllScenes();
    AudioChannelState& audioChannel(AudioChannelKind kind);
    const AudioChannelState& audioChannel(AudioChannelKind kind) const;
    bool createAudioChannel(AudioChannelState& channel, const AudioDeviceInfo& device);
    void releaseAudioChannel(AudioChannelState& channel);
    void releaseAllAudioChannels();
    AudioChannelInfo audioChannelInfo(const AudioChannelState& channel) const;
    bool restoreAudioChannel(AudioChannelKind kind, const QJsonObject& state);
    bool addDisplayCaptureToScene(
        ManagedScene& scene,
        const DisplayInfo& display,
        bool captureCursor,
        const QString& requestedName,
        bool enforceLimit,
        QString* createdName);
    bool addWindowCaptureToScene(
        ManagedScene& scene,
        const WindowInfo& window,
        WindowCaptureMethod method,
        WindowMatchPriority priority,
        bool captureCursor,
        bool clientArea,
        const QString& requestedName,
        bool enforceLimit,
        QString* createdName);
    SourceInfo sourceInfo(const ManagedSource& source) const;
    SourceTransform sourceTransform(const ManagedSource& source) const;
    void fitSceneItemToCanvas(ObsAbi::obs_sceneitem_t* item);
    bool applySourceTransform(ManagedSource& source, const SourceTransform& transform, bool allowLocked);

    QString logPath_;
    QString lastError_;
    QString versionString_;
    QString graphicsDescription_;
    QByteArray graphicsModuleUtf8_;
    QByteArray configPathUtf8_;
    QByteArray coreDataPathUtf8_;
    QByteArray imageModulePathUtf8_;
    QByteArray imageModuleDataPathUtf8_;
    QByteArray winCaptureModulePathUtf8_;
    QByteArray winCaptureModuleDataPathUtf8_;
    QByteArray wasapiModulePathUtf8_;
    QByteArray wasapiModuleDataPathUtf8_;
    QByteArray ffmpegModulePathUtf8_;
    QByteArray ffmpegModuleDataPathUtf8_;
    QByteArray x264ModulePathUtf8_;
    QByteArray x264ModuleDataPathUtf8_;
    QByteArray nvencModulePathUtf8_;
    QByteArray nvencModuleDataPathUtf8_;
    QByteArray qsvModulePathUtf8_;
    QByteArray qsvModuleDataPathUtf8_;
    QByteArray outputsModulePathUtf8_;
    QByteArray outputsModuleDataPathUtf8_;
    QByteArray rtmpServicesModulePathUtf8_;
    QByteArray rtmpServicesModuleDataPathUtf8_;

    ObsRuntime runtime_;
    ObsAbi::obs_module_t* imageSourceModule_ = nullptr;
    ObsAbi::obs_module_t* winCaptureModule_ = nullptr;
    ObsAbi::obs_module_t* wasapiModule_ = nullptr;
    ObsAbi::obs_module_t* ffmpegModule_ = nullptr;
    ObsAbi::obs_module_t* x264Module_ = nullptr;
    ObsAbi::obs_module_t* nvencModule_ = nullptr;
    ObsAbi::obs_module_t* qsvModule_ = nullptr;
    ObsAbi::obs_module_t* outputsModule_ = nullptr;
    ObsAbi::obs_module_t* rtmpServicesModule_ = nullptr;
    ObsAbi::obs_output_t* streamingOutput_ = nullptr;
    ObsAbi::obs_encoder_t* streamingVideoEncoder_ = nullptr;
    ObsAbi::obs_encoder_t* streamingAudioEncoder_ = nullptr;
    ObsAbi::obs_service_t* streamingService_ = nullptr;
    StreamingInfo streamingInfo_;
    StreamingSettings activeStreamingSettings_;
    std::chrono::steady_clock::time_point streamingStartedAt_{};
    std::chrono::steady_clock::time_point streamingStatsUpdatedAt_{};
    std::uint64_t streamingStatsLastBytes_ = 0;
    ObsAbi::obs_output_t* recordingOutput_ = nullptr;
    ObsAbi::obs_encoder_t* recordingVideoEncoder_ = nullptr;
    ObsAbi::obs_encoder_t* recordingAudioEncoder_ = nullptr;
    RecordingInfo recordingInfo_;
    RecordingSettings activeRecordingSettings_;
    std::chrono::steady_clock::time_point recordingStartedAt_{};
    AudioChannelState desktopAudio_{AudioChannelKind::Desktop, 1, QStringLiteral("Desktop Audio")};
    AudioChannelState microphoneAudio_{AudioChannelKind::Microphone, 2, QStringLiteral("Mic/Aux")};
    QVector<ManagedScene> scenes_;
    int activeSceneIndex_ = -1;
    ObsAbi::obs_display_t* display_ = nullptr;
    std::atomic_bool ready_{false};
    std::atomic_bool nativeLoggingActive_{false};
    mutable std::mutex logMutex_;
};

}
