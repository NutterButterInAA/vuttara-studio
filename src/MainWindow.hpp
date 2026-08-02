#pragma once

#include "engine/VuttaraEngine.hpp"

#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QAction;
class QByteArray;
class QCloseEvent;
class QEvent;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPoint;
class QListWidget;
class QListWidgetItem;
class QJsonObject;
class QProcess;
class QObject;
class QProgressBar;
class QPushButton;
class QShortcut;
class QShowEvent;
class QSlider;
class QTimer;
class QToolButton;
class QWidget;
class UpdateManager;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Vuttara::VuttaraEngine* engine, QWidget* parent = nullptr);
    static bool runStage8CFix3InteractionSelfTest(QStringList& lines);
    static bool runStage9ASecureSettingsSelfTest(QStringList& lines);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct SourceGroup
    {
        QString name;
        QStringList sourceNames;
        bool expanded = true;
    };

    enum class TransformCommand
    {
        Reset,
        Fit,
        Stretch,
        Center,
        CenterHorizontal,
        CenterVertical,
        FlipHorizontal,
        FlipVertical,
        RotateClockwise,
        RotateCounterclockwise,
    };

    struct TransformHistoryEntry
    {
        QString sceneName;
        QHash<QString, Vuttara::SourceTransform> before;
        QHash<QString, Vuttara::SourceTransform> after;
        QString label;
    };

    void createMenus();
    void createPreview();
    void createSceneDock();
    void createSourceDock();
    void createAudioDock();
    void applyDefaultDockLayout(bool persist);
    void applyNativeWindowChrome();
    void toggleFullScreen();
    void openProjectDataFolder();
    void openLogsFolder();
    void showGettingStartedDialog();
    void showUpdateStatusDialog();
    void showAboutDialog();
    void showSettingsDialog(int initialPage = 0);
    void refreshHotkeys();
    void cycleScene(int direction);

    void refreshAudioDeviceLists();
    void updateAudioControls();
    void updateAudioMeters();
    void applyAudioDevice(Vuttara::AudioChannelKind kind, QComboBox* combo);
    void applyAudioVolume(Vuttara::AudioChannelKind kind, int volumePercent);
    void toggleAudioMute(Vuttara::AudioChannelKind kind);

    void startStreaming();
    void stopStreaming();
    void updateStreamingControls();
    Vuttara::StreamingSettings streamingSettings() const;
    bool saveStreamingSettings(const Vuttara::StreamingSettings& settings);
    void chooseRecordingFolder();
    void openRecordingFolder();
    void showRecordingSettingsDialog();
    void startRecording();
    void stopRecording();
    void updateRecordingControls();
    Vuttara::RecordingSettings recordingSettings() const;
    void saveRecordingSettings(const Vuttara::RecordingSettings& settings);
    void refreshRecordingEstimate();
    QString recordingOutputDirectory() const;
    QString findFfmpegExecutable() const;
    void maybeStartAutomaticRemux(const Vuttara::RecordingInfo& info);
    void startAutomaticRemux(const QString& mkvPath);
    void finishAutomaticRemux(int exitCode, int exitStatus);

    void addScene();
    void removeSelectedScene();
    void handleSceneSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void refreshSceneList();

    void showAddDisplayCaptureDialog();
    void showAddWindowCaptureDialog();
    void showSelectedSourceProperties();
    void showSelectedSourceTransform();
    void showSourceContextMenu(const QPoint& position);
    void showSourceContextMenuAtGlobal(const QPoint& globalPosition, QListWidgetItem* item);
    bool isPreviewInteractionOverlayBlocker(QObject* watched) const;
    bool shouldSuppressPreviewInteractionOverlay() const;
    void suppressPreviewInteractionOverlay();
    void syncPreviewInteractionOverlay();
    void refreshPreviewInteractionOverlay();
    void selectSourcesFromPreview(const QStringList& sourceNames);
    bool applyPreviewSourceTransforms(
        const QHash<QString, Vuttara::SourceTransform>& transforms);
    void commitPreviewSourceTransforms(
        const QHash<QString, Vuttara::SourceTransform>& originals,
        const QHash<QString, Vuttara::SourceTransform>& current);
    void cancelPreviewSourceTransforms(
        const QHash<QString, Vuttara::SourceTransform>& originals);
    bool applyTransformEdit(
        const QHash<QString, Vuttara::SourceTransform>& before,
        const QHash<QString, Vuttara::SourceTransform>& after,
        const QString& label,
        bool recordHistory = true);
    QHash<QString, Vuttara::SourceTransform> selectedSourceTransforms() const;
    void pushTransformHistory(const TransformHistoryEntry& entry);
    void undoTransform();
    void redoTransform();
    void nudgeSelectedSources(double dx, double dy);
    void duplicateSelectedSources();
    void transformSelectedSources(TransformCommand command);
    void showPreviewSourceContextMenu(
        const QString& sourceName,
        const QPoint& globalPosition);
    void renameSourceInGroups(
        const QString& sceneName,
        const QString& oldName,
        const QString& newName);
    void removeSelectedSource();
    void createSourceGroup();
    void ungroupSelectedSources();
    void removeCurrentSourceGroup();
    void handleSourceGroupTabChanged(int index);
    void refreshSourceGroupTabs(const QString& preferredGroup = {});
    void setSourceVisibility(const QStringList& sourceNames, bool visible);
    void setSourceLockState(const QStringList& sourceNames, bool locked);
    void toggleSourceGroupExpanded(const QString& groupName);
    void startSourceDockDrag(QListWidgetItem* item);
    bool handleSourceDockDrop(
        const QByteArray& payload,
        QListWidgetItem* target,
        int dropPosition);
    void selectSourceDockItem(QListWidgetItem* item, Qt::KeyboardModifiers modifiers);
    void refreshSourceRowVisuals();
    void pruneSourceGroupsForActiveScene();
    void addSourceToCurrentGroup(const QString& sourceName);
    void removeSourceFromGroups(const QString& sceneName, const QString& sourceName);
    QString currentSourceGroupName() const;
    QStringList selectedSourceNames() const;
    QJsonObject sourceGroupsState() const;
    void restoreSourceGroups(const QJsonObject& project);
    void handleSourceVisibilityChanged(QListWidgetItem* item);
    void moveSelectedSource(Vuttara::SourceOrderMovement movement);
    void toggleSelectedSourceLock();
    void fitSelectedSourceToCanvas();
    void centerSelectedSource();
    void refreshSourceList(const QString& preferredSelection = {});
    void updateSourceControls();
    void updatePreviewInformation();

    bool loadProjectState();
    bool saveProjectState();
    void migrateLegacyCaptureSettings();
    void restoreLegacyDisplayCapture();
    void restoreLegacyWindowCapture();
    QString selectedSceneName() const;
    QString selectedSourceName() const;

    Vuttara::VuttaraEngine* engine_ = nullptr;
    UpdateManager* updateManager_ = nullptr;
    QListWidget* scenesList_ = nullptr;
    QPushButton* addSceneButton_ = nullptr;
    QPushButton* removeSceneButton_ = nullptr;

    QListWidget* sourcesList_ = nullptr;
    QHash<QString, QVector<SourceGroup>> sourceGroupsByScene_;
    QToolButton* addSourceButton_ = nullptr;
    QToolButton* removeSourceButton_ = nullptr;
    QToolButton* sourceActionsButton_ = nullptr;
    QAction* addDisplaySourceAction_ = nullptr;
    QAction* addWindowSourceAction_ = nullptr;
    QLabel* previewTitle_ = nullptr;
    QLabel* previewInformation_ = nullptr;
    QWidget* previewWidget_ = nullptr;
    QWidget* previewInteractionOverlay_ = nullptr;
    bool previewInteractionMainWindowDeactivated_ = false;

    QDockWidget* scenesDock_ = nullptr;
    QDockWidget* sourcesDock_ = nullptr;
    QDockWidget* audioMixerDock_ = nullptr;
    QLabel* streamingStatusLabel_ = nullptr;
    QLabel* streamingElapsedLabel_ = nullptr;
    QLabel* streamingBitrateLabel_ = nullptr;
    QLabel* streamingDroppedLabel_ = nullptr;
    QPushButton* startStreamingButton_ = nullptr;
    QPushButton* stopStreamingButton_ = nullptr;
    QTimer* streamingTimer_ = nullptr;
    QLabel* recordingStatusLabel_ = nullptr;
    QLabel* recordingElapsedLabel_ = nullptr;
    QLabel* recordingEncoderLabel_ = nullptr;
    QLabel* recordingFileLabel_ = nullptr;
    QLabel* recordingSizeLabel_ = nullptr;
    QLabel* recordingEstimateLabel_ = nullptr;
    QPushButton* startRecordingButton_ = nullptr;
    QPushButton* stopRecordingButton_ = nullptr;
    QTimer* recordingTimer_ = nullptr;
    QProcess* remuxProcess_ = nullptr;
    QString remuxInputPath_;
    QString remuxOutputPath_;
    QString remuxPartialPath_;
    QString lastHandledRecordingPath_;
    QString lastRemuxedPath_;
    QString recordingDiagnostics_;
    bool remuxRequestedForCurrentRecording_ = false;
    QComboBox* desktopDeviceCombo_ = nullptr;
    QComboBox* microphoneDeviceCombo_ = nullptr;
    QLabel* desktopAudioState_ = nullptr;
    QLabel* microphoneAudioState_ = nullptr;
    QProgressBar* desktopAudioMeter_ = nullptr;
    QProgressBar* microphoneAudioMeter_ = nullptr;
    QLabel* desktopAudioLevel_ = nullptr;
    QLabel* microphoneAudioLevel_ = nullptr;
    QSlider* desktopVolume_ = nullptr;
    QSlider* microphoneVolume_ = nullptr;
    QLabel* desktopVolumeValue_ = nullptr;
    QLabel* microphoneVolumeValue_ = nullptr;
    QPushButton* desktopMuteButton_ = nullptr;
    QPushButton* microphoneMuteButton_ = nullptr;
    QPushButton* refreshAudioDevicesButton_ = nullptr;
    QTimer* audioMeterTimer_ = nullptr;
    QAction* sourcePropertiesAction_ = nullptr;
    QAction* sourceTransformAction_ = nullptr;
    QAction* undoTransformAction_ = nullptr;
    QAction* redoTransformAction_ = nullptr;
    QAction* duplicateSourceAction_ = nullptr;
    QAction* fitSourceAction_ = nullptr;
    QAction* centerSourceAction_ = nullptr;
    QAction* lockSourceAction_ = nullptr;
    QAction* removeSourceAction_ = nullptr;
    QAction* fullScreenAction_ = nullptr;
    QShortcut* startStreamingShortcut_ = nullptr;
    QShortcut* stopStreamingShortcut_ = nullptr;
    QShortcut* startRecordingShortcut_ = nullptr;
    QShortcut* stopRecordingShortcut_ = nullptr;
    QShortcut* desktopMuteShortcut_ = nullptr;
    QShortcut* microphoneMuteShortcut_ = nullptr;
    QShortcut* nextSceneShortcut_ = nullptr;
    QShortcut* previousSceneShortcut_ = nullptr;
    QShortcut* nudgeLeftShortcut_ = nullptr;
    QShortcut* nudgeRightShortcut_ = nullptr;
    QShortcut* nudgeUpShortcut_ = nullptr;
    QShortcut* nudgeDownShortcut_ = nullptr;
    QShortcut* nudgeLeftFastShortcut_ = nullptr;
    QShortcut* nudgeRightFastShortcut_ = nullptr;
    QShortcut* nudgeUpFastShortcut_ = nullptr;
    QShortcut* nudgeDownFastShortcut_ = nullptr;
    QVector<TransformHistoryEntry> transformUndoStack_;
    QVector<TransformHistoryEntry> transformRedoStack_;
};
