#include "MainWindow.hpp"

#include "AppPaths.hpp"
#include "PreviewWidget.hpp"
#include "UpdateManager.hpp"
#include "engine/VuttaraEngine.hpp"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequenceEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLibrary>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QContextMenuEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPixmap>
#include <QPair>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QStyle>
#include <QStyleOption>
#include <QtMath>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

constexpr int NameRole = Qt::UserRole;
constexpr int TypeRole = Qt::UserRole + 1;
constexpr int RemovableRole = Qt::UserRole + 2;
constexpr int LockedRole = Qt::UserRole + 3;
constexpr int GroupNameRole = Qt::UserRole + 4;
constexpr int GroupRowRole = Qt::UserRole + 5;

QString protectSettingSecret(const QString& plaintext)
{
    if (plaintext.isEmpty()) {
        return {};
    }
#ifdef Q_OS_WIN
    const QByteArray bytes = plaintext.toUtf8();
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()));
    input.cbData = static_cast<DWORD>(bytes.size());
    DATA_BLOB output{};
    if (!CryptProtectData(
            &input,
            L"Vuttara Studio output credential",
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output)) {
        return {};
    }
    const QByteArray protectedBytes(
        reinterpret_cast<const char*>(output.pbData),
        static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return QStringLiteral("dpapi:%1").arg(QString::fromLatin1(protectedBytes.toBase64()));
#else
    return QStringLiteral("local:%1").arg(QString::fromLatin1(plaintext.toUtf8().toBase64()));
#endif
}

QString unprotectSettingSecret(const QString& protectedValue)
{
    if (protectedValue.isEmpty()) {
        return {};
    }
#ifdef Q_OS_WIN
    if (!protectedValue.startsWith(QStringLiteral("dpapi:"))) {
        return {};
    }
    const QByteArray protectedBytes = QByteArray::fromBase64(protectedValue.mid(6).toLatin1());
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedBytes.constData()));
    input.cbData = static_cast<DWORD>(protectedBytes.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(
            &input,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output)) {
        return {};
    }
    const QString plaintext = QString::fromUtf8(
        reinterpret_cast<const char*>(output.pbData),
        static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return plaintext;
#else
    if (!protectedValue.startsWith(QStringLiteral("local:"))) return {};
    return QString::fromUtf8(QByteArray::fromBase64(protectedValue.mid(6).toLatin1()));
#endif
}

QIcon visibilityStateIcon(bool visible)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor foreground(226, 226, 232);
    painter.setPen(QPen(foreground, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    QPainterPath eye;
    eye.moveTo(2.0, 9.0);
    eye.cubicTo(5.2, 4.4, 12.8, 4.4, 16.0, 9.0);
    eye.cubicTo(12.8, 13.6, 5.2, 13.6, 2.0, 9.0);
    painter.drawPath(eye);
    painter.setBrush(foreground);
    painter.drawEllipse(QPointF(9.0, 9.0), 2.2, 2.2);
    if (!visible) {
        painter.setPen(QPen(QColor(244, 125, 125), 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(3.0, 3.0), QPointF(15.0, 15.0));
    }
    return QIcon(pixmap);
}

QIcon lockStateIcon(bool locked)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor foreground = locked ? QColor(246, 184, 79) : QColor(190, 190, 200);
    painter.setPen(QPen(foreground, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    QRectF body(4.0, 8.0, 10.0, 7.0);
    painter.drawRoundedRect(body, 1.5, 1.5);
    if (locked) {
        painter.drawArc(QRectF(5.5, 2.5, 7.0, 9.0), 0, 180 * 16);
    } else {
        painter.drawArc(QRectF(6.5, 2.5, 7.0, 9.0), 0, 165 * 16);
        painter.drawLine(QPointF(6.5, 6.8), QPointF(6.5, 8.0));
    }
    return QIcon(pixmap);
}

constexpr char SourceDockMimeType[] = "application/x-vuttara-source-row";
constexpr int SourceDropAbove = 0;
constexpr int SourceDropOn = 1;
constexpr int SourceDropBelow = 2;

class SourcesListWidget final : public QListWidget
{
public:
    using DropCallback = std::function<bool(const QByteArray&, QListWidgetItem*, int)>;

    explicit SourcesListWidget(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
        setDefaultDropAction(Qt::MoveAction);
        setDropIndicatorShown(false);
    }

    void setDropCallback(DropCallback callback)
    {
        dropCallback_ = std::move(callback);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(SourceDockMimeType)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        QListWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (!event->mimeData()->hasFormat(SourceDockMimeType)) {
            QListWidget::dragMoveEvent(event);
            return;
        }
        updateDropTarget(event->position().toPoint());
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        clearDropTarget();
        event->accept();
    }

    void dropEvent(QDropEvent* event) override
    {
        if (!event->mimeData()->hasFormat(SourceDockMimeType)) {
            QListWidget::dropEvent(event);
            return;
        }
        updateDropTarget(event->position().toPoint());
        const bool accepted = dropCallback_ && dropCallback_(
            event->mimeData()->data(SourceDockMimeType),
            dropTarget_,
            dropPosition_);
        clearDropTarget();
        if (accepted) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else {
            event->ignore();
        }
    }

    void paintEvent(QPaintEvent* event) override
    {
        QListWidget::paintEvent(event);
        if (!dropActive_) {
            return;
        }

        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor accent(115, 202, 255);
        if (dropTarget_ != nullptr && dropPosition_ == SourceDropOn) {
            const QRect rectangle = visualItemRect(dropTarget_).adjusted(1, 1, -1, -1);
            painter.setPen(QPen(accent, 1.5));
            painter.setBrush(QColor(115, 202, 255, 36));
            painter.drawRoundedRect(rectangle, 4.0, 4.0);
            return;
        }

        int y = viewport()->height() - 2;
        if (dropTarget_ != nullptr) {
            const QRect rectangle = visualItemRect(dropTarget_);
            y = dropPosition_ == SourceDropAbove ? rectangle.top() : rectangle.bottom();
        }
        painter.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(4.0, y), QPointF(viewport()->width() - 4.0, y));
    }

private:
    void updateDropTarget(const QPoint& position)
    {
        dropActive_ = true;
        dropTarget_ = itemAt(position);
        dropPosition_ = SourceDropBelow;
        if (dropTarget_ != nullptr) {
            const QRect rectangle = visualItemRect(dropTarget_);
            const bool groupRow = dropTarget_->data(GroupRowRole).toBool();
            const int upperBand = rectangle.top() + rectangle.height() / 3;
            const int lowerBand = rectangle.bottom() - rectangle.height() / 3;
            if (groupRow && position.y() >= upperBand && position.y() <= lowerBand) {
                dropPosition_ = SourceDropOn;
            } else {
                dropPosition_ = position.y() < rectangle.center().y()
                    ? SourceDropAbove
                    : SourceDropBelow;
            }
        }
        viewport()->update();
    }

    void clearDropTarget()
    {
        dropActive_ = false;
        dropTarget_ = nullptr;
        dropPosition_ = SourceDropBelow;
        viewport()->update();
    }

    DropCallback dropCallback_;
    QListWidgetItem* dropTarget_ = nullptr;
    int dropPosition_ = SourceDropBelow;
    bool dropActive_ = false;
};

class SourceDockRow final : public QWidget
{
public:
    using ActivateCallback = std::function<void(Qt::KeyboardModifiers)>;
    using VoidCallback = std::function<void()>;
    using DragCallback = std::function<void()>;
    using ContextCallback = std::function<void(const QPoint&)>;

    SourceDockRow(
        const QString& title,
        const QString& sourceType,
        bool groupRow,
        bool expanded,
        bool visible,
        bool locked,
        bool cropped,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , groupRow_(groupRow)
    {
        setObjectName(QStringLiteral("sourceDockRow"));
        setAttribute(Qt::WA_StyledBackground, true);
        setMinimumHeight(groupRow ? 31 : 29);
        setStyleSheet(QStringLiteral(
            "QWidget#sourceDockRow { border-radius: 4px; background: transparent; }"
            "QWidget#sourceDockRow[selected=\"true\"] { background: rgba(118, 86, 224, 118); }"
            "QWidget#sourceDockRow[current=\"true\"] { border: 1px solid rgba(155, 127, 255, 185); }"
            "QWidget#sourceDockRow[hiddenSource=\"true\"] QLabel#sourceNameLabel { color: rgba(220, 220, 228, 135); }"
            "QWidget#sourceDockRow[groupRow=\"true\"] QLabel#sourceNameLabel { font-weight: 600; }"
            "QToolButton { border: 0; background: transparent; padding: 1px; border-radius: 3px; }"
            "QToolButton:hover { background: rgba(255, 255, 255, 24); }"));

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(groupRow ? 3 : 17, 1, 3, 1);
        layout->setSpacing(4);

        dragHandle_ = new QLabel(QStringLiteral("≡"), this);
        dragHandle_->setObjectName(QStringLiteral("sourceDragHandle"));
        dragHandle_->setAlignment(Qt::AlignCenter);
        dragHandle_->setFixedWidth(14);
        dragHandle_->setToolTip(QStringLiteral("Source order handle"));
        dragHandle_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        layout->addWidget(dragHandle_);

        disclosure_ = new QToolButton(this);
        disclosure_->setObjectName(QStringLiteral("sourceGroupDisclosure"));
        disclosure_->setAutoRaise(true);
        disclosure_->setFocusPolicy(Qt::NoFocus);
        disclosure_->setFixedSize(18, 22);
        disclosure_->setVisible(groupRow);
        layout->addWidget(disclosure_);

        typeIcon_ = new QLabel(this);
        typeIcon_->setObjectName(QStringLiteral("sourceTypeIcon"));
        typeIcon_->setFixedSize(18, 18);
        typeIcon_->setAlignment(Qt::AlignCenter);
        typeIcon_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        layout->addWidget(typeIcon_);

        nameLabel_ = new QLabel(title, this);
        nameLabel_->setObjectName(QStringLiteral("sourceNameLabel"));
        nameLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
        nameLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        layout->addWidget(nameLabel_, 1);

        cropBadge_ = new QLabel(QStringLiteral("CROP"), this);
        cropBadge_->setObjectName(QStringLiteral("sourceCropBadge"));
        cropBadge_->setAlignment(Qt::AlignCenter);
        cropBadge_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        cropBadge_->setVisible(cropped && !groupRow);
        cropBadge_->setStyleSheet(QStringLiteral(
            "QLabel#sourceCropBadge { color: rgb(90, 235, 202); background: rgba(24, 120, 103, 90);"
            " border: 1px solid rgba(90, 235, 202, 130); border-radius: 3px; padding: 0 3px; font-size: 8px; }"));
        layout->addWidget(cropBadge_);

        visibilityButton_ = new QToolButton(this);
        visibilityButton_->setObjectName(QStringLiteral("sourceVisibilityButton"));
        visibilityButton_->setAutoRaise(true);
        visibilityButton_->setFocusPolicy(Qt::NoFocus);
        visibilityButton_->setFixedSize(24, 24);
        layout->addWidget(visibilityButton_);

        lockButton_ = new QToolButton(this);
        lockButton_->setObjectName(QStringLiteral("sourceLockButton"));
        lockButton_->setAutoRaise(true);
        lockButton_->setFocusPolicy(Qt::NoFocus);
        lockButton_->setFixedSize(24, 24);
        layout->addWidget(lockButton_);

        connect(disclosure_, &QToolButton::clicked, this, [this]() {
            if (expandCallback_) expandCallback_();
        });
        connect(visibilityButton_, &QToolButton::clicked, this, [this]() {
            if (visibilityCallback_) visibilityCallback_();
        });
        connect(lockButton_, &QToolButton::clicked, this, [this]() {
            if (lockCallback_) lockCallback_();
        });

        setGroupExpanded(expanded);
        setSourceState(visible, locked, cropped);
        setSourceType(sourceType);
        setProperty("groupRow", groupRow_);
    }

    void setCallbacks(
        ActivateCallback activate,
        DragCallback drag,
        VoidCallback expand,
        VoidCallback visibility,
        VoidCallback lock,
        VoidCallback doubleClick,
        ContextCallback context)
    {
        activateCallback_ = std::move(activate);
        dragCallback_ = std::move(drag);
        expandCallback_ = std::move(expand);
        visibilityCallback_ = std::move(visibility);
        lockCallback_ = std::move(lock);
        doubleClickCallback_ = std::move(doubleClick);
        contextCallback_ = std::move(context);
    }

    void setSelectionVisual(bool selected, bool current)
    {
        setProperty("selected", selected);
        setProperty("current", current);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

    void setGroupExpanded(bool expanded)
    {
        expanded_ = expanded;
        if (disclosure_ != nullptr) {
            disclosure_->setText(expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
            disclosure_->setToolTip(expanded
                ? QStringLiteral("Collapse source group")
                : QStringLiteral("Expand source group"));
        }
        if (groupRow_ && typeIcon_ != nullptr) {
            typeIcon_->setPixmap(style()->standardIcon(
                expanded ? QStyle::SP_DirOpenIcon : QStyle::SP_DirClosedIcon)
                                     .pixmap(16, 16));
        }
    }

    void setSourceState(bool visible, bool locked, bool cropped)
    {
        visible_ = visible;
        locked_ = locked;
        setProperty("hiddenSource", !visible && !groupRow_);
        visibilityButton_->setIcon(visibilityStateIcon(visible));
        visibilityButton_->setToolTip(visible
            ? QStringLiteral("Hide")
            : QStringLiteral("Show"));
        lockButton_->setIcon(lockStateIcon(locked));
        lockButton_->setToolTip(locked
            ? QStringLiteral("Unlock")
            : QStringLiteral("Lock"));
        cropBadge_->setVisible(cropped && !groupRow_);
        style()->unpolish(this);
        style()->polish(this);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragPressPosition_ = event->position();
            dragPressed_ = true;
            if (activateCallback_) {
                activateCallback_(event->modifiers());
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (
            dragPressed_ &&
            event->buttons().testFlag(Qt::LeftButton) &&
            QLineF(dragPressPosition_, event->position()).length() >= QApplication::startDragDistance()) {
            dragPressed_ = false;
            if (dragCallback_) {
                dragCallback_();
            }
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragPressed_ = false;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && doubleClickCallback_) {
            doubleClickCallback_();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (contextCallback_) {
            contextCallback_(event->globalPos());
            event->accept();
            return;
        }
        QWidget::contextMenuEvent(event);
    }

private:
    void setSourceType(const QString& sourceType)
    {
        if (groupRow_) {
            return;
        }
        const QStyle::StandardPixmap standardIcon =
            sourceType == QStringLiteral("display_capture")
            ? QStyle::SP_ComputerIcon
            : sourceType == QStringLiteral("window_capture")
                ? QStyle::SP_TitleBarNormalButton
                : QStyle::SP_FileIcon;
        typeIcon_->setPixmap(style()->standardIcon(standardIcon).pixmap(16, 16));
    }

    QLabel* dragHandle_ = nullptr;
    QToolButton* disclosure_ = nullptr;
    QLabel* typeIcon_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QLabel* cropBadge_ = nullptr;
    QToolButton* visibilityButton_ = nullptr;
    QToolButton* lockButton_ = nullptr;
    ActivateCallback activateCallback_;
    DragCallback dragCallback_;
    VoidCallback expandCallback_;
    VoidCallback visibilityCallback_;
    VoidCallback lockCallback_;
    VoidCallback doubleClickCallback_;
    ContextCallback contextCallback_;
    QPointF dragPressPosition_;
    bool dragPressed_ = false;
    bool groupRow_ = false;
    bool expanded_ = true;
    bool visible_ = true;
    bool locked_ = false;
};

Vuttara::WindowCaptureMethod windowMethodFromIndex(int index)
{
    switch (index) {
    case 1:
        return Vuttara::WindowCaptureMethod::BitBlt;
    case 2:
        return Vuttara::WindowCaptureMethod::WindowsGraphicsCapture;
    default:
        return Vuttara::WindowCaptureMethod::Automatic;
    }
}

Vuttara::WindowMatchPriority windowPriorityFromIndex(int index)
{
    switch (index) {
    case 0:
        return Vuttara::WindowMatchPriority::Class;
    case 2:
        return Vuttara::WindowMatchPriority::Executable;
    default:
        return Vuttara::WindowMatchPriority::Title;
    }
}


QString humanBytes(std::uint64_t bytes)
{
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return QStringLiteral("%1 GB").arg(
            value / static_cast<double>(1024ULL * 1024ULL * 1024ULL),
            0,
            'f',
            1);
    }
    return QStringLiteral("%1 MB").arg(
        value / static_cast<double>(1024ULL * 1024ULL),
        0,
        'f',
        0);
}

std::uint64_t estimatedRecordingBytes(
    const Vuttara::RecordingSettings& settings,
    qint64 seconds)
{
    const std::uint64_t totalKbps = static_cast<std::uint64_t>(
        std::max(0, settings.videoBitrateKbps) +
        std::max(0, settings.audioBitrateKbps));
    return totalKbps * 1000ULL * static_cast<std::uint64_t>(std::max<qint64>(0, seconds)) / 8ULL;
}


constexpr double PreviewCanvasWidth = 1920.0;
constexpr double PreviewCanvasHeight = 1080.0;
constexpr double PreviewMinimumSourceSize = 20.0;
constexpr double PreviewHandleHitRadius = 10.0;

class PreviewInteractionOverlay final : public QWidget
{
public:
    enum class Handle
    {
        None,
        Move,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left,
    };

    using TransformMap = QHash<QString, Vuttara::SourceTransform>;
    using SourcesProvider = std::function<QVector<Vuttara::SourceInfo>()>;
    using SelectionProvider = std::function<QStringList()>;
    using SelectionCallback = std::function<void(const QStringList&)>;
    using ApplyCallback = std::function<bool(const TransformMap&)>;
    using CommitCallback = std::function<void(const TransformMap&, const TransformMap&)>;
    using CancelCallback = std::function<void(const TransformMap&)>;
    using SourceCallback = std::function<void(const QString&)>;
    using ContextCallback = std::function<void(const QString&, const QPoint&)>;
    using BooleanProvider = std::function<bool()>;
    using StatusCallback = std::function<void(const QString&)>;

    explicit PreviewInteractionOverlay(QWidget* owner)
        : QWidget(
              owner,
              Qt::Tool |
                  Qt::FramelessWindowHint |
                  Qt::NoDropShadowWindowHint |
                  Qt::WindowDoesNotAcceptFocus |
                  Qt::WindowTransparentForInput)
    {
        setObjectName(QStringLiteral("previewInteractionOverlay"));
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAutoFillBackground(false);
        setMouseTracking(true);
        hide();
    }

    void setInputProxy(QWidget* inputProxy)
    {
        inputProxy_ = inputProxy;
    }

    void configure(
        SourcesProvider sourcesProvider,
        SelectionProvider selectionProvider,
        SelectionCallback selectionCallback,
        ApplyCallback applyCallback,
        CommitCallback commitCallback,
        CancelCallback cancelCallback,
        SourceCallback propertiesCallback,
        ContextCallback contextCallback,
        BooleanProvider editAllowedProvider,
        StatusCallback statusCallback)
    {
        sourcesProvider_ = std::move(sourcesProvider);
        selectionProvider_ = std::move(selectionProvider);
        selectionCallback_ = std::move(selectionCallback);
        applyCallback_ = std::move(applyCallback);
        commitCallback_ = std::move(commitCallback);
        cancelCallback_ = std::move(cancelCallback);
        propertiesCallback_ = std::move(propertiesCallback);
        contextCallback_ = std::move(contextCallback);
        editAllowedProvider_ = std::move(editAllowedProvider);
        statusCallback_ = std::move(statusCallback);
    }

    void refreshOverlay()
    {
        updateCursorForPosition(lastPointerPosition_);
        update();
    }

    void cancelActiveInteraction()
    {
        if (dragging_ && cancelCallback_ && !originalTransforms_.isEmpty()) {
            cancelCallback_(originalTransforms_);
        }
        finishInteraction();
    }

    bool ownsPointerInteraction() const
    {
        return dragging_ || marqueeSelecting_;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF canvas = canvasRect();
        if (canvas.isEmpty()) {
            return;
        }

        painter.setPen(QPen(QColor(255, 255, 255, 42), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(canvas.adjusted(0.5, 0.5, -0.5, -0.5));

        const QVector<Vuttara::SourceInfo> sources = currentSources();
        const QStringList selectedNames = currentSelection();
        drawSelectedSources(painter, sources, selectedNames);
        drawAlignmentGuides(painter);

        if (marqueeSelecting_) {
            const QRectF rectangle = QRectF(marqueeStart_, marqueeCurrent_).normalized();
            painter.setPen(QPen(QColor(115, 202, 255), 1.5, Qt::DashLine));
            painter.setBrush(QColor(115, 202, 255, 28));
            painter.drawRect(rectangle);
        }

        drawHint(painter, sources, selectedNames);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        lastPointerPosition_ = event->position();
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        bool insideCanvas = false;
        const QPointF canvasPosition = widgetToCanvas(event->position(), &insideCanvas);
        if (!insideCanvas) {
            event->accept();
            return;
        }

        const QVector<Vuttara::SourceInfo> sources = currentSources();
        QStringList selection = normalizedSelection(currentSelection(), sources);
        const bool toggle = event->modifiers().testFlag(Qt::ControlModifier);
        Handle handle = handleAt(event->position(), sources, selection);
        const auto hit = topSourceAt(event->position(), sources);

        if (toggle && hit.has_value()) {
            if (selection.contains(hit->name)) {
                selection.removeAll(hit->name);
            } else {
                selection.append(hit->name);
            }
            emitSelection(selection);
            event->accept();
            updateCursorForPosition(event->position());
            update();
            return;
        }

        if (handle == Handle::None && hit.has_value()) {
            if (!selection.contains(hit->name)) {
                selection = {hit->name};
            }
            emitSelection(selection);
            handle = Handle::Move;
        }

        if (handle == Handle::None && !hit.has_value()) {
            marqueeSelecting_ = true;
            marqueeStart_ = event->position();
            marqueeCurrent_ = event->position();
            marqueeBaseSelection_ = toggle ? selection : QStringList{};
            if (!toggle) {
                emitSelection({});
            }
            capturePointer();
            event->accept();
            update();
            return;
        }

        selection = normalizedSelection(currentSelection(), sources);
        if (selection.isEmpty()) {
            event->accept();
            return;
        }
        for (const QString& name : selection) {
            const auto source = findSource(sources, name);
            if (source.has_value() && source->locked) {
                postStatus(QStringLiteral("%1 is locked. Unlock all selected sources before editing them.")
                               .arg(source->name));
                event->accept();
                refreshOverlay();
                return;
            }
        }
        if (!editingAllowed()) {
            postStatus(QStringLiteral("Preview editing is temporarily unavailable while the engine is busy."));
            event->accept();
            return;
        }

        originalTransforms_.clear();
        currentTransforms_.clear();
        for (const QString& name : selection) {
            const auto source = findSource(sources, name);
            if (source.has_value() && source->visible) {
                originalTransforms_.insert(name, source->transform);
                currentTransforms_.insert(name, source->transform);
            }
        }
        if (originalTransforms_.isEmpty()) {
            postStatus(QStringLiteral("Hidden sources can be selected in the Sources dock, but they cannot be dragged in the preview."));
            event->accept();
            return;
        }

        dragging_ = true;
        changed_ = false;
        activeHandle_ = handle;
        activeSourceName_ =
            (handle != Handle::Move && selection.size() == 1)
            ? selection.first()
            : (hit.has_value() ? hit->name : selection.last());
        pressCanvasPosition_ = canvasPosition;
        originalGroupBounds_ = transformMapBounds(originalTransforms_);
        activeSourceInfo_ = findSource(sources, activeSourceName_);
        alignmentGuides_.clear();
        capturePointer();
        updateCursorForHandle(activeHandle_);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        lastPointerPosition_ = event->position();

        if (marqueeSelecting_) {
            marqueeCurrent_ = event->position();
            QStringList selection = marqueeBaseSelection_;
            const QRectF marquee = QRectF(marqueeStart_, marqueeCurrent_).normalized();
            for (const Vuttara::SourceInfo& source : currentSources()) {
                if (!source.visible) {
                    continue;
                }
                QPainterPath path;
                path.addPolygon(sourcePolygon(source.transform));
                if (path.intersects(marquee) || marquee.contains(path.boundingRect())) {
                    if (!selection.contains(source.name)) {
                        selection.append(source.name);
                    }
                }
            }
            emitSelection(selection);
            update();
            event->accept();
            return;
        }

        if (!dragging_) {
            updateCursorForPosition(event->position());
            update();
            event->accept();
            return;
        }

        const QPointF canvasPosition = widgetToCanvas(event->position(), nullptr);
        TransformMap candidate;
        alignmentGuides_.clear();

        if (activeHandle_ == Handle::Move) {
            const QPointF rawDelta = canvasPosition - pressCanvasPosition_;
            QPointF delta = rawDelta;
            const QRectF moved = originalGroupBounds_.translated(delta);
            delta += snappingTranslation(moved, currentSources(), originalTransforms_.keys());
            for (auto iterator = originalTransforms_.cbegin(); iterator != originalTransforms_.cend(); ++iterator) {
                Vuttara::SourceTransform transform = iterator.value();
                transform.x += delta.x();
                transform.y += delta.y();
                candidate.insert(iterator.key(), transform);
            }
        } else if (
            originalTransforms_.size() == 1 &&
            event->modifiers().testFlag(Qt::AltModifier) &&
            activeSourceInfo_.has_value()) {
            candidate.insert(
                activeSourceName_,
                croppedTransform(*activeSourceInfo_, activeHandle_, canvasPosition));
        } else if (originalTransforms_.size() == 1) {
            const QString name = originalTransforms_.cbegin().key();
            Vuttara::SourceTransform transform = resizedTransform(
                originalTransforms_.cbegin().value(),
                activeHandle_,
                canvasPosition,
                event->modifiers().testFlag(Qt::ShiftModifier));
            const QRectF bounds = transformBounds(transform);
            const QPointF snap = snappingTranslationForResize(
                bounds,
                activeHandle_,
                currentSources(),
                originalTransforms_.keys());
            if (!qFuzzyIsNull(snap.x()) || !qFuzzyIsNull(snap.y())) {
                transform.x += snap.x();
                transform.y += snap.y();
            }
            candidate.insert(name, transform);
        } else {
            candidate = resizedGroupTransforms(
                originalTransforms_,
                originalGroupBounds_,
                activeHandle_,
                canvasPosition,
                event->modifiers().testFlag(Qt::ShiftModifier),
                currentSources());
        }

        if (applyCallback_ && !candidate.isEmpty() && applyCallback_(candidate)) {
            currentTransforms_ = candidate;
            changed_ = !transformMapsEqual(originalTransforms_, currentTransforms_);
        }

        update();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        lastPointerPosition_ = event->position();
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        if (marqueeSelecting_) {
            releasePointer();
            marqueeSelecting_ = false;
            marqueeBaseSelection_.clear();
            updateCursorForPosition(event->position());
            update();
            if (QWidget* owner = parentWidget()) {
                owner->activateWindow();
            }
            event->accept();
            return;
        }

        if (!dragging_) {
            if (QWidget* owner = parentWidget()) {
                owner->activateWindow();
            }
            QWidget::mouseReleaseEvent(event);
            return;
        }

        const TransformMap original = originalTransforms_;
        const TransformMap current = currentTransforms_;
        const bool changed = changed_;
        finishInteraction();

        if (changed && commitCallback_) {
            commitCallback_(original, current);
        }
        updateCursorForPosition(event->position());
        update();
        if (QWidget* owner = parentWidget()) {
            owner->activateWindow();
        }
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }
        const auto hit = topSourceAt(event->position(), currentSources());
        if (hit.has_value()) {
            emitSelection({hit->name});
            if (propertiesCallback_) {
                propertiesCallback_(hit->name);
            }
        }
        event->accept();
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        const auto hit = topSourceAt(event->pos(), currentSources());
        const QString sourceName = hit.has_value() ? hit->name : QString{};
        QStringList selection = currentSelection();
        if (hit.has_value() && !selection.contains(hit->name)) {
            selection = {hit->name};
            emitSelection(selection);
        } else if (!hit.has_value()) {
            emitSelection({});
        }
        if (contextCallback_) {
            contextCallback_(sourceName, event->globalPos());
        }
        event->accept();
    }

    void leaveEvent(QEvent* event) override
    {
        if (!dragging_ && !marqueeSelecting_) {
            clearInteractionCursor();
            update();
        }
        QWidget::leaveEvent(event);
    }

private:
    struct HandlePoint
    {
        Handle handle = Handle::None;
        QPointF position;
    };

    struct Guide
    {
        bool vertical = true;
        double coordinate = 0.0;
    };

    QVector<Vuttara::SourceInfo> currentSources() const
    {
        return sourcesProvider_ ? sourcesProvider_() : QVector<Vuttara::SourceInfo>{};
    }

    QStringList currentSelection() const
    {
        return selectionProvider_ ? selectionProvider_() : QStringList{};
    }

    void emitSelection(const QStringList& names)
    {
        if (selectionCallback_) {
            selectionCallback_(names);
        }
    }

    bool editingAllowed() const
    {
        return !editAllowedProvider_ || editAllowedProvider_();
    }

    void postStatus(const QString& message) const
    {
        if (statusCallback_) {
            statusCallback_(message);
        }
    }

    QRectF canvasRect() const
    {
        if (width() <= 0 || height() <= 0) {
            return {};
        }
        const double scale = std::min(
            static_cast<double>(width()) / PreviewCanvasWidth,
            static_cast<double>(height()) / PreviewCanvasHeight);
        const QSizeF size(PreviewCanvasWidth * scale, PreviewCanvasHeight * scale);
        return QRectF(
            (static_cast<double>(width()) - size.width()) / 2.0,
            (static_cast<double>(height()) - size.height()) / 2.0,
            size.width(),
            size.height());
    }

    QPointF canvasToWidget(const QPointF& point) const
    {
        const QRectF viewport = canvasRect();
        if (viewport.isEmpty()) {
            return {};
        }
        const double scale = viewport.width() / PreviewCanvasWidth;
        return QPointF(
            viewport.left() + point.x() * scale,
            viewport.top() + point.y() * scale);
    }

    QPointF widgetToCanvas(const QPointF& point, bool* inside) const
    {
        const QRectF viewport = canvasRect();
        const bool isInside = !viewport.isEmpty() && viewport.contains(point);
        if (inside != nullptr) {
            *inside = isInside;
        }
        if (viewport.isEmpty()) {
            return {};
        }
        const double scale = PreviewCanvasWidth / viewport.width();
        return QPointF(
            (point.x() - viewport.left()) * scale,
            (point.y() - viewport.top()) * scale);
    }

    static QPointF rotateVector(const QPointF& vector, double degrees)
    {
        const double radians = qDegreesToRadians(degrees);
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        return QPointF(
            vector.x() * cosine - vector.y() * sine,
            vector.x() * sine + vector.y() * cosine);
    }

    static QVector<QPointF> canvasCorners(const Vuttara::SourceTransform& transform)
    {
        const QPointF center(transform.x, transform.y);
        const double halfWidth = transform.width / 2.0;
        const double halfHeight = transform.height / 2.0;
        const QVector<QPointF> local{
            {-halfWidth, -halfHeight},
            {halfWidth, -halfHeight},
            {halfWidth, halfHeight},
            {-halfWidth, halfHeight},
        };
        QVector<QPointF> result;
        result.reserve(4);
        for (const QPointF& point : local) {
            result.append(center + rotateVector(point, transform.rotation));
        }
        return result;
    }

    QPolygonF sourcePolygon(const Vuttara::SourceTransform& transform) const
    {
        QPolygonF polygon;
        for (const QPointF& point : canvasCorners(transform)) {
            polygon.append(canvasToWidget(point));
        }
        return polygon;
    }

    static QRectF transformBounds(const Vuttara::SourceTransform& transform)
    {
        const QVector<QPointF> corners = canvasCorners(transform);
        if (corners.isEmpty()) {
            return {};
        }
        double minimumX = corners.first().x();
        double maximumX = minimumX;
        double minimumY = corners.first().y();
        double maximumY = minimumY;
        for (const QPointF& point : corners) {
            minimumX = std::min(minimumX, point.x());
            maximumX = std::max(maximumX, point.x());
            minimumY = std::min(minimumY, point.y());
            maximumY = std::max(maximumY, point.y());
        }
        return QRectF(
            QPointF(minimumX, minimumY),
            QPointF(maximumX, maximumY)).normalized();
    }

    static QRectF transformMapBounds(const TransformMap& transforms)
    {
        QRectF result;
        bool first = true;
        for (auto iterator = transforms.cbegin(); iterator != transforms.cend(); ++iterator) {
            const QRectF bounds = transformBounds(iterator.value());
            result = first ? bounds : result.united(bounds);
            first = false;
        }
        return result;
    }

    QVector<HandlePoint> singleHandlePoints(const Vuttara::SourceTransform& transform) const
    {
        const QPolygonF polygon = sourcePolygon(transform);
        if (polygon.size() != 4) {
            return {};
        }
        const QPointF topLeft = polygon.at(0);
        const QPointF topRight = polygon.at(1);
        const QPointF bottomRight = polygon.at(2);
        const QPointF bottomLeft = polygon.at(3);
        return {
            {Handle::TopLeft, topLeft},
            {Handle::Top, (topLeft + topRight) / 2.0},
            {Handle::TopRight, topRight},
            {Handle::Right, (topRight + bottomRight) / 2.0},
            {Handle::BottomRight, bottomRight},
            {Handle::Bottom, (bottomRight + bottomLeft) / 2.0},
            {Handle::BottomLeft, bottomLeft},
            {Handle::Left, (bottomLeft + topLeft) / 2.0},
        };
    }

    QVector<HandlePoint> groupHandlePoints(const QRectF& canvasBounds) const
    {
        const QRectF widgetBounds(canvasToWidget(canvasBounds.topLeft()), canvasToWidget(canvasBounds.bottomRight()));
        return {
            {Handle::TopLeft, widgetBounds.topLeft()},
            {Handle::Top, QPointF(widgetBounds.center().x(), widgetBounds.top())},
            {Handle::TopRight, widgetBounds.topRight()},
            {Handle::Right, QPointF(widgetBounds.right(), widgetBounds.center().y())},
            {Handle::BottomRight, widgetBounds.bottomRight()},
            {Handle::Bottom, QPointF(widgetBounds.center().x(), widgetBounds.bottom())},
            {Handle::BottomLeft, widgetBounds.bottomLeft()},
            {Handle::Left, QPointF(widgetBounds.left(), widgetBounds.center().y())},
        };
    }

    static std::optional<Vuttara::SourceInfo> findSource(
        const QVector<Vuttara::SourceInfo>& sources,
        const QString& name)
    {
        const auto iterator = std::find_if(
            sources.cbegin(),
            sources.cend(),
            [&name](const Vuttara::SourceInfo& source) { return source.name == name; });
        return iterator == sources.cend()
            ? std::nullopt
            : std::optional<Vuttara::SourceInfo>(*iterator);
    }

    static QStringList normalizedSelection(
        const QStringList& names,
        const QVector<Vuttara::SourceInfo>& sources)
    {
        QStringList result;
        for (const QString& name : names) {
            if (!name.isEmpty() && findSource(sources, name).has_value() && !result.contains(name)) {
                result.append(name);
            }
        }
        return result;
    }

    std::optional<Vuttara::SourceInfo> topSourceAt(
        const QPointF& widgetPosition,
        const QVector<Vuttara::SourceInfo>& sources) const
    {
        bool insideCanvas = false;
        const QPointF canvasPosition = widgetToCanvas(widgetPosition, &insideCanvas);
        if (!insideCanvas) {
            return std::nullopt;
        }
        for (const Vuttara::SourceInfo& source : sources) {
            if (!source.visible || source.transform.width <= 0.0 || source.transform.height <= 0.0) {
                continue;
            }
            const QPointF relative = canvasPosition - QPointF(source.transform.x, source.transform.y);
            const QPointF local = rotateVector(relative, -source.transform.rotation);
            if (
                std::abs(local.x()) <= source.transform.width / 2.0 &&
                std::abs(local.y()) <= source.transform.height / 2.0) {
                return source;
            }
        }
        return std::nullopt;
    }

    Handle handleAt(
        const QPointF& widgetPosition,
        const QVector<Vuttara::SourceInfo>& sources,
        const QStringList& selectedNames) const
    {
        if (selectedNames.isEmpty()) {
            return Handle::None;
        }
        QVector<Vuttara::SourceInfo> selected;
        TransformMap transforms;
        for (const QString& name : selectedNames) {
            const auto source = findSource(sources, name);
            if (source.has_value() && source->visible) {
                selected.append(*source);
                transforms.insert(name, source->transform);
            }
        }
        if (selected.isEmpty()) {
            return Handle::None;
        }

        const QVector<HandlePoint> handles = selected.size() == 1
            ? singleHandlePoints(selected.first().transform)
            : groupHandlePoints(transformMapBounds(transforms));
        const bool locked = std::any_of(
            selected.cbegin(),
            selected.cend(),
            [](const Vuttara::SourceInfo& source) { return source.locked; });
        if (!locked && editingAllowed()) {
            for (const HandlePoint& point : handles) {
                if (QLineF(widgetPosition, point.position).length() <= PreviewHandleHitRadius) {
                    return point.handle;
                }
            }
        }

        for (const Vuttara::SourceInfo& source : selected) {
            if (sourcePolygon(source.transform).containsPoint(widgetPosition, Qt::OddEvenFill)) {
                return Handle::Move;
            }
        }
        return Handle::None;
    }

    static bool isCornerHandle(Handle handle)
    {
        return handle == Handle::TopLeft ||
            handle == Handle::TopRight ||
            handle == Handle::BottomRight ||
            handle == Handle::BottomLeft;
    }

    static bool affectsLeft(Handle handle)
    {
        return handle == Handle::Left || handle == Handle::TopLeft || handle == Handle::BottomLeft;
    }

    static bool affectsRight(Handle handle)
    {
        return handle == Handle::Right || handle == Handle::TopRight || handle == Handle::BottomRight;
    }

    static bool affectsTop(Handle handle)
    {
        return handle == Handle::Top || handle == Handle::TopLeft || handle == Handle::TopRight;
    }

    static bool affectsBottom(Handle handle)
    {
        return handle == Handle::Bottom || handle == Handle::BottomLeft || handle == Handle::BottomRight;
    }

    static Vuttara::SourceTransform resizedTransform(
        const Vuttara::SourceTransform& original,
        Handle handle,
        const QPointF& canvasPosition,
        bool freeResize)
    {
        const QPointF originalCenter(original.x, original.y);
        const QPointF localPointer = rotateVector(canvasPosition - originalCenter, -original.rotation);
        double left = -original.width / 2.0;
        double right = original.width / 2.0;
        double top = -original.height / 2.0;
        double bottom = original.height / 2.0;

        if (affectsLeft(handle)) {
            left = std::min(localPointer.x(), right - PreviewMinimumSourceSize);
        }
        if (affectsRight(handle)) {
            right = std::max(localPointer.x(), left + PreviewMinimumSourceSize);
        }
        if (affectsTop(handle)) {
            top = std::min(localPointer.y(), bottom - PreviewMinimumSourceSize);
        }
        if (affectsBottom(handle)) {
            bottom = std::max(localPointer.y(), top + PreviewMinimumSourceSize);
        }
        if (handle == Handle::None || handle == Handle::Move) {
            return original;
        }

        if (isCornerHandle(handle) && !freeResize) {
            const double ratio = std::max(PreviewMinimumSourceSize, original.width) /
                std::max(PreviewMinimumSourceSize, original.height);
            double newWidth = right - left;
            double newHeight = bottom - top;
            if (std::abs(newWidth - original.width) / std::max(1.0, original.width) >=
                std::abs(newHeight - original.height) / std::max(1.0, original.height)) {
                newHeight = std::max(PreviewMinimumSourceSize, newWidth / ratio);
            } else {
                newWidth = std::max(PreviewMinimumSourceSize, newHeight * ratio);
            }
            if (affectsLeft(handle)) {
                left = right - newWidth;
            } else {
                right = left + newWidth;
            }
            if (affectsTop(handle)) {
                top = bottom - newHeight;
            } else {
                bottom = top + newHeight;
            }
        }

        const QPointF localCenter((left + right) / 2.0, (top + bottom) / 2.0);
        const QPointF canvasCenter = originalCenter + rotateVector(localCenter, original.rotation);
        Vuttara::SourceTransform result = original;
        result.x = canvasCenter.x();
        result.y = canvasCenter.y();
        result.width = std::max(PreviewMinimumSourceSize, right - left);
        result.height = std::max(PreviewMinimumSourceSize, bottom - top);
        return result;
    }

    static Vuttara::SourceTransform croppedTransform(
        const Vuttara::SourceInfo& source,
        Handle handle,
        const QPointF& canvasPosition)
    {
        Vuttara::SourceTransform result = source.transform;
        if (handle == Handle::None || handle == Handle::Move) {
            return result;
        }
        const QPointF center(result.x, result.y);
        const QPointF localPointer = rotateVector(canvasPosition - center, -result.rotation);
        const double oldWidth = std::max(1.0, result.width);
        const double oldHeight = std::max(1.0, result.height);
        const double visiblePixelsX = std::max(
            1.0,
            static_cast<double>(source.sourceWidth) - result.cropLeft - result.cropRight);
        const double visiblePixelsY = std::max(
            1.0,
            static_cast<double>(source.sourceHeight) - result.cropTop - result.cropBottom);
        double left = -oldWidth / 2.0;
        double right = oldWidth / 2.0;
        double top = -oldHeight / 2.0;
        double bottom = oldHeight / 2.0;

        if (affectsLeft(handle)) {
            const double desiredDelta = localPointer.x() - left;
            const double pixelDelta = desiredDelta * visiblePixelsX / oldWidth;
            const double newCrop = std::clamp(
                result.cropLeft + pixelDelta,
                0.0,
                std::max(0.0, static_cast<double>(source.sourceWidth - 1) - result.cropRight));
            const double actualDelta = (newCrop - result.cropLeft) * oldWidth / visiblePixelsX;
            result.cropLeft = newCrop;
            left += actualDelta;
        }
        if (affectsRight(handle)) {
            const double desiredDelta = right - localPointer.x();
            const double pixelDelta = desiredDelta * visiblePixelsX / oldWidth;
            const double newCrop = std::clamp(
                result.cropRight + pixelDelta,
                0.0,
                std::max(0.0, static_cast<double>(source.sourceWidth - 1) - result.cropLeft));
            const double actualDelta = (newCrop - result.cropRight) * oldWidth / visiblePixelsX;
            result.cropRight = newCrop;
            right -= actualDelta;
        }
        if (affectsTop(handle)) {
            const double desiredDelta = localPointer.y() - top;
            const double pixelDelta = desiredDelta * visiblePixelsY / oldHeight;
            const double newCrop = std::clamp(
                result.cropTop + pixelDelta,
                0.0,
                std::max(0.0, static_cast<double>(source.sourceHeight - 1) - result.cropBottom));
            const double actualDelta = (newCrop - result.cropTop) * oldHeight / visiblePixelsY;
            result.cropTop = newCrop;
            top += actualDelta;
        }
        if (affectsBottom(handle)) {
            const double desiredDelta = bottom - localPointer.y();
            const double pixelDelta = desiredDelta * visiblePixelsY / oldHeight;
            const double newCrop = std::clamp(
                result.cropBottom + pixelDelta,
                0.0,
                std::max(0.0, static_cast<double>(source.sourceHeight - 1) - result.cropTop));
            const double actualDelta = (newCrop - result.cropBottom) * oldHeight / visiblePixelsY;
            result.cropBottom = newCrop;
            bottom -= actualDelta;
        }

        const QPointF localCenter((left + right) / 2.0, (top + bottom) / 2.0);
        const QPointF newCenter = center + rotateVector(localCenter, result.rotation);
        result.x = newCenter.x();
        result.y = newCenter.y();
        result.width = std::max(1.0, right - left);
        result.height = std::max(1.0, bottom - top);
        return result;
    }

    TransformMap resizedGroupTransforms(
        const TransformMap& originals,
        const QRectF& originalBounds,
        Handle handle,
        const QPointF& canvasPosition,
        bool freeResize,
        const QVector<Vuttara::SourceInfo>& sources)
    {
        QRectF target = originalBounds;
        if (affectsLeft(handle)) {
            target.setLeft(std::min(canvasPosition.x(), target.right() - PreviewMinimumSourceSize));
        }
        if (affectsRight(handle)) {
            target.setRight(std::max(canvasPosition.x(), target.left() + PreviewMinimumSourceSize));
        }
        if (affectsTop(handle)) {
            target.setTop(std::min(canvasPosition.y(), target.bottom() - PreviewMinimumSourceSize));
        }
        if (affectsBottom(handle)) {
            target.setBottom(std::max(canvasPosition.y(), target.top() + PreviewMinimumSourceSize));
        }
        if (isCornerHandle(handle) && !freeResize) {
            const double ratio = originalBounds.width() / std::max(1.0, originalBounds.height());
            double width = target.width();
            double height = target.height();
            if (std::abs(width - originalBounds.width()) / std::max(1.0, originalBounds.width()) >=
                std::abs(height - originalBounds.height()) / std::max(1.0, originalBounds.height())) {
                height = std::max(PreviewMinimumSourceSize, width / ratio);
            } else {
                width = std::max(PreviewMinimumSourceSize, height * ratio);
            }
            if (affectsLeft(handle)) {
                target.setLeft(target.right() - width);
            } else {
                target.setRight(target.left() + width);
            }
            if (affectsTop(handle)) {
                target.setTop(target.bottom() - height);
            } else {
                target.setBottom(target.top() + height);
            }
        }

        const QPointF snap = snappingTranslationForResize(
            target,
            handle,
            sources,
            originals.keys());
        target.translate(snap);
        const double scaleX = target.width() / std::max(1.0, originalBounds.width());
        const double scaleY = target.height() / std::max(1.0, originalBounds.height());
        TransformMap result;
        for (auto iterator = originals.cbegin(); iterator != originals.cend(); ++iterator) {
            const Vuttara::SourceTransform& original = iterator.value();
            const double relativeX = (original.x - originalBounds.left()) /
                std::max(1.0, originalBounds.width());
            const double relativeY = (original.y - originalBounds.top()) /
                std::max(1.0, originalBounds.height());
            Vuttara::SourceTransform transformed = original;
            transformed.x = target.left() + relativeX * target.width();
            transformed.y = target.top() + relativeY * target.height();
            transformed.width = std::max(1.0, original.width * std::abs(scaleX));
            transformed.height = std::max(1.0, original.height * std::abs(scaleY));
            result.insert(iterator.key(), transformed);
        }
        return result;
    }

    QVector<double> horizontalTargets(
        const QVector<Vuttara::SourceInfo>& sources,
        const QStringList& excluded) const
    {
        QVector<double> targets{0.0, PreviewCanvasWidth / 2.0, PreviewCanvasWidth};
        for (const Vuttara::SourceInfo& source : sources) {
            if (!source.visible || excluded.contains(source.name)) {
                continue;
            }
            const QRectF bounds = transformBounds(source.transform);
            targets << bounds.left() << bounds.center().x() << bounds.right();
        }
        return targets;
    }

    QVector<double> verticalTargets(
        const QVector<Vuttara::SourceInfo>& sources,
        const QStringList& excluded) const
    {
        QVector<double> targets{0.0, PreviewCanvasHeight / 2.0, PreviewCanvasHeight};
        for (const Vuttara::SourceInfo& source : sources) {
            if (!source.visible || excluded.contains(source.name)) {
                continue;
            }
            const QRectF bounds = transformBounds(source.transform);
            targets << bounds.top() << bounds.center().y() << bounds.bottom();
        }
        return targets;
    }

    double bestSnap(
        const QVector<double>& moving,
        const QVector<double>& targets,
        bool verticalGuide)
    {
        constexpr double threshold = 8.0;
        double best = 0.0;
        double bestDistance = threshold + 1.0;
        double guideCoordinate = 0.0;
        for (double movingCoordinate : moving) {
            for (double target : targets) {
                const double delta = target - movingCoordinate;
                if (std::abs(delta) < bestDistance && std::abs(delta) <= threshold) {
                    best = delta;
                    bestDistance = std::abs(delta);
                    guideCoordinate = target;
                }
            }
        }
        if (bestDistance <= threshold) {
            alignmentGuides_.append(Guide{verticalGuide, guideCoordinate});
        }
        return best;
    }

    QPointF snappingTranslation(
        const QRectF& bounds,
        const QVector<Vuttara::SourceInfo>& sources,
        const QStringList& excluded)
    {
        const double x = bestSnap(
            {bounds.left(), bounds.center().x(), bounds.right()},
            horizontalTargets(sources, excluded),
            true);
        const double y = bestSnap(
            {bounds.top(), bounds.center().y(), bounds.bottom()},
            verticalTargets(sources, excluded),
            false);
        return QPointF(x, y);
    }

    QPointF snappingTranslationForResize(
        const QRectF& bounds,
        Handle handle,
        const QVector<Vuttara::SourceInfo>& sources,
        const QStringList& excluded)
    {
        QVector<double> movingX;
        QVector<double> movingY;
        if (affectsLeft(handle)) {
            movingX.append(bounds.left());
        }
        if (affectsRight(handle)) {
            movingX.append(bounds.right());
        }
        if (affectsTop(handle)) {
            movingY.append(bounds.top());
        }
        if (affectsBottom(handle)) {
            movingY.append(bounds.bottom());
        }
        const double x = movingX.isEmpty()
            ? 0.0
            : bestSnap(movingX, horizontalTargets(sources, excluded), true);
        const double y = movingY.isEmpty()
            ? 0.0
            : bestSnap(movingY, verticalTargets(sources, excluded), false);
        return QPointF(x, y);
    }

    static bool transformsEqual(
        const Vuttara::SourceTransform& left,
        const Vuttara::SourceTransform& right)
    {
        constexpr double tolerance = 0.05;
        return std::abs(left.x - right.x) < tolerance &&
            std::abs(left.y - right.y) < tolerance &&
            std::abs(left.width - right.width) < tolerance &&
            std::abs(left.height - right.height) < tolerance &&
            std::abs(left.rotation - right.rotation) < tolerance &&
            std::abs(left.cropLeft - right.cropLeft) < tolerance &&
            std::abs(left.cropTop - right.cropTop) < tolerance &&
            std::abs(left.cropRight - right.cropRight) < tolerance &&
            std::abs(left.cropBottom - right.cropBottom) < tolerance &&
            left.flipHorizontal == right.flipHorizontal &&
            left.flipVertical == right.flipVertical &&
            left.stretchToBounds == right.stretchToBounds;
    }

    static bool transformMapsEqual(const TransformMap& left, const TransformMap& right)
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (auto iterator = left.cbegin(); iterator != left.cend(); ++iterator) {
            const auto other = right.constFind(iterator.key());
            if (other == right.cend() || !transformsEqual(iterator.value(), other.value())) {
                return false;
            }
        }
        return true;
    }

    void drawSelectedSources(
        QPainter& painter,
        const QVector<Vuttara::SourceInfo>& sources,
        const QStringList& selectedNames)
    {
        TransformMap visibleTransforms;
        for (int index = 0; index < selectedNames.size(); ++index) {
            const auto source = findSource(sources, selectedNames.at(index));
            if (!source.has_value()) {
                continue;
            }
            const bool primary = index == selectedNames.size() - 1;
            const bool cropped =
                source->transform.cropLeft > 0.5 || source->transform.cropTop > 0.5 ||
                source->transform.cropRight > 0.5 || source->transform.cropBottom > 0.5;
            QColor accent = primary ? QColor(127, 92, 255) : QColor(86, 198, 255);
            if (!source->visible) {
                accent = QColor(150, 150, 160);
            } else if (source->locked) {
                accent = QColor(246, 184, 79);
            } else if (cropped) {
                accent = QColor(55, 211, 179);
            }
            QPen pen(accent, primary ? 2.4 : 1.7, source->visible ? Qt::SolidLine : Qt::DashLine);
            painter.setPen(pen);
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), source->visible ? 20 : 8));
            const QPolygonF polygon = sourcePolygon(source->transform);
            painter.drawPolygon(polygon);
            if (source->visible) {
                visibleTransforms.insert(source->name, source->transform);
            }

            QStringList states;
            if (!source->visible) states << QStringLiteral("Hidden");
            if (source->locked) states << QStringLiteral("Locked");
            if (cropped) states << QStringLiteral("Cropped");
            if (selectedNames.size() > 1) states << QStringLiteral("Multi");
            const QString label = states.isEmpty()
                ? source->name
                : QStringLiteral("%1  •  %2").arg(source->name, states.join(QStringLiteral(" • ")));
            const QRectF bounds = polygon.boundingRect();
            const QFontMetrics metrics(painter.font());
            const QSize textSize = metrics.size(Qt::TextSingleLine, label);
            QRectF labelRect(
                bounds.left(),
                std::max(4.0, bounds.top() - textSize.height() - 10.0),
                textSize.width() + 16.0,
                textSize.height() + 8.0);
            if (labelRect.right() > width() - 4.0) labelRect.moveRight(width() - 4.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(18, 16, 24, 225));
            painter.drawRoundedRect(labelRect, 5.0, 5.0);
            painter.setPen(QColor(245, 242, 250));
            painter.drawText(labelRect, Qt::AlignCenter, label);
        }

        if (visibleTransforms.isEmpty()) {
            return;
        }
        bool locked = false;
        for (const QString& name : visibleTransforms.keys()) {
            const auto source = findSource(sources, name);
            locked = locked || (source.has_value() && source->locked);
        }
        if (locked || !editingAllowed()) {
            return;
        }

        QVector<HandlePoint> handles;
        if (visibleTransforms.size() == 1) {
            handles = singleHandlePoints(visibleTransforms.cbegin().value());
        } else {
            const QRectF bounds = transformMapBounds(visibleTransforms);
            painter.setPen(QPen(QColor(86, 198, 255), 1.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(canvasToWidget(bounds.topLeft()), canvasToWidget(bounds.bottomRight())));
            handles = groupHandlePoints(bounds);
        }
        painter.setPen(QPen(QColor(20, 18, 27), 1.0));
        painter.setBrush(visibleTransforms.size() == 1 ? QColor(127, 92, 255) : QColor(86, 198, 255));
        for (const HandlePoint& point : handles) {
            painter.drawRoundedRect(
                QRectF(point.position.x() - 4.5, point.position.y() - 4.5, 9.0, 9.0),
                2.0,
                2.0);
        }
    }

    void drawAlignmentGuides(QPainter& painter)
    {
        const QRectF viewport = canvasRect();
        painter.setPen(QPen(QColor(55, 211, 179, 230), 1.4, Qt::DashLine));
        for (const Guide& guide : alignmentGuides_) {
            if (guide.vertical) {
                const double x = canvasToWidget(QPointF(guide.coordinate, 0.0)).x();
                painter.drawLine(QPointF(x, viewport.top()), QPointF(x, viewport.bottom()));
            } else {
                const double y = canvasToWidget(QPointF(0.0, guide.coordinate)).y();
                painter.drawLine(QPointF(viewport.left(), y), QPointF(viewport.right(), y));
            }
        }
    }

    void drawHint(
        QPainter& painter,
        const QVector<Vuttara::SourceInfo>& sources,
        const QStringList& selectedNames)
    {
        QString hint;
        if (!editingAllowed()) {
            hint = QStringLiteral("Preview editing is temporarily unavailable");
        } else if (selectedNames.isEmpty()) {
            hint = QStringLiteral("Click or drag a marquee • Ctrl-click selects multiple sources");
        } else {
            bool locked = false;
            for (const QString& name : selectedNames) {
                const auto source = findSource(sources, name);
                locked = locked || (source.has_value() && source->locked);
            }
            hint = locked
                ? QStringLiteral("A selected source is locked • Right-click to unlock")
                : QStringLiteral("Drag to move • Handles resize • Alt+handle crops • Shift frees corner ratio");
        }
        const QFontMetrics metrics(painter.font());
        const QSize textSize = metrics.size(Qt::TextSingleLine, hint);
        QRectF hintRect(
            10.0,
            std::max(10.0, static_cast<double>(height()) - textSize.height() - 24.0),
            std::min(static_cast<double>(width()) - 20.0, textSize.width() + 22.0),
            textSize.height() + 12.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 16, 24, 205));
        painter.drawRoundedRect(hintRect, 6.0, 6.0);
        painter.setPen(QColor(221, 214, 231));
        painter.drawText(
            hintRect.adjusted(10.0, 0.0, -10.0, 0.0),
            Qt::AlignVCenter | Qt::AlignLeft,
            hint);
    }

    void updateCursorForPosition(const QPointF& position)
    {
        const QVector<Vuttara::SourceInfo> sources = currentSources();
        const Handle handle = handleAt(position, sources, currentSelection());
        if (handle != Handle::None) {
            updateCursorForHandle(handle);
            return;
        }
        applyInteractionCursor(topSourceAt(position, sources).has_value()
                                   ? Qt::PointingHandCursor
                                   : Qt::ArrowCursor);
    }

    void updateCursorForHandle(Handle handle)
    {
        switch (handle) {
        case Handle::TopLeft:
        case Handle::BottomRight:
            applyInteractionCursor(Qt::SizeFDiagCursor);
            break;
        case Handle::TopRight:
        case Handle::BottomLeft:
            applyInteractionCursor(Qt::SizeBDiagCursor);
            break;
        case Handle::Top:
        case Handle::Bottom:
            applyInteractionCursor(Qt::SizeVerCursor);
            break;
        case Handle::Left:
        case Handle::Right:
            applyInteractionCursor(Qt::SizeHorCursor);
            break;
        case Handle::Move:
            applyInteractionCursor(Qt::SizeAllCursor);
            break;
        case Handle::None:
            applyInteractionCursor(Qt::ArrowCursor);
            break;
        }
    }

    void applyInteractionCursor(Qt::CursorShape shape)
    {
        setCursor(shape);
        if (inputProxy_ != nullptr) {
            inputProxy_->setCursor(shape);
        }
    }

    void clearInteractionCursor()
    {
        unsetCursor();
        if (inputProxy_ != nullptr) {
            inputProxy_->unsetCursor();
        }
    }

    void finishInteraction()
    {
        if (dragging_ || marqueeSelecting_) {
            releasePointer();
        }
        dragging_ = false;
        marqueeSelecting_ = false;
        changed_ = false;
        activeHandle_ = Handle::None;
        activeSourceName_.clear();
        originalTransforms_.clear();
        currentTransforms_.clear();
        alignmentGuides_.clear();
        activeSourceInfo_.reset();
    }

    void capturePointer()
    {
        if (inputProxy_ != nullptr) {
            inputProxy_->grabMouse();
        }
    }

    void releasePointer()
    {
        if (inputProxy_ != nullptr && QWidget::mouseGrabber() == inputProxy_) {
            inputProxy_->releaseMouse();
        }
    }

    QWidget* inputProxy_ = nullptr;
    SourcesProvider sourcesProvider_;
    SelectionProvider selectionProvider_;
    SelectionCallback selectionCallback_;
    ApplyCallback applyCallback_;
    CommitCallback commitCallback_;
    CancelCallback cancelCallback_;
    SourceCallback propertiesCallback_;
    ContextCallback contextCallback_;
    BooleanProvider editAllowedProvider_;
    StatusCallback statusCallback_;
    QPointF lastPointerPosition_;
    QPointF pressCanvasPosition_;
    QPointF marqueeStart_;
    QPointF marqueeCurrent_;
    QStringList marqueeBaseSelection_;
    QString activeSourceName_;
    TransformMap originalTransforms_;
    TransformMap currentTransforms_;
    QRectF originalGroupBounds_;
    std::optional<Vuttara::SourceInfo> activeSourceInfo_;
    QVector<Guide> alignmentGuides_;
    Handle activeHandle_ = Handle::None;
    bool dragging_ = false;
    bool marqueeSelecting_ = false;
    bool changed_ = false;
};



}

MainWindow::MainWindow(Vuttara::VuttaraEngine* engine, QWidget* parent)
    : QMainWindow(parent)
    , engine_(engine)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Vuttara Studio"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/vuttara-studio-256.png")));
    resize(1500, 940);
    setMinimumSize(1120, 740);

    // Stability mode: the central libobs/D3D11 preview is expensive to resize.
    // Use snap placement rather than animating the entire preview during a
    // drag, and keep one simple row/column per edge. Tab groups and grouped
    // tab dragging remain available.
    setAnimated(false);
    setDockNestingEnabled(false);
    setDockOptions(
        QMainWindow::AllowTabbedDocks |
        QMainWindow::GroupedDragging);
    setDocumentMode(false);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::South);
    setTabShape(QTabWidget::Rounded);

    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    createPreview();
    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }
    createSceneDock();
    createSourceDock();
    createAudioDock();
    createMenus();
    updateManager_ = new UpdateManager(engine_, this);
    connect(
        updateManager_,
        &UpdateManager::statusMessage,
        this,
        [this](const QString& message, int timeoutMilliseconds) {
            statusBar()->showMessage(message, timeoutMilliseconds);
        });
    refreshHotkeys();
    applyDefaultDockLayout(false);

    // A fresh layout uses the explicit default below. Once the user moves or
    // floats or tabifies a dock, the V10 stable QMainWindow state restores that choice.
    QSettings windowSettings;
    const auto savedGeometry =
        windowSettings.value(QStringLiteral("mainWindow/geometryV1")).toByteArray();
    if (!savedGeometry.isEmpty()) {
        restoreGeometry(savedGeometry);
    }

    const auto savedDockState =
        windowSettings.value(QStringLiteral("mainWindow/dockStateV10")).toByteArray();
    if (
        !savedDockState.isEmpty() &&
        !restoreState(savedDockState, 10)) {
        applyDefaultDockLayout(true);
    }

    if (engine_ != nullptr && engine_->isReady()) {
        if (!loadProjectState()) {
            migrateLegacyCaptureSettings();
            saveProjectState();
        }

        refreshSceneList();
        refreshSourceGroupTabs();
        refreshSourceList();
        refreshAudioDeviceLists();
        updateAudioControls();
        updateStreamingControls();
        updateRecordingControls();
#ifdef VUTTARA_STUDIO_RELEASE_BUILD
        const QString distributionStatus = QStringLiteral("Stable updates: Enabled");
#else
        const QString distributionStatus = QStringLiteral("Local development");
#endif
        statusBar()->showMessage(QStringLiteral(
            "Engine: Ready  |  Capture: Ready  |  Audio: Ready  |  Streaming: Ready  |  Recording: Ready  |  libobs: %1  |  %2")
                                     .arg(engine_->versionString())
                                     .arg(distributionStatus));
    } else {
        refreshSceneList();
        refreshSourceGroupTabs();
        refreshSourceList();
        statusBar()->showMessage(QStringLiteral("Engine initialization failed: %1")
                                     .arg(engine_ != nullptr ? engine_->lastError() : QStringLiteral("Engine unavailable")));
    }

    statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
    updatePreviewInformation();
    QTimer::singleShot(0, this, &MainWindow::applyNativeWindowChrome);
    updateManager_->startAutomaticChecks();
}

bool MainWindow::runStage9ASecureSettingsSelfTest(QStringList& lines)
{
    const QString secret = QStringLiteral("stage9a-self-test-secret-not-a-real-key");
    const QString protectedValue = protectSettingSecret(secret);
    if (protectedValue.isEmpty() || protectedValue.contains(secret) || !protectedValue.startsWith(QStringLiteral("dpapi:"))) {
        lines << QStringLiteral("FAIL: Windows DPAPI did not produce a protected streaming credential value.");
        return false;
    }
    if (unprotectSettingSecret(protectedValue) != secret) {
        lines << QStringLiteral("FAIL: Windows DPAPI streaming credential round-trip failed.");
        return false;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        lines << QStringLiteral("FAIL: Could not create the secure streaming-settings test directory.");
        return false;
    }
    const QString settingsPath = QDir(directory.path()).filePath(QStringLiteral("streaming-settings.ini"));
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("streaming/streamKeyProtectedV1"), protectedValue);
        settings.setValue(QStringLiteral("streaming/passwordProtectedV1"), protectedValue);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            lines << QStringLiteral("FAIL: Protected streaming credentials could not be persisted.");
            return false;
        }
    }
    QFile persisted(settingsPath);
    if (!persisted.open(QIODevice::ReadOnly)) {
        lines << QStringLiteral("FAIL: Protected streaming-settings test file could not be read.");
        return false;
    }
    const QByteArray storedBytes = persisted.readAll();
    if (storedBytes.contains(secret.toUtf8()) || !storedBytes.contains("dpapi:")) {
        lines << QStringLiteral("FAIL: Streaming credential persistence exposed plaintext or omitted the DPAPI value.");
        return false;
    }
    QSettings restored(settingsPath, QSettings::IniFormat);
    if (unprotectSettingSecret(restored.value(QStringLiteral("streaming/streamKeyProtectedV1")).toString()) != secret) {
        lines << QStringLiteral("FAIL: Protected streaming credential did not survive settings persistence.");
        return false;
    }
    lines << QStringLiteral("PASS: Stream key and password round-tripped through Windows DPAPI and persisted without plaintext exposure.");
    return true;
}

bool MainWindow::runStage8CFix3InteractionSelfTest(QStringList& lines)
{
    bool passed = true;

    MainWindow previewWindow(nullptr);
    previewWindow.resize(1100, 760);
    previewWindow.show();
    QApplication::processEvents();

    QStringList previewSelection;
    Vuttara::SourceInfo testSource;
    testSource.name = QStringLiteral("Stage 8C FIX3 Preview Selection Probe");
    testSource.type = QStringLiteral("color");
    testSource.visible = true;
    testSource.locked = false;
    testSource.orderPosition = 1;
    testSource.sourceWidth = 1920;
    testSource.sourceHeight = 1080;
    testSource.transform.x = 960.0;
    testSource.transform.y = 540.0;
    testSource.transform.width = 420.0;
    testSource.transform.height = 240.0;

    auto* overlay = static_cast<PreviewInteractionOverlay*>(
        previewWindow.previewInteractionOverlay_);
    overlay->configure(
        [testSource]() {
            return QVector<Vuttara::SourceInfo>{testSource};
        },
        [&previewSelection]() {
            return previewSelection;
        },
        [&previewSelection](const QStringList& names) {
            previewSelection = names;
        },
        [](const QHash<QString, Vuttara::SourceTransform>&) {
            return true;
        },
        [](const QHash<QString, Vuttara::SourceTransform>&,
           const QHash<QString, Vuttara::SourceTransform>&) {},
        [](const QHash<QString, Vuttara::SourceTransform>&) {},
        [](const QString&) {},
        [](const QString&, const QPoint&) {},
        []() { return true; },
        [](const QString&) {});
    previewWindow.syncPreviewInteractionOverlay();
    overlay->setGeometry(QRect(
        previewWindow.mapFromGlobal(previewWindow.previewWidget_->mapToGlobal(QPoint(0, 0))),
        previewWindow.previewWidget_->size()));
    QApplication::processEvents();

    const QPointF clickPosition(
        previewWindow.previewWidget_->width() / 2.0,
        previewWindow.previewWidget_->height() / 2.0);
#ifdef Q_OS_WIN
    const HWND previewHandle = reinterpret_cast<HWND>(previewWindow.previewWidget_->winId());
    const qreal previewPixelRatio = std::max<qreal>(
        1.0,
        previewWindow.previewWidget_->devicePixelRatioF());
    const auto nativeCoordinate = [previewPixelRatio](double coordinate) {
        return static_cast<int>(std::lround(coordinate * previewPixelRatio));
    };
    SendMessageW(
        previewHandle,
        WM_LBUTTONDOWN,
        MK_LBUTTON,
        MAKELPARAM(nativeCoordinate(clickPosition.x()), nativeCoordinate(clickPosition.y())));
    SendMessageW(
        previewHandle,
        WM_LBUTTONUP,
        0,
        MAKELPARAM(nativeCoordinate(clickPosition.x()), nativeCoordinate(clickPosition.y())));
#else
    QMouseEvent clickPress(
        QEvent::MouseButtonPress,
        clickPosition,
        previewWindow.previewWidget_->mapToGlobal(clickPosition.toPoint()),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    static_cast<PreviewWidget*>(previewWindow.previewWidget_)->dispatchInteractionEventForTest(&clickPress);
    QMouseEvent clickRelease(
        QEvent::MouseButtonRelease,
        clickPosition,
        previewWindow.previewWidget_->mapToGlobal(clickPosition.toPoint()),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    static_cast<PreviewWidget*>(previewWindow.previewWidget_)->dispatchInteractionEventForTest(&clickRelease);
#endif

    if (!previewSelection.contains(testSource.name)) {
        lines << QStringLiteral("FAIL: Stage 8C V1 FIX3 preview click did not traverse the native PreviewWidget HWND bridge.");
        passed = false;
    }

    previewSelection.clear();
    const double previewWidth = static_cast<double>(previewWindow.previewWidget_->width());
    const double previewHeight = static_cast<double>(previewWindow.previewWidget_->height());
    const double canvasScale = std::min(
        previewWidth / PreviewCanvasWidth,
        previewHeight / PreviewCanvasHeight);
    const QSizeF renderedCanvasSize(
        PreviewCanvasWidth * canvasScale,
        PreviewCanvasHeight * canvasScale);
    const QRectF renderedCanvas(
        (previewWidth - renderedCanvasSize.width()) / 2.0,
        (previewHeight - renderedCanvasSize.height()) / 2.0,
        renderedCanvasSize.width(),
        renderedCanvasSize.height());
    const double marqueeInset = std::min(
        24.0,
        std::max(8.0, std::min(renderedCanvas.width(), renderedCanvas.height()) / 12.0));
    const QPointF marqueeStart = renderedCanvas.topLeft() + QPointF(marqueeInset, marqueeInset);
    const QPointF marqueeMid = renderedCanvas.center();
    const QPointF marqueeEnd = renderedCanvas.bottomRight() - QPointF(marqueeInset, marqueeInset);
    const QRectF renderedSourceBounds(
        renderedCanvas.center().x() - testSource.transform.width * canvasScale / 2.0,
        renderedCanvas.center().y() - testSource.transform.height * canvasScale / 2.0,
        testSource.transform.width * canvasScale,
        testSource.transform.height * canvasScale);

    bool marqueeStarted = false;
    bool marqueeSelectedDuringMove = false;
    bool marqueeReleased = false;
    if (
        renderedCanvas.isEmpty() ||
        !renderedCanvas.contains(marqueeStart) ||
        !renderedCanvas.contains(marqueeEnd) ||
        renderedSourceBounds.contains(marqueeStart)) {
        lines << QStringLiteral("FAIL: Stage 8C V1 FIX3 marquee self-test could not construct an in-canvas empty-space drag path.");
        passed = false;
    } else {
#ifdef Q_OS_WIN
        SendMessageW(
            previewHandle,
            WM_LBUTTONDOWN,
            MK_LBUTTON,
            MAKELPARAM(nativeCoordinate(marqueeStart.x()), nativeCoordinate(marqueeStart.y())));
        marqueeStarted = overlay->ownsPointerInteraction();
        SendMessageW(
            previewHandle,
            WM_MOUSEMOVE,
            MK_LBUTTON,
            MAKELPARAM(nativeCoordinate(marqueeMid.x()), nativeCoordinate(marqueeMid.y())));
        marqueeSelectedDuringMove = previewSelection.contains(testSource.name);
        SendMessageW(
            previewHandle,
            WM_MOUSEMOVE,
            MK_LBUTTON,
            MAKELPARAM(nativeCoordinate(marqueeEnd.x()), nativeCoordinate(marqueeEnd.y())));
        marqueeSelectedDuringMove =
            marqueeSelectedDuringMove || previewSelection.contains(testSource.name);
        SendMessageW(
            previewHandle,
            WM_LBUTTONUP,
            0,
            MAKELPARAM(nativeCoordinate(marqueeEnd.x()), nativeCoordinate(marqueeEnd.y())));
        marqueeReleased = !overlay->ownsPointerInteraction();
#else
        QMouseEvent marqueePress(
            QEvent::MouseButtonPress,
            marqueeStart,
            previewWindow.previewWidget_->mapToGlobal(marqueeStart.toPoint()),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier);
        static_cast<PreviewWidget*>(previewWindow.previewWidget_)->dispatchInteractionEventForTest(&marqueePress);
        marqueeStarted = overlay->ownsPointerInteraction();
        QMouseEvent marqueeMove(
            QEvent::MouseMove,
            marqueeMid,
            previewWindow.previewWidget_->mapToGlobal(marqueeMid.toPoint()),
            Qt::NoButton,
            Qt::LeftButton,
            Qt::NoModifier);
        static_cast<PreviewWidget*>(previewWindow.previewWidget_)->dispatchInteractionEventForTest(&marqueeMove);
        marqueeSelectedDuringMove = previewSelection.contains(testSource.name);
        QMouseEvent marqueeEndMove(
            QEvent::MouseMove,
            marqueeEnd,
            previewWindow.previewWidget_->mapToGlobal(marqueeEnd.toPoint()),
            Qt::NoButton,
            Qt::LeftButton,
            Qt::NoModifier);
        static_cast<PreviewWidget*>(previewWindow.previewWidget_)->dispatchInteractionEventForTest(&marqueeEndMove);
        marqueeSelectedDuringMove =
            marqueeSelectedDuringMove || previewSelection.contains(testSource.name);
        QMouseEvent marqueeRelease(
            QEvent::MouseButtonRelease,
            marqueeEnd,
            previewWindow.previewWidget_->mapToGlobal(marqueeEnd.toPoint()),
            Qt::LeftButton,
            Qt::NoButton,
            Qt::NoModifier);
        static_cast<PreviewWidget*>(previewWindow.previewWidget_)->dispatchInteractionEventForTest(&marqueeRelease);
        marqueeReleased = !overlay->ownsPointerInteraction();
#endif
    }

    if (!marqueeStarted) {
        lines << QStringLiteral("FAIL: Stage 8C V1 FIX3 marquee press did not start from empty space inside the rendered canvas.");
        passed = false;
    }
    if (!marqueeSelectedDuringMove || !previewSelection.contains(testSource.name)) {
        lines << QStringLiteral("FAIL: Stage 8C V1 FIX3 marquee move did not select the source through the native PreviewWidget HWND bridge.");
        passed = false;
    }
    if (!marqueeReleased) {
        lines << QStringLiteral("FAIL: Stage 8C V1 FIX3 marquee release did not finish the native pointer interaction.");
        passed = false;
    }
    if (passed) {
        lines << QStringLiteral("PASS: Stage 8C V1 FIX3 preview click and marquee traversed the native PreviewWidget HWND bridge.");
    }
    previewWindow.hide();
    QApplication::processEvents();

    QWidget rowOwner;
    SourceDockRow row(
        QStringLiteral("Drag Probe"),
        QStringLiteral("color"),
        false,
        true,
        true,
        false,
        false,
        &rowOwner);
    row.resize(320, 31);
    rowOwner.resize(320, 31);
    rowOwner.show();
    row.show();
    bool rowActivated = false;
    bool dragStarted = false;
    row.setCallbacks(
        [&rowActivated](Qt::KeyboardModifiers) { rowActivated = true; },
        [&dragStarted]() { dragStarted = true; },
        {}, {}, {}, {}, {});
    const QPointF rowStart(10.0, 15.0);
    const QPointF rowEnd(
        10.0 + QApplication::startDragDistance() + 8.0,
        15.0);
    QMouseEvent rowPress(
        QEvent::MouseButtonPress,
        rowStart,
        row.mapToGlobal(rowStart.toPoint()),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&row, &rowPress);
    QMouseEvent rowMove(
        QEvent::MouseMove,
        rowEnd,
        row.mapToGlobal(rowEnd.toPoint()),
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&row, &rowMove);

    SourcesListWidget dropList;
    dropList.resize(360, 100);
    auto* groupItem = new QListWidgetItem(&dropList);
    groupItem->setData(GroupRowRole, true);
    groupItem->setData(GroupNameRole, QStringLiteral("Drop Probe Folder"));
    groupItem->setSizeHint(QSize(0, 33));
    dropList.show();
    QApplication::processEvents();
    bool dropExecuted = false;
    dropList.setDropCallback(
        [&dropExecuted, groupItem](const QByteArray& payload, QListWidgetItem* target, int position) {
            dropExecuted =
                !payload.isEmpty() &&
                target == groupItem &&
                position == SourceDropOn;
            return dropExecuted;
        });
    QMimeData mimeData;
    mimeData.setData(SourceDockMimeType, QByteArrayLiteral("{\"kind\":\"sources\"}"));
    const QPoint dropPoint = dropList.visualItemRect(groupItem).center();
    QDragEnterEvent dragEnter(
        dropPoint,
        Qt::MoveAction,
        &mimeData,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(dropList.viewport(), &dragEnter);
    QDragMoveEvent dragMove(
        dropPoint,
        Qt::MoveAction,
        &mimeData,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(dropList.viewport(), &dragMove);
    QDropEvent dropEvent(
        QPointF(dropPoint),
        Qt::MoveAction,
        &mimeData,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(dropList.viewport(), &dropEvent);

    if (!rowActivated || !dragStarted || !dragEnter.isAccepted() || !dropExecuted || !dropEvent.isAccepted()) {
        lines << QStringLiteral("FAIL: Stage 8C V1 FIX3 Sources row drag or folder drop routing did not execute.");
        passed = false;
    } else {
        lines << QStringLiteral("PASS: Stage 8C V1 FIX3 Sources row drag threshold and folder drop routing executed.");
    }

    rowOwner.hide();
    dropList.hide();
    QApplication::processEvents();
    return passed;
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    applyNativeWindowChrome();
}


bool MainWindow::isPreviewInteractionOverlayBlocker(QObject* watched) const
{
    auto* widget = qobject_cast<QWidget*>(watched);
    if (
        widget == nullptr ||
        !widget->isWindow() ||
        widget == this ||
        widget == previewInteractionOverlay_ ||
        qobject_cast<QDockWidget*>(widget) != nullptr) {
        return false;
    }

    const Qt::WindowType windowType = widget->windowType();
    if (windowType == Qt::ToolTip) {
        return false;
    }

    return
        qobject_cast<QDialog*>(widget) != nullptr ||
        qobject_cast<QMenu*>(widget) != nullptr ||
        windowType == Qt::Dialog ||
        windowType == Qt::Sheet ||
        windowType == Qt::Popup ||
        windowType == Qt::Tool ||
        windowType == Qt::Window;
}

bool MainWindow::shouldSuppressPreviewInteractionOverlay() const
{
    if (QApplication::applicationState() != Qt::ApplicationActive) {
        return true;
    }

    if (
        QApplication::activeModalWidget() != nullptr ||
        QApplication::activePopupWidget() != nullptr) {
        return true;
    }

    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (
            widget != nullptr &&
            widget->isVisible() &&
            isPreviewInteractionOverlayBlocker(widget)) {
            return true;
        }
    }

    auto* overlay = previewInteractionOverlay_ != nullptr
        ? static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)
        : nullptr;
    QWidget* activeWindow = QApplication::activeWindow();
    if (
        overlay != nullptr &&
        (overlay->ownsPointerInteraction() || activeWindow == previewInteractionOverlay_)) {
        return false;
    }

    if (previewInteractionMainWindowDeactivated_) {
        if (qobject_cast<QDockWidget*>(activeWindow) != nullptr) {
            return false;
        }
        return true;
    }

    return false;
}

void MainWindow::suppressPreviewInteractionOverlay()
{
    if (previewInteractionOverlay_ == nullptr) {
        return;
    }

    static_cast<PreviewInteractionOverlay*>(
        previewInteractionOverlay_)->cancelActiveInteraction();
    previewInteractionOverlay_->hide();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr) {
        const QEvent::Type eventType = event->type();

        if (watched == this) {
            if (eventType == QEvent::WindowDeactivate) {
                // The preview interaction surface is an owned no-activate tool window
                // above the native libobs preview. A left click can briefly deactivate
                // QMainWindow before the overlay receives the press. Defer the decision
                // and keep the overlay alive when it owns that pointer interaction.
                QTimer::singleShot(0, this, [this]() {
                    auto* overlay = previewInteractionOverlay_ != nullptr
                        ? static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)
                        : nullptr;
                    QWidget* activeWindow = QApplication::activeWindow();
                    if (
                        overlay != nullptr &&
                        (overlay->ownsPointerInteraction() || activeWindow == previewInteractionOverlay_)) {
                        previewInteractionMainWindowDeactivated_ = false;
                        syncPreviewInteractionOverlay();
                        return;
                    }
                    if (qobject_cast<QDockWidget*>(activeWindow) != nullptr) {
                        previewInteractionMainWindowDeactivated_ = false;
                        syncPreviewInteractionOverlay();
                        return;
                    }
                    previewInteractionMainWindowDeactivated_ = true;
                    suppressPreviewInteractionOverlay();
                });
            } else if (eventType == QEvent::WindowActivate) {
                previewInteractionMainWindowDeactivated_ = false;
                QTimer::singleShot(
                    0,
                    this,
                    &MainWindow::syncPreviewInteractionOverlay);
            }
        }

        if (eventType == QEvent::ApplicationDeactivate) {
            suppressPreviewInteractionOverlay();
        } else if (eventType == QEvent::ApplicationActivate) {
            QTimer::singleShot(
                0,
                this,
                &MainWindow::syncPreviewInteractionOverlay);
        }

        if (
            eventType == QEvent::Show &&
            isPreviewInteractionOverlayBlocker(watched)) {
            suppressPreviewInteractionOverlay();
        }

        auto* topLevelWidget = qobject_cast<QWidget*>(watched);
        if (
            topLevelWidget != nullptr &&
            topLevelWidget->isWindow() &&
            topLevelWidget != previewInteractionOverlay_ &&
            (
                eventType == QEvent::Hide ||
                eventType == QEvent::Destroy ||
                eventType == QEvent::WindowActivate ||
                eventType == QEvent::WindowDeactivate)) {
            QTimer::singleShot(
                0,
                this,
                &MainWindow::syncPreviewInteractionOverlay);
        }
    }

    if (
        watched == previewWidget_ ||
        (previewWidget_ != nullptr && watched == previewWidget_->parentWidget()) ||
        watched == centralWidget() ||
        watched == this) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
        case QEvent::WindowStateChange:
            QTimer::singleShot(
                0,
                this,
                &MainWindow::syncPreviewInteractionOverlay);
            break;
        default:
            break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (remuxProcess_ != nullptr) {
        const auto answer = QMessageBox::warning(
            this,
            QStringLiteral("MP4 Remux in Progress"),
            QStringLiteral(
                "Vuttara Studio is creating an MP4 copy. Cancel the remux and close? The original MKV will remain safe."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        remuxProcess_->kill();
        remuxProcess_->waitForFinished(3000);
        QFile::remove(remuxPartialPath_);
    }

    if (engine_ != nullptr && engine_->isStreaming()) {
        QSettings settings;
        if (settings.value(QStringLiteral("general/confirmStreamingCloseV1"), true).toBool()) {
            const auto response = QMessageBox::question(
                this, QStringLiteral("Streaming in Progress"),
                QStringLiteral("Vuttara Studio is live. Stop streaming before closing?"),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (response != QMessageBox::Yes) { event->ignore(); return; }
        }
        engine_->stopStreaming();
        QElapsedTimer waitTimer; waitTimer.start();
        while (engine_->isStreaming() && waitTimer.elapsed() < 15000) {
            engine_->streamingInfo();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(50);
        }
        if (engine_->isStreaming()) {
            QMessageBox::warning(this, QStringLiteral("Stream Still Stopping"), QStringLiteral("The stream has not stopped cleanly. Vuttara Studio will remain open."));
            event->ignore(); return;
        }
    }

    if (engine_ != nullptr && engine_->isRecording()) {
        QSettings settings;
        if (settings.value(
                QStringLiteral("general/confirmRecordingCloseV1"),
                true).toBool()) {
            const auto response = QMessageBox::question(
                this,
                QStringLiteral("Recording in Progress"),
                QStringLiteral(
                    "Vuttara Studio is recording. Stop and finalize the recording before closing?"),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel);

            if (response != QMessageBox::Yes) {
                event->ignore();
                return;
            }
        }

        engine_->stopRecording();
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (engine_->isRecording() && waitTimer.elapsed() < 15000) {
            engine_->recordingInfo();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(50);
        }

        if (engine_->isRecording()) {
            QMessageBox::warning(
                this,
                QStringLiteral("Recording Still Finalizing"),
                QStringLiteral(
                    "The recording has not finished finalizing. Vuttara Studio will remain open to protect the file."));
            event->ignore();
            return;
        }
    }

    if (previewInteractionOverlay_ != nullptr) {
        static_cast<PreviewInteractionOverlay*>(
            previewInteractionOverlay_)->cancelActiveInteraction();
        previewInteractionOverlay_->hide();
    }

    QSettings windowSettings;
    windowSettings.setValue(QStringLiteral("mainWindow/geometryV1"), saveGeometry());
    windowSettings.setValue(QStringLiteral("mainWindow/dockStateV10"), saveState(10));
    windowSettings.sync();

    saveProjectState();
    QMainWindow::closeEvent(event);
}

void MainWindow::createMenus()
{
    QMenuBar* applicationMenuBar = menuBar();
    applicationMenuBar->clear();
    applicationMenuBar->setNativeMenuBar(false);
    applicationMenuBar->setObjectName(QStringLiteral("mainMenuBar"));

    auto* fileMenu = applicationMenuBar->addMenu(QStringLiteral("&File"));
    auto* saveAction = fileMenu->addAction(QStringLiteral("&Save Project"));
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setStatusTip(QStringLiteral("Save scenes, sources, groups, audio state, and transforms."));
    connect(saveAction, &QAction::triggered, this, [this]() {
        if (saveProjectState()) {
            statusBar()->showMessage(QStringLiteral("Project saved."), 4000);
        }
    });

    auto* openRecordingFolderAction =
        fileMenu->addAction(QStringLiteral("Open &Recording Folder"));
    openRecordingFolderAction->setStatusTip(QStringLiteral("Open the active recording output folder."));
    connect(
        openRecordingFolderAction,
        &QAction::triggered,
        this,
        &MainWindow::openRecordingFolder);

    fileMenu->addSeparator();
    auto* settingsAction = fileMenu->addAction(QStringLiteral("&Settings…"));
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    settingsAction->setMenuRole(QAction::PreferencesRole);
    settingsAction->setStatusTip(QStringLiteral("Open global video, audio, output, hotkey, and advanced settings."));
    connect(settingsAction, &QAction::triggered, this, [this]() {
        showSettingsDialog(0);
    });

    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(QStringLiteral("E&xit"));
    exitAction->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
    exitAction->setMenuRole(QAction::QuitRole);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = applicationMenuBar->addMenu(QStringLiteral("&Edit"));
    undoTransformAction_ = editMenu->addAction(QStringLiteral("&Undo Transform"));
    undoTransformAction_->setShortcut(QKeySequence::Undo);
    connect(undoTransformAction_, &QAction::triggered, this, &MainWindow::undoTransform);

    redoTransformAction_ = editMenu->addAction(QStringLiteral("&Redo Transform"));
    redoTransformAction_->setShortcut(QKeySequence::Redo);
    connect(redoTransformAction_, &QAction::triggered, this, &MainWindow::redoTransform);

    editMenu->addSeparator();
    duplicateSourceAction_ = editMenu->addAction(QStringLiteral("&Duplicate Selected Sources"));
    duplicateSourceAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(duplicateSourceAction_, &QAction::triggered, this, &MainWindow::duplicateSelectedSources);

    sourcePropertiesAction_ = editMenu->addAction(QStringLiteral("Source &Properties…"));
    sourcePropertiesAction_->setShortcut(QKeySequence(QStringLiteral("Alt+Return")));
    sourcePropertiesAction_->setStatusTip(QStringLiteral("Edit settings belonging to the selected source."));
    connect(sourcePropertiesAction_, &QAction::triggered, this, &MainWindow::showSelectedSourceProperties);

    sourceTransformAction_ = editMenu->addAction(QStringLiteral("Source &Transform…"));
    sourceTransformAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    sourceTransformAction_->setStatusTip(QStringLiteral("Edit position, bounds, crop, flip, and rotation."));
    connect(sourceTransformAction_, &QAction::triggered, this, &MainWindow::showSelectedSourceTransform);

    editMenu->addSeparator();
    fitSourceAction_ = editMenu->addAction(QStringLiteral("Fit Source to Canvas"));
    fitSourceAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    connect(fitSourceAction_, &QAction::triggered, this, &MainWindow::fitSelectedSourceToCanvas);

    centerSourceAction_ = editMenu->addAction(QStringLiteral("Center Source"));
    centerSourceAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(centerSourceAction_, &QAction::triggered, this, &MainWindow::centerSelectedSource);

    lockSourceAction_ = editMenu->addAction(QStringLiteral("Lock Source"));
    lockSourceAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    connect(lockSourceAction_, &QAction::triggered, this, &MainWindow::toggleSelectedSourceLock);

    editMenu->addSeparator();
    removeSourceAction_ = editMenu->addAction(QStringLiteral("Remove Selected Sources"));
    removeSourceAction_->setShortcut(QKeySequence::Delete);
    connect(removeSourceAction_, &QAction::triggered, this, &MainWindow::removeSelectedSource);

    auto* viewMenu = applicationMenuBar->addMenu(QStringLiteral("&View"));
    fullScreenAction_ = viewMenu->addAction(QStringLiteral("Full Screen"));
    fullScreenAction_->setCheckable(true);
    fullScreenAction_->setShortcut(QKeySequence(QStringLiteral("F11")));
    connect(fullScreenAction_, &QAction::triggered, this, &MainWindow::toggleFullScreen);

    auto* statusBarAction = viewMenu->addAction(QStringLiteral("Status Bar"));
    statusBarAction->setCheckable(true);
    statusBarAction->setChecked(!statusBar()->isHidden());
    connect(statusBarAction, &QAction::toggled, statusBar(), &QWidget::setVisible);

    auto* docksMenu = viewMenu->addMenu(QStringLiteral("Docks"));
    const auto addDockVisibilityAction = [docksMenu](QDockWidget* dock) {
        if (dock == nullptr) {
            return;
        }
        QAction* action = docksMenu->addAction(dock->windowTitle());
        action->setCheckable(true);
        action->setChecked(!dock->isHidden());
        QObject::connect(action, &QAction::toggled, dock, &QWidget::setVisible);
        QObject::connect(dock, &QDockWidget::visibilityChanged, action, &QAction::setChecked);
    };
    addDockVisibilityAction(scenesDock_);
    addDockVisibilityAction(sourcesDock_);
    addDockVisibilityAction(audioMixerDock_);

    auto* toolsMenu = applicationMenuBar->addMenu(QStringLiteral("&Tools"));
    auto* toolsRecordingFolder = toolsMenu->addAction(QStringLiteral("Open Recording Folder"));
    connect(toolsRecordingFolder, &QAction::triggered, this, &MainWindow::openRecordingFolder);

    auto* projectDataAction = toolsMenu->addAction(QStringLiteral("Open Project Data Folder"));
    connect(projectDataAction, &QAction::triggered, this, &MainWindow::openProjectDataFolder);

    auto* logsAction = toolsMenu->addAction(QStringLiteral("Open Logs Folder"));
    connect(logsAction, &QAction::triggered, this, &MainWindow::openLogsFolder);

    toolsMenu->addSeparator();
    auto* diagnosticsAction = toolsMenu->addAction(QStringLiteral("Recording Diagnostics…"));
    connect(diagnosticsAction, &QAction::triggered, this, [this]() {
        showSettingsDialog(6);
    });

    auto* helpMenu = applicationMenuBar->addMenu(QStringLiteral("&Help"));
    auto* gettingStartedAction = helpMenu->addAction(QStringLiteral("Getting Started"));
    connect(gettingStartedAction, &QAction::triggered, this, &MainWindow::showGettingStartedDialog);

    auto* keyboardShortcutsAction = helpMenu->addAction(QStringLiteral("Keyboard Shortcuts…"));
    connect(keyboardShortcutsAction, &QAction::triggered, this, [this]() {
        showSettingsDialog(5);
    });

    helpMenu->addSeparator();
    auto* updateAction = helpMenu->addAction(QStringLiteral("Check for Updates…"));
    connect(updateAction, &QAction::triggered, this, &MainWindow::showUpdateStatusDialog);

    helpMenu->addSeparator();
    auto* aboutAction = helpMenu->addAction(QStringLiteral("About Vuttara Studio"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

    updateSourceControls();
}

void MainWindow::applyNativeWindowChrome()
{
#ifdef Q_OS_WIN
    setAttribute(Qt::WA_NativeWindow);
    const HWND windowHandle = reinterpret_cast<HWND>(winId());
    if (windowHandle == nullptr) {
        return;
    }

    QLibrary dwmApi(QStringLiteral("dwmapi"));
    using DwmSetWindowAttributeFunction = HRESULT(WINAPI*)(
        HWND,
        DWORD,
        LPCVOID,
        DWORD);
    const auto dwmSetWindowAttribute =
        reinterpret_cast<DwmSetWindowAttributeFunction>(
            dwmApi.resolve("DwmSetWindowAttribute"));
    if (dwmSetWindowAttribute == nullptr) {
        return;
    }

    constexpr DWORD useImmersiveDarkMode = 20;
    constexpr DWORD useImmersiveDarkModeLegacy = 19;
    constexpr DWORD windowCornerPreference = 33;
    constexpr DWORD borderColorAttribute = 34;
    constexpr DWORD captionColorAttribute = 35;
    constexpr DWORD textColorAttribute = 36;
    constexpr DWORD systemBackdropType = 38;

    const BOOL darkModeEnabled = TRUE;
    if (FAILED(dwmSetWindowAttribute(
            windowHandle,
            useImmersiveDarkMode,
            &darkModeEnabled,
            sizeof(darkModeEnabled)))) {
        dwmSetWindowAttribute(
            windowHandle,
            useImmersiveDarkModeLegacy,
            &darkModeEnabled,
            sizeof(darkModeEnabled));
    }

    const int roundedCorners = 2;
    dwmSetWindowAttribute(
        windowHandle,
        windowCornerPreference,
        &roundedCorners,
        sizeof(roundedCorners));

    const COLORREF borderColor = RGB(70, 55, 82);
    const COLORREF captionColor = RGB(23, 21, 29);
    const COLORREF textColor = RGB(245, 242, 250);
    dwmSetWindowAttribute(
        windowHandle,
        borderColorAttribute,
        &borderColor,
        sizeof(borderColor));
    dwmSetWindowAttribute(
        windowHandle,
        captionColorAttribute,
        &captionColor,
        sizeof(captionColor));
    dwmSetWindowAttribute(
        windowHandle,
        textColorAttribute,
        &textColor,
        sizeof(textColor));

    const int mainWindowBackdrop = 2;
    dwmSetWindowAttribute(
        windowHandle,
        systemBackdropType,
        &mainWindowBackdrop,
        sizeof(mainWindowBackdrop));
#endif
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
        if (fullScreenAction_ != nullptr) {
            fullScreenAction_->setChecked(false);
        }
        applyNativeWindowChrome();
        return;
    }

    showFullScreen();
    if (fullScreenAction_ != nullptr) {
        fullScreenAction_->setChecked(true);
    }
}

void MainWindow::openProjectDataFolder()
{
    const QString path = QFileInfo(Vuttara::AppPaths::projectStatePath()).absolutePath();
    QDir().mkpath(path);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(
            this,
            QStringLiteral("Project Data Folder"),
            QStringLiteral("Windows could not open:\n%1").arg(path));
    }
}

void MainWindow::openLogsFolder()
{
    const QString path = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                             .filePath(QStringLiteral("logs"));
    QDir().mkpath(path);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(
            this,
            QStringLiteral("Logs Folder"),
            QStringLiteral("Windows could not open:\n%1").arg(path));
    }
}

void MainWindow::showGettingStartedDialog()
{
    QMessageBox::information(
        this,
        QStringLiteral("Getting Started"),
        QStringLiteral(
            "1. Add or select a scene.\n"
            "2. Use the + button in Sources to add Display or Window Capture.\n"
            "3. Click a source in the preview, then drag it to move or use its handles to resize.\n"
            "4. Double-click a source to open Properties.\n"
            "5. Select Desktop Audio and Mic/Aux devices.\n"
            "6. Configure global options through File > Settings.\n"
            "7. Start recording from the fixed bar below the preview."));
}

void MainWindow::showUpdateStatusDialog()
{
    if (updateManager_ == nullptr) {
        QMessageBox::warning(
            this,
            QStringLiteral("Vuttara Studio Updates"),
            QStringLiteral("The update service is unavailable in this session."));
        return;
    }
    updateManager_->checkForUpdates(true);
}


void MainWindow::createPreview()
{
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("panel"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto* headingRow = new QHBoxLayout();
    previewTitle_ = new QLabel(QStringLiteral("Main Scene Preview"), panel);
    QFont titleFont = previewTitle_->font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    previewTitle_->setFont(titleFont);
    headingRow->addWidget(previewTitle_);
    headingRow->addStretch();

#ifdef VUTTARA_STUDIO_RELEASE_BUILD
    const QString buildBadgeText = QStringLiteral("STABLE %1")
        .arg(QStringLiteral(VUTTARA_STUDIO_VERSION));
    const QString buildBadgeToolTip = QStringLiteral(
        "Stable release with verified HTTPS and SHA-256 updates.");
#else
    const QString buildBadgeText = QStringLiteral("LOCAL BUILD");
    const QString buildBadgeToolTip = QStringLiteral(
        "Sources Organization + Preview Selection Stage 8C V1 FIX3 — local development only");
#endif
    auto* badge = new QLabel(buildBadgeText, panel);
    badge->setObjectName(QStringLiteral("engineBadge"));
    badge->setToolTip(buildBadgeToolTip);
    headingRow->addWidget(badge);
    layout->addLayout(headingRow);

    auto* previewFrame = new QFrame(panel);
    previewFrame->setObjectName(QStringLiteral("previewFrame"));
    auto* previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(1, 1, 1, 1);

    previewWidget_ = new PreviewWidget(engine_, previewFrame);
    previewWidget_->setObjectName(QStringLiteral("mainPreviewWidget"));
    previewWidget_->setMouseTracking(true);
    previewWidget_->setToolTip(QStringLiteral(
        "Click or drag a marquee to select sources. Ctrl-click adds or removes a source, "
        "drag handles to resize, and Alt-drag handles to crop. Edits save automatically per scene."));
    previewLayout->addWidget(previewWidget_);
    layout->addWidget(previewFrame, 1);

    previewInteractionOverlay_ = new PreviewInteractionOverlay(this);
    static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)->setInputProxy(previewWidget_);
    static_cast<PreviewWidget*>(previewWidget_)->setInteractionEventHandler(
        [this](QEvent* interactionEvent) {
            if (
                interactionEvent == nullptr ||
                previewInteractionOverlay_ == nullptr ||
                shouldSuppressPreviewInteractionOverlay()) {
                return false;
            }
            QCoreApplication::sendEvent(previewInteractionOverlay_, interactionEvent);
            return interactionEvent->isAccepted();
        });
    static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)->configure(
        [this]() {
            return engine_ != nullptr && engine_->isReady()
                ? engine_->sourceInfos()
                : QVector<Vuttara::SourceInfo>{};
        },
        [this]() {
            return selectedSourceNames();
        },
        [this](const QStringList& sourceNames) {
            selectSourcesFromPreview(sourceNames);
        },
        [this](const QHash<QString, Vuttara::SourceTransform>& transforms) {
            return applyPreviewSourceTransforms(transforms);
        },
        [this](
            const QHash<QString, Vuttara::SourceTransform>& originals,
            const QHash<QString, Vuttara::SourceTransform>& current) {
            commitPreviewSourceTransforms(originals, current);
        },
        [this](const QHash<QString, Vuttara::SourceTransform>& originals) {
            cancelPreviewSourceTransforms(originals);
        },
        [this](const QString& sourceName) {
            selectSourcesFromPreview({sourceName});
            showSelectedSourceProperties();
        },
        [this](const QString& sourceName, const QPoint& globalPosition) {
            showPreviewSourceContextMenu(sourceName, globalPosition);
        },
        [this]() {
            return engine_ != nullptr && engine_->isReady();
        },
        [this](const QString& message) {
            statusBar()->showMessage(message, 4500);
        });

    previewWidget_->installEventFilter(this);
    previewFrame->installEventFilter(this);
    installEventFilter(this);
    QTimer::singleShot(0, this, &MainWindow::syncPreviewInteractionOverlay);

    previewInformation_ = new QLabel(panel);
    previewInformation_->setObjectName(QStringLiteral("previewInformation"));
    previewInformation_->setAlignment(Qt::AlignCenter);
    layout->addWidget(previewInformation_);

    auto* outputsPanel = new QFrame(panel);
    outputsPanel->setObjectName(QStringLiteral("recordingControlsPanel"));
    outputsPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    outputsPanel->setMaximumHeight(98);
    auto* outputsLayout = new QVBoxLayout(outputsPanel);
    outputsLayout->setContentsMargins(8, 5, 8, 5);
    outputsLayout->setSpacing(4);

    auto* streamingLayout = new QHBoxLayout();
    streamingLayout->setSpacing(7);
    auto* streamingHeading = new QLabel(QStringLiteral("Live Stream"), outputsPanel);
    streamingHeading->setObjectName(QStringLiteral("recordingHeading"));
    streamingLayout->addWidget(streamingHeading);
    streamingStatusLabel_ = new QLabel(QStringLiteral("Ready"), outputsPanel);
    streamingStatusLabel_->setObjectName(QStringLiteral("streamingStatusLabel"));
    streamingLayout->addWidget(streamingStatusLabel_);
    streamingLayout->addStretch(1);
    streamingLayout->addWidget(new QLabel(QStringLiteral("Elapsed"), outputsPanel));
    streamingElapsedLabel_ = new QLabel(QStringLiteral("00:00:00"), outputsPanel);
    streamingElapsedLabel_->setMinimumWidth(65);
    streamingLayout->addWidget(streamingElapsedLabel_);
    streamingLayout->addWidget(new QLabel(QStringLiteral("Upload"), outputsPanel));
    streamingBitrateLabel_ = new QLabel(QStringLiteral("0 Kbps"), outputsPanel);
    streamingBitrateLabel_->setMinimumWidth(70);
    streamingLayout->addWidget(streamingBitrateLabel_);
    streamingLayout->addWidget(new QLabel(QStringLiteral("Dropped"), outputsPanel));
    streamingDroppedLabel_ = new QLabel(QStringLiteral("0"), outputsPanel);
    streamingDroppedLabel_->setMinimumWidth(45);
    streamingLayout->addWidget(streamingDroppedLabel_);
    startStreamingButton_ = new QPushButton(QStringLiteral("Start Streaming"), outputsPanel);
    stopStreamingButton_ = new QPushButton(QStringLiteral("Stop Streaming"), outputsPanel);
    streamingLayout->addWidget(startStreamingButton_);
    streamingLayout->addWidget(stopStreamingButton_);
    outputsLayout->addLayout(streamingLayout);

    auto* recordingLayout = new QHBoxLayout();
    recordingLayout->setSpacing(7);
    auto* heading = new QLabel(QStringLiteral("Local Recording"), outputsPanel);
    heading->setObjectName(QStringLiteral("recordingHeading"));
    recordingLayout->addWidget(heading);
    recordingStatusLabel_ = new QLabel(QStringLiteral("Ready"), outputsPanel);
    recordingStatusLabel_->setObjectName(QStringLiteral("recordingStatusLabel"));
    recordingLayout->addWidget(recordingStatusLabel_);
    recordingLayout->addStretch(1);
    recordingLayout->addWidget(new QLabel(QStringLiteral("Elapsed"), outputsPanel));
    recordingElapsedLabel_ = new QLabel(QStringLiteral("00:00:00"), outputsPanel);
    recordingElapsedLabel_->setMinimumWidth(65);
    recordingLayout->addWidget(recordingElapsedLabel_);
    recordingLayout->addWidget(new QLabel(QStringLiteral("Encoder"), outputsPanel));
    recordingEncoderLabel_ = new QLabel(QStringLiteral("Automatic"), outputsPanel);
    recordingEncoderLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    recordingEncoderLabel_->setMinimumWidth(90);
    recordingLayout->addWidget(recordingEncoderLabel_);
    recordingLayout->addWidget(new QLabel(QStringLiteral("Written"), outputsPanel));
    recordingSizeLabel_ = new QLabel(QStringLiteral("0 MB"), outputsPanel);
    recordingSizeLabel_->setMinimumWidth(52);
    recordingLayout->addWidget(recordingSizeLabel_);
    startRecordingButton_ = new QPushButton(QStringLiteral("Start Recording"), outputsPanel);
    startRecordingButton_->setObjectName(QStringLiteral("startRecordingButton"));
    stopRecordingButton_ = new QPushButton(QStringLiteral("Stop Recording"), outputsPanel);
    stopRecordingButton_->setObjectName(QStringLiteral("stopRecordingButton"));
    recordingLayout->addWidget(startRecordingButton_);
    recordingLayout->addWidget(stopRecordingButton_);
    outputsLayout->addLayout(recordingLayout);

    connect(startStreamingButton_, &QPushButton::clicked, this, &MainWindow::startStreaming);
    connect(stopStreamingButton_, &QPushButton::clicked, this, &MainWindow::stopStreaming);
    connect(startRecordingButton_, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(stopRecordingButton_, &QPushButton::clicked, this, &MainWindow::stopRecording);

    streamingTimer_ = new QTimer(this);
    streamingTimer_->setInterval(500);
    connect(streamingTimer_, &QTimer::timeout, this, &MainWindow::updateStreamingControls);
    recordingTimer_ = new QTimer(this);
    recordingTimer_->setInterval(500);
    connect(recordingTimer_, &QTimer::timeout, this, &MainWindow::updateRecordingControls);

    layout->addWidget(outputsPanel);
    setCentralWidget(panel);
}

void MainWindow::createSceneDock()
{
    scenesDock_ = new QDockWidget(QStringLiteral("Scenes"), this);
    auto* dock = scenesDock_;
    dock->setObjectName(QStringLiteral("scenesDock"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setAttribute(Qt::WA_DeleteOnClose, false);
    dock->setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable);

    auto* container = new QWidget(dock);
    container->setMinimumWidth(190);
    auto* layout = new QVBoxLayout(container);

    scenesList_ = new QListWidget(container);
    scenesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(scenesList_);

    auto* buttons = new QHBoxLayout();
    addSceneButton_ = new QPushButton(QStringLiteral("Add Scene"), container);
    removeSceneButton_ = new QPushButton(QStringLiteral("Remove Scene"), container);
    buttons->addWidget(addSceneButton_);
    buttons->addWidget(removeSceneButton_);
    layout->addLayout(buttons);

    connect(addSceneButton_, &QPushButton::clicked, this, &MainWindow::addScene);
    connect(removeSceneButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedScene);
    connect(
        scenesList_,
        &QListWidget::currentItemChanged,
        this,
        &MainWindow::handleSceneSelectionChanged);

    dock->setWidget(container);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::createSourceDock()
{
    sourcesDock_ = new QDockWidget(QStringLiteral("Sources"), this);
    auto* dock = sourcesDock_;
    dock->setObjectName(QStringLiteral("sourcesDock"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setAttribute(Qt::WA_DeleteOnClose, false);
    dock->setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable);

    auto* container = new QWidget(dock);
    container->setMinimumWidth(240);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(7, 7, 7, 7);
    layout->setSpacing(6);

    sourcesList_ = new SourcesListWidget(container);
    sourcesList_->setObjectName(QStringLiteral("sourcesList"));
    sourcesList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sourcesList_->setContextMenuPolicy(Qt::CustomContextMenu);
    sourcesList_->setSpacing(1);
    sourcesList_->setUniformItemSizes(false);
    sourcesList_->setFrameShape(QFrame::NoFrame);
    sourcesList_->setToolTip(QStringLiteral(
        "Click the eye or lock icon directly. Ctrl-click selects multiple sources. "
        "Source folders expand in place like OBS Studio."));
    sourcesList_->setStyleSheet(QStringLiteral(
        "QListWidget#sourcesList { background: rgba(12, 11, 17, 90); border: 0; outline: 0; }"
        "QListWidget#sourcesList::item { border: 0; padding: 0; }"
        "QListWidget#sourcesList::item:selected { background: transparent; }"));
    static_cast<SourcesListWidget*>(sourcesList_)->setDropCallback(
        [this](const QByteArray& payload, QListWidgetItem* target, int position) {
            return handleSourceDockDrop(payload, target, position);
        });
    layout->addWidget(sourcesList_, 1);

    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(4);

    addSourceButton_ = new QToolButton(container);
    addSourceButton_->setObjectName(QStringLiteral("addSourceButton"));
    addSourceButton_->setText(QStringLiteral("+"));
    addSourceButton_->setToolTip(QStringLiteral("Add Source"));
    addSourceButton_->setPopupMode(QToolButton::InstantPopup);
    addSourceButton_->setAutoRaise(false);

    auto* addMenu = new QMenu(addSourceButton_);
    addMenu->setObjectName(QStringLiteral("addSourceMenu"));
    addDisplaySourceAction_ = addMenu->addAction(QStringLiteral("Display Capture"));
    addWindowSourceAction_ = addMenu->addAction(QStringLiteral("Window Capture"));
    addMenu->addSeparator();
    auto* futureSources = addMenu->addAction(QStringLiteral("More source types will appear here"));
    futureSources->setEnabled(false);
    addSourceButton_->setMenu(addMenu);

    removeSourceButton_ = new QToolButton(container);
    removeSourceButton_->setObjectName(QStringLiteral("removeSourceButton"));
    removeSourceButton_->setText(QStringLiteral("−"));
    removeSourceButton_->setToolTip(QStringLiteral("Remove Selected Source"));
    removeSourceButton_->setAutoRaise(false);

    sourceActionsButton_ = new QToolButton(container);
    sourceActionsButton_->setObjectName(QStringLiteral("sourceActionsButton"));
    sourceActionsButton_->setText(QStringLiteral("⋯"));
    sourceActionsButton_->setToolTip(QStringLiteral("Selected Source Actions"));
    sourceActionsButton_->setAutoRaise(false);

    toolbar->addWidget(addSourceButton_);
    toolbar->addWidget(removeSourceButton_);
    toolbar->addStretch(1);
    toolbar->addWidget(sourceActionsButton_);
    layout->addLayout(toolbar);

    connect(addDisplaySourceAction_, &QAction::triggered, this, &MainWindow::showAddDisplayCaptureDialog);
    connect(addWindowSourceAction_, &QAction::triggered, this, &MainWindow::showAddWindowCaptureDialog);
    connect(removeSourceButton_, &QToolButton::clicked, this, &MainWindow::removeSelectedSource);
    connect(sourceActionsButton_, &QToolButton::clicked, this, [this]() {
        QPoint anchor(8, sourcesList_ != nullptr ? sourcesList_->height() - 8 : 0);
        if (sourcesList_ != nullptr && sourcesList_->currentItem() != nullptr) {
            anchor = sourcesList_->visualItemRect(sourcesList_->currentItem()).center();
        }
        showSourceContextMenu(anchor);
    });
    connect(sourcesList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr && item->data(GroupRowRole).toBool()) {
            toggleSourceGroupExpanded(item->data(GroupNameRole).toString());
        } else {
            showSelectedSourceProperties();
        }
    });
    connect(
        sourcesList_,
        &QListWidget::customContextMenuRequested,
        this,
        &MainWindow::showSourceContextMenu);
    connect(
        sourcesList_,
        &QListWidget::currentItemChanged,
        this,
        [this](QListWidgetItem*, QListWidgetItem*) {
            refreshSourceRowVisuals();
            updateSourceControls();
            refreshPreviewInteractionOverlay();
        });
    connect(sourcesList_, &QListWidget::itemSelectionChanged, this, [this]() {
        refreshSourceRowVisuals();
        updateSourceControls();
        refreshPreviewInteractionOverlay();
    });

    dock->setWidget(container);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::createAudioDock()
{
    audioMixerDock_ = new QDockWidget(QStringLiteral("Audio Mixer"), this);

    auto* dock = audioMixerDock_;
    dock->setObjectName(QStringLiteral("audioMixerDock"));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setAttribute(Qt::WA_DeleteOnClose, false);
    dock->setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable);

    auto* container = new QWidget(dock);
    container->setObjectName(QStringLiteral("audioMixerPanel"));
    container->setMinimumWidth(230);

    auto* outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(6, 4, 6, 6);
    outerLayout->setSpacing(5);

    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(5);

    auto* explanation = new QLabel(
        QStringLiteral("Native dock: drag the title bar until the placement preview highlights, then release • IEC meters"),
        container);
    explanation->setObjectName(QStringLiteral("audioMixerHint"));
    explanation->setWordWrap(false);
    toolbar->addWidget(explanation, 1);

    refreshAudioDevicesButton_ = new QPushButton(QStringLiteral("Refresh"), container);
    refreshAudioDevicesButton_->setObjectName(QStringLiteral("audioRefreshButton"));
    refreshAudioDevicesButton_->setToolTip(QStringLiteral("Refresh Windows audio devices"));
    refreshAudioDevicesButton_->setMaximumWidth(76);
    toolbar->addWidget(refreshAudioDevicesButton_);

    outerLayout->addLayout(toolbar);

    auto* channelsLayout = new QVBoxLayout();
    channelsLayout->setContentsMargins(0, 0, 0, 0);
    channelsLayout->setSpacing(6);

    auto createChannel = [this, channelsLayout](
                             const QString& title,
                             QComboBox*& deviceCombo,
                             QLabel*& stateLabel,
                             QProgressBar*& meter,
                             QLabel*& levelLabel,
                             QSlider*& volume,
                             QLabel*& volumeValue,
                             QPushButton*& muteButton) {
        auto* group = new QGroupBox(title, this);
        group->setObjectName(QStringLiteral("audioChannelGroup"));
        group->setAccessibleName(title);
        group->setMaximumHeight(146);

        auto* layout = new QVBoxLayout(group);
        layout->setContentsMargins(8, 6, 8, 7);
        layout->setSpacing(4);

        deviceCombo = new QComboBox(group);
        deviceCombo->setObjectName(QStringLiteral("audioDeviceCombo"));
        deviceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        deviceCombo->setMinimumContentsLength(18);
        deviceCombo->setMaxVisibleItems(12);
        layout->addWidget(deviceCombo);

        stateLabel = new QLabel(QStringLiteral("Disabled"), group);
        stateLabel->setObjectName(QStringLiteral("audioChannelState"));
        stateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        stateLabel->setWordWrap(false);
        stateLabel->setMaximumHeight(18);
        layout->addWidget(stateLabel);

        auto* meterRow = new QHBoxLayout();
        meterRow->setContentsMargins(0, 0, 0, 0);
        meterRow->setSpacing(5);

        meter = new QProgressBar(group);
        meter->setObjectName(QStringLiteral("audioMeter"));
        meter->setRange(0, 100);
        meter->setValue(0);
        meter->setTextVisible(false);
        meter->setMinimumHeight(10);
        meter->setMaximumHeight(10);

        levelLabel = new QLabel(QStringLiteral("−∞ dB"), group);
        levelLabel->setObjectName(QStringLiteral("audioLevelLabel"));
        levelLabel->setMinimumWidth(50);
        levelLabel->setMaximumWidth(50);
        levelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        meterRow->addWidget(meter, 1);
        meterRow->addWidget(levelLabel);
        layout->addLayout(meterRow);

        auto* volumeRow = new QHBoxLayout();
        volumeRow->setContentsMargins(0, 0, 0, 0);
        volumeRow->setSpacing(5);

        volume = new QSlider(Qt::Horizontal, group);
        volume->setObjectName(QStringLiteral("audioVolumeSlider"));
        volume->setRange(0, 100);
        volume->setValue(70);

        volumeValue = new QLabel(QStringLiteral("70%"), group);
        volumeValue->setObjectName(QStringLiteral("audioVolumeValue"));
        volumeValue->setMinimumWidth(34);
        volumeValue->setMaximumWidth(34);
        volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        muteButton = new QPushButton(QStringLiteral("Mute"), group);
        muteButton->setObjectName(QStringLiteral("audioMuteButton"));
        muteButton->setMinimumWidth(64);
        muteButton->setMaximumWidth(70);

        volumeRow->addWidget(volume, 1);
        volumeRow->addWidget(volumeValue);
        volumeRow->addWidget(muteButton);
        layout->addLayout(volumeRow);

        channelsLayout->addWidget(group);
    };

    createChannel(
        QStringLiteral("Desktop Audio"),
        desktopDeviceCombo_,
        desktopAudioState_,
        desktopAudioMeter_,
        desktopAudioLevel_,
        desktopVolume_,
        desktopVolumeValue_,
        desktopMuteButton_);

    createChannel(
        QStringLiteral("Mic/Aux"),
        microphoneDeviceCombo_,
        microphoneAudioState_,
        microphoneAudioMeter_,
        microphoneAudioLevel_,
        microphoneVolume_,
        microphoneVolumeValue_,
        microphoneMuteButton_);

    outerLayout->addLayout(channelsLayout);
    outerLayout->addStretch(1);

    connect(refreshAudioDevicesButton_, &QPushButton::clicked, this, &MainWindow::refreshAudioDeviceLists);
    connect(desktopDeviceCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        applyAudioDevice(Vuttara::AudioChannelKind::Desktop, desktopDeviceCombo_);
    });
    connect(microphoneDeviceCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        applyAudioDevice(Vuttara::AudioChannelKind::Microphone, microphoneDeviceCombo_);
    });
    connect(desktopVolume_, &QSlider::valueChanged, this, [this](int value) {
        applyAudioVolume(Vuttara::AudioChannelKind::Desktop, value);
    });
    connect(microphoneVolume_, &QSlider::valueChanged, this, [this](int value) {
        applyAudioVolume(Vuttara::AudioChannelKind::Microphone, value);
    });
    connect(desktopVolume_, &QSlider::sliderReleased, this, [this]() { saveProjectState(); });
    connect(microphoneVolume_, &QSlider::sliderReleased, this, [this]() { saveProjectState(); });
    connect(desktopMuteButton_, &QPushButton::clicked, this, [this]() {
        toggleAudioMute(Vuttara::AudioChannelKind::Desktop);
    });
    connect(microphoneMuteButton_, &QPushButton::clicked, this, [this]() {
        toggleAudioMute(Vuttara::AudioChannelKind::Microphone);
    });

    connect(
        dock,
        &QDockWidget::topLevelChanged,
        this,
        [this](bool floating) {
            statusBar()->showMessage(
                floating
                    ? QStringLiteral(
                        "Audio Mixer is floating. Drag its title bar over Vuttara Studio to use the highlighted native dock preview.")
                    : QStringLiteral("Audio Mixer docked."),
                6000);
        });

    audioMeterTimer_ = new QTimer(this);
    audioMeterTimer_->setInterval(50);
    connect(audioMeterTimer_, &QTimer::timeout, this, &MainWindow::updateAudioMeters);
    audioMeterTimer_->start();

    dock->setWidget(container);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::applyDefaultDockLayout(bool persist)
{
    QDockWidget* scenesDock =
        findChild<QDockWidget*>(QStringLiteral("scenesDock"));
    QDockWidget* sourcesDock =
        findChild<QDockWidget*>(QStringLiteral("sourcesDock"));

    setUpdatesEnabled(false);

    if (scenesDock != nullptr) {
        scenesDock->setFloating(false);
        scenesDock->show();
        addDockWidget(Qt::LeftDockWidgetArea, scenesDock);
    }

    if (sourcesDock != nullptr) {
        sourcesDock->setFloating(false);
        sourcesDock->show();
        addDockWidget(Qt::RightDockWidgetArea, sourcesDock);
    }

    if (audioMixerDock_ != nullptr) {
        audioMixerDock_->setFloating(false);
        audioMixerDock_->show();
        addDockWidget(Qt::RightDockWidgetArea, audioMixerDock_);

        if (sourcesDock != nullptr) {
            splitDockWidget(
                sourcesDock,
                audioMixerDock_,
                Qt::Vertical);
        }
    }

    if (sourcesDock != nullptr && audioMixerDock_ != nullptr) {
        resizeDocks(
            QList<QDockWidget*>{
                sourcesDock,
                audioMixerDock_,
            },
            QList<int>{
                620,
                300,
            },
            Qt::Vertical);
    }

    if (scenesDock != nullptr && sourcesDock != nullptr) {
        resizeDocks(
            QList<QDockWidget*>{
                scenesDock,
                sourcesDock,
            },
            QList<int>{
                260,
                430,
            },
            Qt::Horizontal);
    }

    setUpdatesEnabled(true);

    if (persist) {
        QSettings settings;
        settings.remove(QStringLiteral("mainWindow/dockStateV1"));
        settings.remove(QStringLiteral("mainWindow/dockStateV2"));
        settings.remove(QStringLiteral("mainWindow/dockStateV3"));
        settings.remove(QStringLiteral("mainWindow/dockStateV4"));
        settings.remove(QStringLiteral("mainWindow/dockStateV6"));
        settings.remove(QStringLiteral("mainWindow/dockStateV7"));
        settings.remove(QStringLiteral("mainWindow/dockStateV8"));
        settings.remove(QStringLiteral("mainWindow/dockStateV9"));
        settings.remove(QStringLiteral("mainWindow/dockStateV10"));
        settings.setValue(
            QStringLiteral("mainWindow/dockStateV10"),
            saveState(10));
        settings.sync();
    }
}

Vuttara::StreamingSettings MainWindow::streamingSettings() const
{
    QSettings store;
    Vuttara::StreamingSettings settings;
    settings.server = store.value(QStringLiteral("streaming/serverV1")).toString().trimmed();
    settings.streamKey = unprotectSettingSecret(store.value(QStringLiteral("streaming/streamKeyProtectedV1")).toString());
    settings.useAuthentication = store.value(QStringLiteral("streaming/useAuthenticationV1"), false).toBool();
    settings.username = store.value(QStringLiteral("streaming/usernameV1")).toString();
    settings.password = unprotectSettingSecret(store.value(QStringLiteral("streaming/passwordProtectedV1")).toString());
    settings.encoderId = store.value(QStringLiteral("streaming/encoderIdV1")).toString();
    settings.outputWidth = store.value(QStringLiteral("streaming/outputWidthV1"), 1920).toInt();
    settings.outputHeight = store.value(QStringLiteral("streaming/outputHeightV1"), 1080).toInt();
    settings.framesPerSecond = store.value(QStringLiteral("streaming/fpsV1"), 60).toInt();
    settings.videoBitrateKbps = std::clamp(store.value(QStringLiteral("streaming/videoBitrateKbpsV1"), 6000).toInt(), 1000, 50000);
    settings.keyframeIntervalSeconds = std::clamp(store.value(QStringLiteral("streaming/keyframeIntervalSecondsV1"), 2).toInt(), 1, 10);
    settings.audioBitrateKbps = std::clamp(store.value(QStringLiteral("streaming/audioBitrateKbpsV1"), 160).toInt(), 96, 320);
    settings.automaticReconnect = store.value(QStringLiteral("streaming/automaticReconnectV1"), true).toBool();
    settings.retryDelaySeconds = std::clamp(store.value(QStringLiteral("streaming/retryDelaySecondsV1"), 2).toInt(), 1, 60);
    settings.maximumRetries = std::clamp(store.value(QStringLiteral("streaming/maximumRetriesV1"), 20).toInt(), 0, 1000);
    return settings;
}

bool MainWindow::saveStreamingSettings(const Vuttara::StreamingSettings& settings)
{
    QSettings store;
    const QString protectedKey = protectSettingSecret(settings.streamKey);
    const QString protectedPassword = protectSettingSecret(settings.password);
    if (!settings.streamKey.isEmpty() && protectedKey.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("Streaming Settings"), QStringLiteral("Windows could not protect the stream key. Settings were not saved."));
        return false;
    }
    if (!settings.password.isEmpty() && protectedPassword.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("Streaming Settings"), QStringLiteral("Windows could not protect the streaming password. Settings were not saved."));
        return false;
    }
    store.setValue(QStringLiteral("streaming/serverV1"), settings.server.trimmed());
    store.setValue(QStringLiteral("streaming/streamKeyProtectedV1"), protectedKey);
    store.setValue(QStringLiteral("streaming/useAuthenticationV1"), settings.useAuthentication);
    store.setValue(QStringLiteral("streaming/usernameV1"), settings.username.trimmed());
    store.setValue(QStringLiteral("streaming/passwordProtectedV1"), protectedPassword);
    store.setValue(QStringLiteral("streaming/encoderIdV1"), settings.encoderId);
    store.setValue(QStringLiteral("streaming/outputWidthV1"), settings.outputWidth);
    store.setValue(QStringLiteral("streaming/outputHeightV1"), settings.outputHeight);
    store.setValue(QStringLiteral("streaming/fpsV1"), settings.framesPerSecond);
    store.setValue(QStringLiteral("streaming/videoBitrateKbpsV1"), settings.videoBitrateKbps);
    store.setValue(QStringLiteral("streaming/keyframeIntervalSecondsV1"), settings.keyframeIntervalSeconds);
    store.setValue(QStringLiteral("streaming/audioBitrateKbpsV1"), settings.audioBitrateKbps);
    store.setValue(QStringLiteral("streaming/automaticReconnectV1"), settings.automaticReconnect);
    store.setValue(QStringLiteral("streaming/retryDelaySecondsV1"), settings.retryDelaySeconds);
    store.setValue(QStringLiteral("streaming/maximumRetriesV1"), settings.maximumRetries);
    store.remove(QStringLiteral("streaming/streamKeyV1"));
    store.remove(QStringLiteral("streaming/passwordV1"));
    store.sync();
    return store.status() == QSettings::NoError;
}

Vuttara::RecordingSettings MainWindow::recordingSettings() const
{
    QSettings settingsStore;
    Vuttara::RecordingSettings settings;
    settings.outputDirectory = recordingOutputDirectory();
    settings.filenameFormat = settingsStore.value(
        QStringLiteral("recording/filenameFormatV1"),
        QStringLiteral("VuttaraStudio_{date}_{time}"))
        .toString()
        .trimmed();
    settings.encoderId = settingsStore.value(
        QStringLiteral("recording/encoderIdV1"))
        .toString();
    settings.outputWidth = settingsStore.value(
        QStringLiteral("recording/outputWidthV1"),
        1920)
        .toInt();
    settings.outputHeight = settingsStore.value(
        QStringLiteral("recording/outputHeightV1"),
        1080)
        .toInt();
    settings.framesPerSecond = settingsStore.value(
        QStringLiteral("recording/fpsV1"),
        60)
        .toInt();
    settings.videoBitrateKbps = settingsStore.value(
        QStringLiteral("recording/videoBitrateKbpsV1"),
        12000)
        .toInt();
    settings.audioBitrateKbps = settingsStore.value(
        QStringLiteral("recording/audioBitrateKbpsV1"),
        192)
        .toInt();
    settings.automaticRemuxToMp4 = settingsStore.value(
        QStringLiteral("recording/automaticRemuxToMp4V1"),
        false)
        .toBool();

    if (settings.filenameFormat.isEmpty()) {
        settings.filenameFormat = QStringLiteral("VuttaraStudio_{date}_{time}");
    }
    if (settings.framesPerSecond != 30 && settings.framesPerSecond != 60) {
        settings.framesPerSecond = 60;
    }
    settings.videoBitrateKbps = std::clamp(settings.videoBitrateKbps, 1000, 50000);
    settings.audioBitrateKbps = std::clamp(settings.audioBitrateKbps, 96, 320);
    return settings;
}

void MainWindow::saveRecordingSettings(
    const Vuttara::RecordingSettings& settings)
{
    QSettings settingsStore;
    settingsStore.setValue(
        QStringLiteral("recording/filenameFormatV1"),
        settings.filenameFormat);
    settingsStore.setValue(
        QStringLiteral("recording/encoderIdV1"),
        settings.encoderId);
    settingsStore.setValue(
        QStringLiteral("recording/outputWidthV1"),
        settings.outputWidth);
    settingsStore.setValue(
        QStringLiteral("recording/outputHeightV1"),
        settings.outputHeight);
    settingsStore.setValue(
        QStringLiteral("recording/fpsV1"),
        settings.framesPerSecond);
    settingsStore.setValue(
        QStringLiteral("recording/videoBitrateKbpsV1"),
        settings.videoBitrateKbps);
    settingsStore.setValue(
        QStringLiteral("recording/audioBitrateKbpsV1"),
        settings.audioBitrateKbps);
    settingsStore.setValue(
        QStringLiteral("recording/automaticRemuxToMp4V1"),
        settings.automaticRemuxToMp4);
    settingsStore.sync();
}

void MainWindow::refreshRecordingEstimate()
{
    if (recordingEstimateLabel_ == nullptr) {
        return;
    }

    const Vuttara::RecordingSettings settings = recordingSettings();
    const std::uint64_t oneHour = estimatedRecordingBytes(settings, 3600);
    QStorageInfo storage(settings.outputDirectory);
    storage.refresh();

    QString text = QStringLiteral("Est. %1/hour").arg(humanBytes(oneHour));
    if (storage.isValid() && storage.isReady()) {
        text += QStringLiteral(" • %1 free").arg(
            humanBytes(static_cast<std::uint64_t>(
                std::max<qint64>(0, storage.bytesAvailable()))));
    }
    recordingEstimateLabel_->setText(text);
    recordingEstimateLabel_->setToolTip(QStringLiteral(
        "Estimate uses the configured video and audio bitrates. Actual size can vary by encoder and content."));
}

void MainWindow::showRecordingSettingsDialog()
{
    showSettingsDialog(4);
}

void MainWindow::refreshHotkeys()
{
    QSettings settings;
    const auto sequence = [&settings](const QString& key, const QString& fallback) {
        return QKeySequence(settings.value(key, fallback).toString());
    };

    if (startStreamingShortcut_ == nullptr) {
        startStreamingShortcut_ = new QShortcut(QKeySequence(), this);
        startStreamingShortcut_->setContext(Qt::ApplicationShortcut);
        connect(startStreamingShortcut_, &QShortcut::activated, this, [this]() {
            if (engine_ != nullptr && !engine_->isStreaming()) startStreaming();
        });
    }
    startStreamingShortcut_->setKey(sequence(QStringLiteral("hotkeys/startStreamingV1"), QStringLiteral("F7")));
    if (stopStreamingShortcut_ == nullptr) {
        stopStreamingShortcut_ = new QShortcut(QKeySequence(), this);
        stopStreamingShortcut_->setContext(Qt::ApplicationShortcut);
        connect(stopStreamingShortcut_, &QShortcut::activated, this, [this]() {
            if (engine_ != nullptr && engine_->isStreaming()) stopStreaming();
        });
    }
    stopStreamingShortcut_->setKey(sequence(QStringLiteral("hotkeys/stopStreamingV1"), QStringLiteral("F8")));

    if (startRecordingShortcut_ == nullptr) {
        startRecordingShortcut_ = new QShortcut(QKeySequence(), this);
        startRecordingShortcut_->setContext(Qt::ApplicationShortcut);
        connect(startRecordingShortcut_, &QShortcut::activated, this, [this]() {
            if (engine_ != nullptr && !engine_->isRecording()) {
                startRecording();
            }
        });
    }
    startRecordingShortcut_->setKey(sequence(
        QStringLiteral("hotkeys/startRecordingV1"),
        QStringLiteral("F9")));

    if (stopRecordingShortcut_ == nullptr) {
        stopRecordingShortcut_ = new QShortcut(QKeySequence(), this);
        stopRecordingShortcut_->setContext(Qt::ApplicationShortcut);
        connect(stopRecordingShortcut_, &QShortcut::activated, this, [this]() {
            if (engine_ != nullptr && engine_->isRecording()) {
                stopRecording();
            }
        });
    }
    stopRecordingShortcut_->setKey(sequence(
        QStringLiteral("hotkeys/stopRecordingV1"),
        QStringLiteral("F10")));

    if (desktopMuteShortcut_ == nullptr) {
        desktopMuteShortcut_ = new QShortcut(QKeySequence(), this);
        desktopMuteShortcut_->setContext(Qt::ApplicationShortcut);
        connect(desktopMuteShortcut_, &QShortcut::activated, this, [this]() {
            toggleAudioMute(Vuttara::AudioChannelKind::Desktop);
        });
    }
    desktopMuteShortcut_->setKey(sequence(
        QStringLiteral("hotkeys/muteDesktopV1"),
        QStringLiteral("Ctrl+Alt+D")));

    if (microphoneMuteShortcut_ == nullptr) {
        microphoneMuteShortcut_ = new QShortcut(QKeySequence(), this);
        microphoneMuteShortcut_->setContext(Qt::ApplicationShortcut);
        connect(microphoneMuteShortcut_, &QShortcut::activated, this, [this]() {
            toggleAudioMute(Vuttara::AudioChannelKind::Microphone);
        });
    }
    microphoneMuteShortcut_->setKey(sequence(
        QStringLiteral("hotkeys/muteMicrophoneV1"),
        QStringLiteral("Ctrl+Alt+M")));

    if (nextSceneShortcut_ == nullptr) {
        nextSceneShortcut_ = new QShortcut(QKeySequence(), this);
        nextSceneShortcut_->setContext(Qt::ApplicationShortcut);
        connect(nextSceneShortcut_, &QShortcut::activated, this, [this]() {
            cycleScene(1);
        });
    }
    nextSceneShortcut_->setKey(sequence(
        QStringLiteral("hotkeys/nextSceneV1"),
        QStringLiteral("PageDown")));

    if (previousSceneShortcut_ == nullptr) {
        previousSceneShortcut_ = new QShortcut(QKeySequence(), this);
        previousSceneShortcut_->setContext(Qt::ApplicationShortcut);
        connect(previousSceneShortcut_, &QShortcut::activated, this, [this]() {
            cycleScene(-1);
        });
    }
    previousSceneShortcut_->setKey(sequence(
        QStringLiteral("hotkeys/previousSceneV1"),
        QStringLiteral("PageUp")));

    const auto configureNudge = [this](
                                      QShortcut*& shortcut,
                                      const QKeySequence& key,
                                      double dx,
                                      double dy) {
        if (shortcut == nullptr) {
            shortcut = new QShortcut(key, this);
            shortcut->setContext(Qt::WindowShortcut);
            connect(shortcut, &QShortcut::activated, this, [this, dx, dy]() {
                nudgeSelectedSources(dx, dy);
            });
        } else {
            shortcut->setKey(key);
        }
    };
    configureNudge(nudgeLeftShortcut_, QKeySequence(Qt::Key_Left), -1.0, 0.0);
    configureNudge(nudgeRightShortcut_, QKeySequence(Qt::Key_Right), 1.0, 0.0);
    configureNudge(nudgeUpShortcut_, QKeySequence(Qt::Key_Up), 0.0, -1.0);
    configureNudge(nudgeDownShortcut_, QKeySequence(Qt::Key_Down), 0.0, 1.0);
    configureNudge(nudgeLeftFastShortcut_, QKeySequence(QStringLiteral("Shift+Left")), -10.0, 0.0);
    configureNudge(nudgeRightFastShortcut_, QKeySequence(QStringLiteral("Shift+Right")), 10.0, 0.0);
    configureNudge(nudgeUpFastShortcut_, QKeySequence(QStringLiteral("Shift+Up")), 0.0, -10.0);
    configureNudge(nudgeDownFastShortcut_, QKeySequence(QStringLiteral("Shift+Down")), 0.0, 10.0);
}

void MainWindow::cycleScene(int direction)
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QVector<Vuttara::SceneInfo> scenes = engine_->sceneInfos();
    if (scenes.size() < 2) {
        return;
    }

    int currentIndex = 0;
    for (int index = 0; index < scenes.size(); ++index) {
        if (scenes.at(index).active) {
            currentIndex = index;
            break;
        }
    }

    const int count = scenes.size();
    const int targetIndex = (currentIndex + direction + count) % count;
    const QString targetName = scenes.at(targetIndex).name;
    if (!engine_->switchScene(targetName)) {
        statusBar()->showMessage(engine_->lastError(), 5000);
        return;
    }

    saveProjectState();
    refreshSceneList();
    refreshSourceGroupTabs();
    refreshSourceList();
    updatePreviewInformation();
    statusBar()->showMessage(QStringLiteral("Active scene: %1").arg(targetName), 4000);
}

void MainWindow::showSettingsDialog(int initialPage)
{
    if (engine_ == nullptr || !engine_->isReady()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Settings"),
            QStringLiteral("The media engine must be ready before settings can be changed."));
        return;
    }
    if (engine_->isRecording() || engine_->isStreaming() || remuxProcess_ != nullptr) {
        QMessageBox::information(
            this,
            QStringLiteral("Settings Locked"),
            QStringLiteral(
                "Stop streaming and recording, then wait for any MP4 remux to finish before changing output-critical settings."));
        return;
    }

    QSettings settingsStore;
    const Vuttara::StreamingSettings currentStreaming = streamingSettings();
    const Vuttara::RecordingSettings currentRecording = recordingSettings();
    const Vuttara::AudioChannelInfo currentDesktop =
        engine_->audioChannelInfo(Vuttara::AudioChannelKind::Desktop);
    const Vuttara::AudioChannelInfo currentMicrophone =
        engine_->audioChannelInfo(Vuttara::AudioChannelKind::Microphone);
    const QVector<Vuttara::AudioDeviceInfo> desktopDevices =
        engine_->availableDesktopAudioDevices();
    const QVector<Vuttara::AudioDeviceInfo> microphoneDevices =
        engine_->availableMicrophoneDevices();

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("settingsDialog"));
    dialog.setWindowTitle(QStringLiteral("Vuttara Studio Settings"));
    dialog.setMinimumSize(850, 610);

    auto* outerLayout = new QVBoxLayout(&dialog);
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(12);

    auto* categories = new QListWidget(&dialog);
    categories->setObjectName(QStringLiteral("settingsCategories"));
    categories->setFixedWidth(150);
    categories->addItems({
        QStringLiteral("General"),
        QStringLiteral("Video"),
        QStringLiteral("Audio"),
        QStringLiteral("Streaming"),
        QStringLiteral("Output"),
        QStringLiteral("Hotkeys"),
        QStringLiteral("Advanced"),
    });
    contentLayout->addWidget(categories);

    auto* pages = new QStackedWidget(&dialog);
    pages->setObjectName(QStringLiteral("settingsPages"));
    contentLayout->addWidget(pages, 1);
    outerLayout->addLayout(contentLayout, 1);

    const auto createPage = [pages](const QString& title, const QString& explanation) {
        auto* scroll = new QScrollArea(pages);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* page = new QWidget(scroll);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(8, 4, 12, 8);
        layout->setSpacing(12);
        auto* heading = new QLabel(title, page);
        heading->setObjectName(QStringLiteral("settingsPageTitle"));
        QFont font = heading->font();
        font.setPointSize(14);
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        auto* description = new QLabel(explanation, page);
        description->setObjectName(QStringLiteral("settingsPageDescription"));
        description->setWordWrap(true);
        layout->addWidget(description);
        scroll->setWidget(page);
        pages->addWidget(scroll);
        return qMakePair(page, layout);
    };

    // General
    const auto generalPage = createPage(
        QStringLiteral("General"),
        QStringLiteral(
            "Application-wide behavior. Dock movement remains in the validated Stage 6A FIX3 stability mode."));
    auto* confirmationsGroup = new QGroupBox(
        QStringLiteral("Confirmations"), generalPage.first);
    auto* confirmationsLayout = new QVBoxLayout(confirmationsGroup);
    confirmationsLayout->setSpacing(9);

    auto* confirmSceneRemoval = new QCheckBox(
        QStringLiteral("Confirm before removing a scene"), confirmationsGroup);
    confirmSceneRemoval->setChecked(settingsStore.value(
        QStringLiteral("general/confirmSceneRemovalV1"), true).toBool());
    confirmationsLayout->addWidget(confirmSceneRemoval);

    auto* confirmSourceRemoval = new QCheckBox(
        QStringLiteral("Confirm before removing a source"), confirmationsGroup);
    confirmSourceRemoval->setChecked(settingsStore.value(
        QStringLiteral("general/confirmSourceRemovalV1"), true).toBool());
    confirmationsLayout->addWidget(confirmSourceRemoval);

    auto* confirmRecordingClose = new QCheckBox(
        QStringLiteral("Confirm before closing while recording"), confirmationsGroup);
    confirmRecordingClose->setChecked(settingsStore.value(
        QStringLiteral("general/confirmRecordingCloseV1"), true).toBool());
    confirmationsLayout->addWidget(confirmRecordingClose);
    auto* confirmStreamingClose = new QCheckBox(
        QStringLiteral("Confirm before closing while streaming"), confirmationsGroup);
    confirmStreamingClose->setChecked(settingsStore.value(
        QStringLiteral("general/confirmStreamingCloseV1"), true).toBool());
    confirmationsLayout->addWidget(confirmStreamingClose);
    generalPage.second->addWidget(confirmationsGroup);

    auto* interfaceGroup = new QGroupBox(
        QStringLiteral("Interface"), generalPage.first);
    auto* interfaceLayout = new QVBoxLayout(interfaceGroup);
    auto* interfaceDescription = new QLabel(
        QStringLiteral(
            "The modern dark interface, compact menus, native Windows title bar, and validated stable dock behavior are applied automatically."),
        interfaceGroup);
    interfaceDescription->setWordWrap(true);
    interfaceDescription->setObjectName(QStringLiteral("settingsInlineDescription"));
    interfaceLayout->addWidget(interfaceDescription);
    generalPage.second->addWidget(interfaceGroup);
    generalPage.second->addStretch();

    // Video
    const auto videoPage = createPage(
        QStringLiteral("Video"),
        QStringLiteral(
            "Canvas and recording-output geometry. The base canvas remains fixed in this release."));
    auto* videoForm = new QFormLayout();
    auto* baseCanvas = new QLabel(QStringLiteral("1920 × 1080 (Direct3D 11)"), videoPage.first);
    videoForm->addRow(QStringLiteral("Base canvas"), baseCanvas);

    auto* resolutionCombo = new QComboBox(videoPage.first);
    resolutionCombo->addItem(QStringLiteral("1920 × 1080"), QStringLiteral("1920x1080"));
    resolutionCombo->addItem(QStringLiteral("1600 × 900"), QStringLiteral("1600x900"));
    resolutionCombo->addItem(QStringLiteral("1280 × 720"), QStringLiteral("1280x720"));
    resolutionCombo->addItem(QStringLiteral("854 × 480"), QStringLiteral("854x480"));
    const QString resolutionKey = QStringLiteral("%1x%2")
        .arg(currentRecording.outputWidth)
        .arg(currentRecording.outputHeight);
    resolutionCombo->setCurrentIndex(std::max(0, resolutionCombo->findData(resolutionKey)));
    videoForm->addRow(QStringLiteral("Output resolution"), resolutionCombo);

    auto* fpsCombo = new QComboBox(videoPage.first);
    fpsCombo->addItem(QStringLiteral("60 FPS"), 60);
    fpsCombo->addItem(QStringLiteral("30 FPS"), 30);
    fpsCombo->setCurrentIndex(std::max(0, fpsCombo->findData(currentRecording.framesPerSecond)));
    videoForm->addRow(QStringLiteral("Frame rate"), fpsCombo);
    videoPage.second->addLayout(videoForm);
    videoPage.second->addStretch();

    // Audio
    const auto audioPage = createPage(
        QStringLiteral("Audio"),
        QStringLiteral(
            "Global Desktop Audio and Mic/Aux devices. Mixer volume and mute controls remain in the Audio Mixer dock."));
    auto* audioForm = new QFormLayout();
    audioForm->addRow(
        QStringLiteral("Audio foundation"),
        new QLabel(QStringLiteral("48 kHz stereo"), audioPage.first));

    auto* desktopCombo = new QComboBox(audioPage.first);
    desktopCombo->addItem(QStringLiteral("Disabled"), QString());
    for (const Vuttara::AudioDeviceInfo& device : desktopDevices) {
        desktopCombo->addItem(device.name, device.deviceId);
    }
    desktopCombo->setCurrentIndex(std::max(0, desktopCombo->findData(currentDesktop.deviceId)));
    audioForm->addRow(QStringLiteral("Desktop Audio"), desktopCombo);

    auto* microphoneCombo = new QComboBox(audioPage.first);
    microphoneCombo->addItem(QStringLiteral("Disabled"), QString());
    for (const Vuttara::AudioDeviceInfo& device : microphoneDevices) {
        microphoneCombo->addItem(device.name, device.deviceId);
    }
    microphoneCombo->setCurrentIndex(std::max(0, microphoneCombo->findData(currentMicrophone.deviceId)));
    audioForm->addRow(QStringLiteral("Mic/Aux"), microphoneCombo);
    audioPage.second->addLayout(audioForm);
    auto* audioHint = new QLabel(
        QStringLiteral(
            "Changing either device reconnects its libobs WASAPI source after Apply or OK. Existing volume and mute state are retained."),
        audioPage.first);
    audioHint->setWordWrap(true);
    audioPage.second->addWidget(audioHint);
    audioPage.second->addStretch();

    // Streaming
    const auto streamingPage = createPage(
        QStringLiteral("Streaming"),
        QStringLiteral(
            "Choose a streaming service, server, credentials, and reconnect behavior. "
            "Encoding and bitrate controls remain under Output."));

    auto* streamingServiceGroup = new QGroupBox(
        QStringLiteral("Streaming Service"), streamingPage.first);
    auto* streamingServiceForm = new QFormLayout(streamingServiceGroup);

    auto* streamService = new QComboBox(streamingServiceGroup);
    streamService->addItem(QStringLiteral("Twitch"), QStringLiteral("twitch"));
    streamService->addItem(QStringLiteral("YouTube"), QStringLiteral("youtube"));
    streamService->addItem(QStringLiteral("Kick"), QStringLiteral("kick"));
    streamService->addItem(QStringLiteral("Facebook Live"), QStringLiteral("facebook"));
    streamService->addItem(QStringLiteral("Rumble"), QStringLiteral("rumble"));
    streamService->addItem(QStringLiteral("BEAM (beamstream.gg)"), QStringLiteral("beam"));
    streamService->addItem(QStringLiteral("Custom RTMP/RTMPS"), QStringLiteral("custom"));

    QString currentServiceId = settingsStore.value(
        QStringLiteral("streaming/serviceIdV1"),
        QStringLiteral("custom")).toString().trimmed().toLower();
    if (streamService->findData(currentServiceId) < 0) {
        currentServiceId = QStringLiteral("custom");
    }
    streamService->setCurrentIndex(std::max(0, streamService->findData(currentServiceId)));
    streamingServiceForm->addRow(QStringLiteral("Service"), streamService);

    auto* streamServer = new QComboBox(streamingServiceGroup);
    streamServer->setEditable(true);
    streamServer->setInsertPolicy(QComboBox::NoInsert);
    streamServer->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    streamServer->setMinimumContentsLength(42);
    streamingServiceForm->addRow(QStringLiteral("Server / region"), streamServer);

    auto* streamServiceGuidance = new QLabel(streamingServiceGroup);
    streamServiceGuidance->setWordWrap(true);
    streamServiceGuidance->setObjectName(QStringLiteral("settingsInlineDescription"));
    streamingServiceForm->addRow(QStringLiteral("Setup"), streamServiceGuidance);

    auto* streamKey = new QLineEdit(currentStreaming.streamKey, streamingServiceGroup);
    streamKey->setEchoMode(QLineEdit::Password);
    streamKey->setPlaceholderText(QStringLiteral("Stream key"));
    streamingServiceForm->addRow(QStringLiteral("Stream key"), streamKey);

    auto* streamAuth = new QCheckBox(
        QStringLiteral("Use server authentication"), streamingServiceGroup);
    streamAuth->setChecked(currentStreaming.useAuthentication);
    streamingServiceForm->addRow(QStringLiteral("Authentication"), streamAuth);

    auto* streamUsername = new QLineEdit(currentStreaming.username, streamingServiceGroup);
    auto* streamPassword = new QLineEdit(currentStreaming.password, streamingServiceGroup);
    streamPassword->setEchoMode(QLineEdit::Password);
    streamingServiceForm->addRow(QStringLiteral("Username"), streamUsername);
    streamingServiceForm->addRow(QStringLiteral("Password"), streamPassword);

    auto* secureStorage = new QLabel(
        QStringLiteral(
            "The stream key and password are encrypted with Windows DPAPI and are never written to logs."),
        streamingServiceGroup);
    secureStorage->setWordWrap(true);
    streamingServiceForm->addRow(QStringLiteral("Credential storage"), secureStorage);

    streamingPage.second->addWidget(streamingServiceGroup);

    auto* reconnectGroup = new QGroupBox(
        QStringLiteral("Connection Recovery"), streamingPage.first);
    auto* reconnectForm = new QFormLayout(reconnectGroup);

    auto* streamReconnect = new QCheckBox(
        QStringLiteral("Reconnect automatically"), reconnectGroup);
    streamReconnect->setChecked(currentStreaming.automaticReconnect);
    reconnectForm->addRow(QStringLiteral("Reconnect"), streamReconnect);

    auto* streamRetryDelay = new QSpinBox(reconnectGroup);
    streamRetryDelay->setRange(1, 60);
    streamRetryDelay->setSuffix(QStringLiteral(" seconds"));
    streamRetryDelay->setValue(currentStreaming.retryDelaySeconds);
    reconnectForm->addRow(QStringLiteral("Retry delay"), streamRetryDelay);

    auto* streamMaxRetries = new QSpinBox(reconnectGroup);
    streamMaxRetries->setRange(0, 1000);
    streamMaxRetries->setValue(currentStreaming.maximumRetries);
    reconnectForm->addRow(QStringLiteral("Maximum retries"), streamMaxRetries);

    streamingPage.second->addWidget(reconnectGroup);
    streamingPage.second->addStretch();

    const auto applyServicePreset = [=](const QString& serviceId, bool preserveServer) {
        const QString existingServer = preserveServer
            ? streamServer->currentText().trimmed()
            : QString();

        {
            QSignalBlocker blocker(streamServer);
            streamServer->clear();

            if (serviceId == QStringLiteral("twitch")) {
                streamServer->addItem(
                    QStringLiteral("rtmp://ingest.global-contribute.live-video.net/app"));
                streamServer->addItem(
                    QStringLiteral("rtmp://usw20.contribute.live-video.net/app"));
                streamServer->addItem(
                    QStringLiteral("rtmp://use20.contribute.live-video.net/app"));
                streamServiceGuidance->setText(QStringLiteral(
                    "Global ingest is recommended by default. Regional Twitch ingest choices are available when routing requires them."));
                streamServer->lineEdit()->setPlaceholderText(QStringLiteral(
                    "Choose a Twitch ingest server"));
            } else if (serviceId == QStringLiteral("youtube")) {
                streamServer->addItem(
                    QStringLiteral("rtmps://a.rtmps.youtube.com/live2"));
                streamServiceGuidance->setText(QStringLiteral(
                    "The secure primary YouTube ingest is preselected. Use the server URL shown in YouTube Live Control Room if your account provides a different value."));
                streamServer->lineEdit()->setPlaceholderText(QStringLiteral(
                    "Paste the YouTube Live server URL"));
            } else if (serviceId == QStringLiteral("kick")) {
                streamServer->addItem(
                    QStringLiteral("rtmps://fa723fc1b171.global-contribute.live-video.net:443/app"));
                streamServiceGuidance->setText(QStringLiteral(
                    "The current secure Kick ingest is preselected. Confirm it against Channel > Stream URL and Key in the Kick creator dashboard."));
                streamServer->lineEdit()->setPlaceholderText(QStringLiteral(
                    "Paste the Kick server URL"));
            } else if (serviceId == QStringLiteral("facebook")) {
                streamServiceGuidance->setText(QStringLiteral(
                    "Facebook Live provides the server URL and stream key through Live Producer. Paste the account-provided RTMPS server here."));
                streamServer->lineEdit()->setPlaceholderText(QStringLiteral(
                    "Paste the Facebook Live Producer server URL"));
            } else if (serviceId == QStringLiteral("rumble")) {
                streamServiceGuidance->setText(QStringLiteral(
                    "Rumble provides the RTMP or RTMPS server and stream key in its live-stream setup. Paste the account-provided server here."));
                streamServer->lineEdit()->setPlaceholderText(QStringLiteral(
                    "Paste the Rumble server URL"));
            } else if (serviceId == QStringLiteral("beam")) {
                streamServiceGuidance->setText(QStringLiteral(
                    "BEAM supports RTMP ingest. Paste the server URL supplied by the BEAM creator dashboard; no undocumented endpoint is hardcoded."));
                streamServer->lineEdit()->setPlaceholderText(QStringLiteral(
                    "Paste the BEAM server URL"));
            } else {
                streamServiceGuidance->setText(QStringLiteral(
                    "Enter any custom RTMP or RTMPS server supplied by the destination service."));
                streamServer->lineEdit()->setPlaceholderText(QStringLiteral(
                    "rtmps://server.example/live"));
            }

            if (!existingServer.isEmpty()) {
                const int matchingServer = streamServer->findText(
                    existingServer, Qt::MatchFixedString);
                if (matchingServer >= 0) {
                    streamServer->setCurrentIndex(matchingServer);
                } else {
                    streamServer->setEditText(existingServer);
                }
            } else if (streamServer->count() > 0) {
                streamServer->setCurrentIndex(0);
            } else {
                streamServer->setEditText(QString());
            }
        }
    };

    const auto updateStreamingConnectionControls = [=]() {
        streamUsername->setEnabled(streamAuth->isChecked());
        streamPassword->setEnabled(streamAuth->isChecked());
        streamRetryDelay->setEnabled(streamReconnect->isChecked());
        streamMaxRetries->setEnabled(streamReconnect->isChecked());
    };

    applyServicePreset(currentServiceId, true);
    if (!currentStreaming.server.isEmpty()) {
        const int matchingServer = streamServer->findText(
            currentStreaming.server, Qt::MatchFixedString);
        if (matchingServer >= 0) {
            streamServer->setCurrentIndex(matchingServer);
        } else {
            streamServer->setEditText(currentStreaming.server);
        }
    }

    connect(streamService, &QComboBox::currentIndexChanged, &dialog, [=](int) {
        applyServicePreset(streamService->currentData().toString(), false);
    });
    connect(streamAuth, &QCheckBox::toggled, &dialog, [=](bool) {
        updateStreamingConnectionControls();
    });
    connect(streamReconnect, &QCheckBox::toggled, &dialog, [=](bool) {
        updateStreamingConnectionControls();
    });
    updateStreamingConnectionControls();

    // Output
    const auto outputPage = createPage(
        QStringLiteral("Output"),
        QStringLiteral(
            "Streaming and recording encoders, resolutions, bitrates, storage, filenames, and optional MKV-to-MP4 remux."));
    auto* outputForm = new QFormLayout();

    auto* folderContainer = new QWidget(outputPage.first);
    auto* folderLayout = new QHBoxLayout(folderContainer);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    auto* folderEdit = new QLineEdit(currentRecording.outputDirectory, folderContainer);
    auto* browseFolder = new QPushButton(QStringLiteral("Browse…"), folderContainer);
    auto* openFolder = new QPushButton(QStringLiteral("Open"), folderContainer);
    folderLayout->addWidget(folderEdit, 1);
    folderLayout->addWidget(browseFolder);
    folderLayout->addWidget(openFolder);
    outputForm->addRow(QStringLiteral("Recording folder"), folderContainer);

    auto* presetCombo = new QComboBox(outputPage.first);
    presetCombo->addItem(QStringLiteral("High Quality"), QStringLiteral("high"));
    presetCombo->addItem(QStringLiteral("Balanced"), QStringLiteral("balanced"));
    presetCombo->addItem(QStringLiteral("Space Saver"), QStringLiteral("space"));
    presetCombo->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    presetCombo->setCurrentIndex(std::max(0, presetCombo->findData(settingsStore.value(
        QStringLiteral("recording/qualityPresetV1"), QStringLiteral("balanced")).toString())));
    outputForm->addRow(QStringLiteral("Quality preset"), presetCombo);

    auto* encoderCombo = new QComboBox(outputPage.first);
    encoderCombo->addItem(QStringLiteral("Automatic — hardware first"), QString());
    for (const Vuttara::RecordingEncoderInfo& encoder : engine_->availableRecordingEncoders()) {
        encoderCombo->addItem(
            QStringLiteral("%1%2")
                .arg(encoder.name)
                .arg(encoder.hardware ? QStringLiteral(" — Hardware") : QStringLiteral(" — Software")),
            encoder.id);
    }
    encoderCombo->setCurrentIndex(std::max(0, encoderCombo->findData(currentRecording.encoderId)));
    outputForm->addRow(QStringLiteral("Encoder"), encoderCombo);

    auto* videoBitrate = new QSpinBox(outputPage.first);
    videoBitrate->setRange(1000, 50000);
    videoBitrate->setSingleStep(500);
    videoBitrate->setSuffix(QStringLiteral(" Kbps"));
    videoBitrate->setValue(currentRecording.videoBitrateKbps);
    outputForm->addRow(QStringLiteral("Video bitrate"), videoBitrate);

    auto* audioBitrate = new QSpinBox(outputPage.first);
    audioBitrate->setRange(96, 320);
    audioBitrate->setSingleStep(32);
    audioBitrate->setSuffix(QStringLiteral(" Kbps"));
    audioBitrate->setValue(currentRecording.audioBitrateKbps);
    outputForm->addRow(QStringLiteral("Audio bitrate"), audioBitrate);

    auto* filenameFormat = new QLineEdit(currentRecording.filenameFormat, outputPage.first);
    filenameFormat->setPlaceholderText(QStringLiteral("VuttaraStudio_{date}_{time}"));
    filenameFormat->setToolTip(QStringLiteral(
        "Available placeholders: {date}, {time}, {scene}, {resolution}, {fps}"));
    outputForm->addRow(QStringLiteral("Filename format"), filenameFormat);

    const QString ffmpeg = findFfmpegExecutable();
    auto* remuxCheck = new QCheckBox(
        QStringLiteral("Automatically create an MP4 copy after MKV finalizes"),
        outputPage.first);
    remuxCheck->setChecked(currentRecording.automaticRemuxToMp4 && !ffmpeg.isEmpty());
    remuxCheck->setEnabled(!ffmpeg.isEmpty());
    outputForm->addRow(QStringLiteral("Automatic remux"), remuxCheck);

    auto* storageEstimate = new QLabel(outputPage.first);
    storageEstimate->setWordWrap(true);
    outputForm->addRow(QStringLiteral("Storage estimate"), storageEstimate);

    auto* remuxStatus = new QLabel(
        ffmpeg.isEmpty()
            ? QStringLiteral("ffmpeg.exe is unavailable. MKV recording remains fully enabled.")
            : QStringLiteral("Remux tool: %1").arg(ffmpeg),
        outputPage.first);
    remuxStatus->setWordWrap(true);
    outputForm->addRow(QStringLiteral("Remux status"), remuxStatus);
    auto* streamingOutputGroup = new QGroupBox(
        QStringLiteral("Streaming Output"), outputPage.first);
    auto* streamingOutputForm = new QFormLayout(streamingOutputGroup);

    auto* streamEncoder = new QComboBox(streamingOutputGroup);
    streamEncoder->addItem(QStringLiteral("Automatic — best available"), QString{});
    for (const Vuttara::RecordingEncoderInfo& encoder : engine_->availableRecordingEncoders()) {
        streamEncoder->addItem(
            QStringLiteral("%1%2")
                .arg(encoder.name)
                .arg(encoder.hardware
                    ? QStringLiteral(" — Hardware")
                    : QStringLiteral(" — Software")),
            encoder.id);
    }
    streamEncoder->setCurrentIndex(
        std::max(0, streamEncoder->findData(currentStreaming.encoderId)));
    streamingOutputForm->addRow(QStringLiteral("Encoder"), streamEncoder);

    auto* streamResolution = new QComboBox(streamingOutputGroup);
    streamResolution->addItem(
        QStringLiteral("1920 × 1080"), QStringLiteral("1920x1080"));
    streamResolution->addItem(
        QStringLiteral("1280 × 720"), QStringLiteral("1280x720"));
    streamResolution->setCurrentIndex(std::max(
        0,
        streamResolution->findData(
            QStringLiteral("%1x%2")
                .arg(currentStreaming.outputWidth)
                .arg(currentStreaming.outputHeight))));
    streamingOutputForm->addRow(QStringLiteral("Resolution"), streamResolution);

    auto* streamFps = new QComboBox(streamingOutputGroup);
    streamFps->addItem(QStringLiteral("60 FPS"), 60);
    streamFps->addItem(QStringLiteral("30 FPS"), 30);
    streamFps->setCurrentIndex(
        std::max(0, streamFps->findData(currentStreaming.framesPerSecond)));
    streamingOutputForm->addRow(QStringLiteral("Frame rate"), streamFps);

    auto* streamVideoBitrate = new QSpinBox(streamingOutputGroup);
    streamVideoBitrate->setRange(1000, 50000);
    streamVideoBitrate->setSingleStep(500);
    streamVideoBitrate->setSuffix(QStringLiteral(" Kbps"));
    streamVideoBitrate->setValue(currentStreaming.videoBitrateKbps);
    streamingOutputForm->addRow(QStringLiteral("Video bitrate"), streamVideoBitrate);

    auto* streamKeyframe = new QSpinBox(streamingOutputGroup);
    streamKeyframe->setRange(1, 10);
    streamKeyframe->setSuffix(QStringLiteral(" seconds"));
    streamKeyframe->setValue(currentStreaming.keyframeIntervalSeconds);
    streamingOutputForm->addRow(QStringLiteral("Keyframe interval"), streamKeyframe);

    auto* streamAudioBitrate = new QSpinBox(streamingOutputGroup);
    streamAudioBitrate->setRange(96, 320);
    streamAudioBitrate->setSingleStep(32);
    streamAudioBitrate->setSuffix(QStringLiteral(" Kbps"));
    streamAudioBitrate->setValue(currentStreaming.audioBitrateKbps);
    streamingOutputForm->addRow(QStringLiteral("Audio bitrate"), streamAudioBitrate);

    outputPage.second->addLayout(outputForm);
    outputPage.second->addWidget(streamingOutputGroup);
    outputPage.second->addStretch();

    // Hotkeys
    const auto hotkeysPage = createPage(
        QStringLiteral("Hotkeys"),
        QStringLiteral("Application-wide shortcuts. Empty shortcuts are allowed."));
    auto* hotkeyForm = new QFormLayout();
    auto* startStreamingHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(QStringLiteral("hotkeys/startStreamingV1"), QStringLiteral("F7")).toString()), hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Start streaming"), startStreamingHotkey);
    auto* stopStreamingHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(QStringLiteral("hotkeys/stopStreamingV1"), QStringLiteral("F8")).toString()), hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Stop streaming"), stopStreamingHotkey);
    auto* startHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(
            QStringLiteral("hotkeys/startRecordingV1"), QStringLiteral("F9")).toString()),
        hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Start recording"), startHotkey);
    auto* stopHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(
            QStringLiteral("hotkeys/stopRecordingV1"), QStringLiteral("F10")).toString()),
        hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Stop recording"), stopHotkey);
    auto* desktopMuteHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(
            QStringLiteral("hotkeys/muteDesktopV1"), QStringLiteral("Ctrl+Alt+D")).toString()),
        hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Mute/unmute Desktop Audio"), desktopMuteHotkey);
    auto* microphoneMuteHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(
            QStringLiteral("hotkeys/muteMicrophoneV1"), QStringLiteral("Ctrl+Alt+M")).toString()),
        hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Mute/unmute Mic/Aux"), microphoneMuteHotkey);
    auto* nextSceneHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(
            QStringLiteral("hotkeys/nextSceneV1"), QStringLiteral("PageDown")).toString()),
        hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Next scene"), nextSceneHotkey);
    auto* previousSceneHotkey = new QKeySequenceEdit(
        QKeySequence(settingsStore.value(
            QStringLiteral("hotkeys/previousSceneV1"), QStringLiteral("PageUp")).toString()),
        hotkeysPage.first);
    hotkeyForm->addRow(QStringLiteral("Previous scene"), previousSceneHotkey);
    hotkeysPage.second->addLayout(hotkeyForm);
    hotkeysPage.second->addStretch();

    // Advanced
    const auto advancedPage = createPage(
        QStringLiteral("Advanced"),
        QStringLiteral("Diagnostics and local-development information."));
    auto* advancedForm = new QFormLayout();
    advancedForm->addRow(
        QStringLiteral("libobs"),
        new QLabel(engine_->versionString(), advancedPage.first));
    advancedForm->addRow(
        QStringLiteral("Graphics"),
        new QLabel(engine_->graphicsDescription(), advancedPage.first));
    advancedForm->addRow(
        QStringLiteral("Audio"),
        new QLabel(engine_->audioDescription(), advancedPage.first));
    auto* projectPathLabel = new QLabel(Vuttara::AppPaths::projectStatePath(), advancedPage.first);
    projectPathLabel->setWordWrap(true);
    projectPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    advancedForm->addRow(QStringLiteral("Project state"), projectPathLabel);
    advancedPage.second->addLayout(advancedForm);
    auto* advancedButtons = new QHBoxLayout();
    auto* openProjectFolder = new QPushButton(QStringLiteral("Open Project Data Folder"), advancedPage.first);
    auto* openRecordings = new QPushButton(QStringLiteral("Open Recording Folder"), advancedPage.first);
    advancedButtons->addWidget(openProjectFolder);
    advancedButtons->addWidget(openRecordings);
    advancedButtons->addStretch();
    advancedPage.second->addLayout(advancedButtons);
    advancedPage.second->addStretch();

    connect(categories, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    categories->setCurrentRow(std::clamp(initialPage, 0, pages->count() - 1));

    connect(browseFolder, &QPushButton::clicked, &dialog, [&, folderEdit]() {
        const QString selected = QFileDialog::getExistingDirectory(
            &dialog,
            QStringLiteral("Choose Recording Folder"),
            folderEdit->text());
        if (!selected.isEmpty()) {
            folderEdit->setText(QDir::cleanPath(selected));
        }
    });
    connect(openFolder, &QPushButton::clicked, &dialog, [folderEdit]() {
        const QString path = QDir::cleanPath(folderEdit->text());
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(openProjectFolder, &QPushButton::clicked, &dialog, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(Vuttara::AppPaths::projectStatePath()).absolutePath()));
    });
    connect(openRecordings, &QPushButton::clicked, this, &MainWindow::openRecordingFolder);

    const auto updateEstimate = [&]() {
        const QStringList parts = resolutionCombo->currentData().toString().split(QLatin1Char('x'));
        Vuttara::RecordingSettings preview = currentRecording;
        preview.outputDirectory = QDir::cleanPath(folderEdit->text());
        preview.outputWidth = parts.value(0).toInt();
        preview.outputHeight = parts.value(1).toInt();
        preview.framesPerSecond = fpsCombo->currentData().toInt();
        preview.videoBitrateKbps = videoBitrate->value();
        preview.audioBitrateKbps = audioBitrate->value();
        QString summary = QStringLiteral("%1/hour at %2 × %3, %4 FPS")
                              .arg(humanBytes(estimatedRecordingBytes(preview, 3600)))
                              .arg(preview.outputWidth)
                              .arg(preview.outputHeight)
                              .arg(preview.framesPerSecond);
        QStorageInfo storage(preview.outputDirectory);
        storage.refresh();
        if (storage.isValid() && storage.isReady()) {
            summary += QStringLiteral(" • %1 free").arg(humanBytes(
                static_cast<std::uint64_t>(std::max<qint64>(0, storage.bytesAvailable()))));
        }
        storageEstimate->setText(summary);
    };

    bool applyingPreset = false;
    const auto applyPreset = [&](const QString& preset) {
        applyingPreset = true;
        if (preset == QStringLiteral("high")) {
            resolutionCombo->setCurrentIndex(resolutionCombo->findData(QStringLiteral("1920x1080")));
            fpsCombo->setCurrentIndex(fpsCombo->findData(60));
            videoBitrate->setValue(18000);
            audioBitrate->setValue(320);
        } else if (preset == QStringLiteral("space")) {
            resolutionCombo->setCurrentIndex(resolutionCombo->findData(QStringLiteral("1280x720")));
            fpsCombo->setCurrentIndex(fpsCombo->findData(30));
            videoBitrate->setValue(6000);
            audioBitrate->setValue(160);
        } else if (preset == QStringLiteral("balanced")) {
            resolutionCombo->setCurrentIndex(resolutionCombo->findData(QStringLiteral("1920x1080")));
            fpsCombo->setCurrentIndex(fpsCombo->findData(60));
            videoBitrate->setValue(12000);
            audioBitrate->setValue(192);
        }
        applyingPreset = false;
        updateEstimate();
    };

    connect(presetCombo, &QComboBox::currentIndexChanged, &dialog, [&, presetCombo](int) {
        applyPreset(presetCombo->currentData().toString());
    });
    const auto markCustom = [&]() {
        if (!applyingPreset) {
            const int custom = presetCombo->findData(QStringLiteral("custom"));
            if (custom >= 0 && presetCombo->currentIndex() != custom) {
                QSignalBlocker blocker(presetCombo);
                presetCombo->setCurrentIndex(custom);
            }
        }
        updateEstimate();
    };
    connect(resolutionCombo, &QComboBox::currentIndexChanged, &dialog, [&, markCustom](int) { markCustom(); });
    connect(fpsCombo, &QComboBox::currentIndexChanged, &dialog, [&, markCustom](int) { markCustom(); });
    connect(videoBitrate, &QSpinBox::valueChanged, &dialog, [&, markCustom](int) { markCustom(); });
    connect(audioBitrate, &QSpinBox::valueChanged, &dialog, [&, markCustom](int) { markCustom(); });
    connect(folderEdit, &QLineEdit::textChanged, &dialog, [&, updateEstimate](const QString&) { updateEstimate(); });
    updateEstimate();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok |
        QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply |
        QDialogButtonBox::RestoreDefaults,
        &dialog);
    outerLayout->addWidget(buttons);

    QString appliedDesktopDeviceId = currentDesktop.deviceId;
    QString appliedMicrophoneDeviceId = currentMicrophone.deviceId;

    const auto applySettings = [&]() -> bool {
        const QString cleanFolder = QDir::cleanPath(folderEdit->text().trimmed());
        const QString cleanFilename = filenameFormat->text().trimmed();
        if (cleanFolder.isEmpty() || cleanFilename.isEmpty()) {
            QMessageBox::warning(
                &dialog,
                QStringLiteral("Settings"),
                QStringLiteral("Recording folder and filename format cannot be empty."));
            return false;
        }
        if (!QDir().mkpath(cleanFolder)) {
            QMessageBox::warning(
                &dialog,
                QStringLiteral("Settings"),
                QStringLiteral("The recording folder could not be created: %1").arg(cleanFolder));
            return false;
        }

        Vuttara::RecordingSettings updated = currentRecording;
        const QStringList resolution = resolutionCombo->currentData().toString().split(QLatin1Char('x'));
        updated.outputDirectory = cleanFolder;
        updated.outputWidth = resolution.value(0).toInt();
        updated.outputHeight = resolution.value(1).toInt();
        updated.framesPerSecond = fpsCombo->currentData().toInt();
        updated.encoderId = encoderCombo->currentData().toString();
        updated.videoBitrateKbps = videoBitrate->value();
        updated.audioBitrateKbps = audioBitrate->value();
        updated.filenameFormat = cleanFilename;
        updated.automaticRemuxToMp4 = remuxCheck->isChecked() && !ffmpeg.isEmpty();

        Vuttara::StreamingSettings updatedStreaming = currentStreaming;
        updatedStreaming.server = streamServer->currentText().trimmed();
        updatedStreaming.streamKey = streamKey->text();
        updatedStreaming.useAuthentication = streamAuth->isChecked();
        updatedStreaming.username = streamUsername->text().trimmed();
        updatedStreaming.password = streamPassword->text();
        updatedStreaming.encoderId = streamEncoder->currentData().toString();
        const QStringList streamSize = streamResolution->currentData().toString().split(QLatin1Char('x'));
        updatedStreaming.outputWidth = streamSize.value(0).toInt();
        updatedStreaming.outputHeight = streamSize.value(1).toInt();
        updatedStreaming.framesPerSecond = streamFps->currentData().toInt();
        updatedStreaming.videoBitrateKbps = streamVideoBitrate->value();
        updatedStreaming.keyframeIntervalSeconds = streamKeyframe->value();
        updatedStreaming.audioBitrateKbps = streamAudioBitrate->value();
        updatedStreaming.automaticReconnect = streamReconnect->isChecked();
        updatedStreaming.retryDelaySeconds = streamRetryDelay->value();
        updatedStreaming.maximumRetries = streamMaxRetries->value();
        if (!updatedStreaming.server.isEmpty() &&
            !updatedStreaming.server.startsWith(QStringLiteral("rtmp://"), Qt::CaseInsensitive) &&
            !updatedStreaming.server.startsWith(QStringLiteral("rtmps://"), Qt::CaseInsensitive)) {
            QMessageBox::warning(&dialog, QStringLiteral("Streaming Settings"), QStringLiteral("The streaming server must begin with rtmp:// or rtmps://."));
            return false;
        }
        const auto applyAudioDevice = [this](
                                          Vuttara::AudioChannelKind kind,
                                          const QString& deviceId,
                                          const QVector<Vuttara::AudioDeviceInfo>& devices) {
            if (deviceId.isEmpty()) {
                return engine_->disconnectAudioDevice(kind);
            }
            const auto device = std::find_if(
                devices.cbegin(),
                devices.cend(),
                [&deviceId](const Vuttara::AudioDeviceInfo& candidate) {
                    return candidate.deviceId == deviceId;
                });
            if (device == devices.cend()) {
                return false;
            }
            return engine_->setAudioDevice(kind, *device);
        };

        const QString requestedDesktopDeviceId = desktopCombo->currentData().toString();
        const QString requestedMicrophoneDeviceId = microphoneCombo->currentData().toString();

        if (!applyAudioDevice(
                Vuttara::AudioChannelKind::Desktop,
                requestedDesktopDeviceId,
                desktopDevices)) {
            const QString error = engine_->lastError();
            applyAudioDevice(
                Vuttara::AudioChannelKind::Desktop,
                appliedDesktopDeviceId,
                desktopDevices);
            QMessageBox::critical(&dialog, QStringLiteral("Audio Settings"), error);
            return false;
        }
        if (!applyAudioDevice(
                Vuttara::AudioChannelKind::Microphone,
                requestedMicrophoneDeviceId,
                microphoneDevices)) {
            const QString error = engine_->lastError();
            applyAudioDevice(
                Vuttara::AudioChannelKind::Desktop,
                appliedDesktopDeviceId,
                desktopDevices);
            applyAudioDevice(
                Vuttara::AudioChannelKind::Microphone,
                appliedMicrophoneDeviceId,
                microphoneDevices);
            QMessageBox::critical(&dialog, QStringLiteral("Audio Settings"), error);
            return false;
        }

        appliedDesktopDeviceId = requestedDesktopDeviceId;
        appliedMicrophoneDeviceId = requestedMicrophoneDeviceId;

        if (!saveStreamingSettings(updatedStreaming)) {
            QMessageBox::critical(&dialog, QStringLiteral("Streaming Settings"), QStringLiteral("The streaming settings could not be saved."));
            return false;
        }
        saveRecordingSettings(updated);
        settingsStore.setValue(
            QStringLiteral("streaming/serviceIdV1"),
            streamService->currentData().toString());
        settingsStore.setValue(QStringLiteral("recording/outputDirectoryV1"), cleanFolder);
        settingsStore.setValue(QStringLiteral("recording/qualityPresetV1"), presetCombo->currentData().toString());
        settingsStore.setValue(QStringLiteral("general/confirmSceneRemovalV1"), confirmSceneRemoval->isChecked());
        settingsStore.setValue(QStringLiteral("general/confirmSourceRemovalV1"), confirmSourceRemoval->isChecked());
        settingsStore.setValue(QStringLiteral("general/confirmRecordingCloseV1"), confirmRecordingClose->isChecked());
        settingsStore.setValue(QStringLiteral("general/confirmStreamingCloseV1"), confirmStreamingClose->isChecked());
        settingsStore.setValue(QStringLiteral("hotkeys/startStreamingV1"), startStreamingHotkey->keySequence().toString());
        settingsStore.setValue(QStringLiteral("hotkeys/stopStreamingV1"), stopStreamingHotkey->keySequence().toString());
        settingsStore.setValue(QStringLiteral("hotkeys/startRecordingV1"), startHotkey->keySequence().toString());
        settingsStore.setValue(QStringLiteral("hotkeys/stopRecordingV1"), stopHotkey->keySequence().toString());
        settingsStore.setValue(QStringLiteral("hotkeys/muteDesktopV1"), desktopMuteHotkey->keySequence().toString());
        settingsStore.setValue(QStringLiteral("hotkeys/muteMicrophoneV1"), microphoneMuteHotkey->keySequence().toString());
        settingsStore.setValue(QStringLiteral("hotkeys/nextSceneV1"), nextSceneHotkey->keySequence().toString());
        settingsStore.setValue(QStringLiteral("hotkeys/previousSceneV1"), previousSceneHotkey->keySequence().toString());
        settingsStore.sync();

        refreshHotkeys();
        refreshAudioDeviceLists();
        updateAudioControls();
        updateStreamingControls();
        updateRecordingControls();
        saveProjectState();
        statusBar()->showMessage(QStringLiteral("Settings applied."), 4000);
        return true;
    };

    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&]() {
        applySettings();
    });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&]() {
        confirmSceneRemoval->setChecked(true);
        confirmSourceRemoval->setChecked(true);
        confirmRecordingClose->setChecked(true);
        confirmStreamingClose->setChecked(true);
        resolutionCombo->setCurrentIndex(resolutionCombo->findData(QStringLiteral("1920x1080")));
        fpsCombo->setCurrentIndex(fpsCombo->findData(60));
        desktopCombo->setCurrentIndex(0);
        microphoneCombo->setCurrentIndex(0);
        presetCombo->setCurrentIndex(presetCombo->findData(QStringLiteral("balanced")));
        encoderCombo->setCurrentIndex(0);
        videoBitrate->setValue(12000);
        audioBitrate->setValue(192);
        filenameFormat->setText(QStringLiteral("VuttaraStudio_{date}_{time}"));
        remuxCheck->setChecked(false);
        streamService->setCurrentIndex(
            streamService->findData(QStringLiteral("custom")));
        streamServer->setEditText(QString());
        streamKey->clear();
        streamAuth->setChecked(false);
        streamUsername->clear();
        streamPassword->clear();
        streamEncoder->setCurrentIndex(0);
        streamResolution->setCurrentIndex(
            streamResolution->findData(QStringLiteral("1920x1080")));
        streamFps->setCurrentIndex(streamFps->findData(60));
        streamVideoBitrate->setValue(6000);
        streamKeyframe->setValue(2);
        streamAudioBitrate->setValue(160);
        streamReconnect->setChecked(true);
        streamRetryDelay->setValue(2);
        streamMaxRetries->setValue(20);
        startStreamingHotkey->setKeySequence(QKeySequence(QStringLiteral("F7")));
        stopStreamingHotkey->setKeySequence(QKeySequence(QStringLiteral("F8")));
        startHotkey->setKeySequence(QKeySequence(QStringLiteral("F9")));
        stopHotkey->setKeySequence(QKeySequence(QStringLiteral("F10")));
        desktopMuteHotkey->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+D")));
        microphoneMuteHotkey->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+M")));
        nextSceneHotkey->setKeySequence(QKeySequence(QStringLiteral("PageDown")));
        previousSceneHotkey->setKeySequence(QKeySequence(QStringLiteral("PageUp")));
        updateEstimate();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (applySettings()) {
            dialog.accept();
        }
    });

    dialog.exec();
}

QString MainWindow::findFfmpegExecutable() const
{
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(applicationDirectory).filePath(QStringLiteral("ffmpeg.exe")),
        QDir(applicationDirectory).filePath(QStringLiteral("obs/bin/64bit/ffmpeg.exe")),
        QStandardPaths::findExecutable(QStringLiteral("ffmpeg.exe")),
    };

    for (const QString& candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo(candidate).isFile()) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

void MainWindow::maybeStartAutomaticRemux(
    const Vuttara::RecordingInfo& info)
{
    if (
        info.outputPath.isEmpty() ||
        info.outputPath == lastHandledRecordingPath_ ||
        info.state == Vuttara::RecordingState::Recording ||
        info.state == Vuttara::RecordingState::Stopping) {
        return;
    }

    lastHandledRecordingPath_ = info.outputPath;
    recordingDiagnostics_ = info.diagnostics;
    if (
        info.state == Vuttara::RecordingState::Ready &&
        remuxRequestedForCurrentRecording_) {
        startAutomaticRemux(info.outputPath);
    }
}

void MainWindow::startAutomaticRemux(const QString& mkvPath)
{
    if (remuxProcess_ != nullptr) {
        return;
    }

    const QString ffmpeg = findFfmpegExecutable();
    if (ffmpeg.isEmpty()) {
        recordingStatusLabel_->setText(
            QStringLiteral("MKV ready — MP4 remux tool unavailable"));
        recordingStatusLabel_->setToolTip(
            QStringLiteral("The bundled FFmpeg remux tool is unavailable. Reinstall the validated local runtime."));
        return;
    }

    const QFileInfo inputInfo(mkvPath);
    if (!inputInfo.isFile() || inputInfo.size() <= 0) {
        recordingStatusLabel_->setText(
            QStringLiteral("Remux skipped — MKV is missing"));
        return;
    }

    QString outputPath = QDir(inputInfo.absolutePath()).filePath(
        inputInfo.completeBaseName() + QStringLiteral(".mp4"));
    int suffix = 2;
    while (QFileInfo::exists(outputPath)) {
        outputPath = QDir(inputInfo.absolutePath()).filePath(
            QStringLiteral("%1_%2.mp4")
                .arg(inputInfo.completeBaseName())
                .arg(suffix++));
    }

    remuxInputPath_ = mkvPath;
    remuxOutputPath_ = outputPath;
    remuxPartialPath_ = QDir(inputInfo.absolutePath()).filePath(
        QFileInfo(outputPath).completeBaseName() + QStringLiteral(".remuxing.mp4"));
    QFile::remove(remuxPartialPath_);

    remuxProcess_ = new QProcess(this);
    remuxProcess_->setProgram(ffmpeg);
    remuxProcess_->setArguments({
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-i"),
        QDir::toNativeSeparators(remuxInputPath_),
        QStringLiteral("-map"),
        QStringLiteral("0"),
        QStringLiteral("-c"),
        QStringLiteral("copy"),
        QStringLiteral("-movflags"),
        QStringLiteral("+faststart"),
        QDir::toNativeSeparators(remuxPartialPath_),
    });

    connect(
        remuxProcess_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            finishAutomaticRemux(exitCode, static_cast<int>(exitStatus));
        });

    recordingStatusLabel_->setText(QStringLiteral("Remuxing MKV to MP4…"));
    recordingStatusLabel_->setToolTip(
        QStringLiteral("Original MKV: %1").arg(remuxInputPath_));
    remuxProcess_->start();
    if (!remuxProcess_->waitForStarted(3000)) {
        finishAutomaticRemux(-1, static_cast<int>(QProcess::CrashExit));
    }
}

void MainWindow::finishAutomaticRemux(int exitCode, int exitStatus)
{
    if (remuxProcess_ == nullptr) {
        return;
    }

    const QString stderrText =
        QString::fromUtf8(remuxProcess_->readAllStandardError()).trimmed();
    remuxProcess_->deleteLater();
    remuxProcess_ = nullptr;

    const bool processSucceeded =
        exitCode == 0 &&
        exitStatus == static_cast<int>(QProcess::NormalExit) &&
        QFileInfo(remuxPartialPath_).isFile() &&
        QFileInfo(remuxPartialPath_).size() > 0;

    if (processSucceeded) {
        QFile::remove(remuxOutputPath_);
        if (QFile::rename(remuxPartialPath_, remuxOutputPath_)) {
            lastRemuxedPath_ = remuxOutputPath_;
            recordingStatusLabel_->setText(QStringLiteral("Ready — MP4 copy created"));
            recordingStatusLabel_->setToolTip(QStringLiteral(
                "MKV retained: %1\nMP4 created: %2")
                .arg(remuxInputPath_, remuxOutputPath_));
            statusBar()->showMessage(
                QStringLiteral("MP4 remux completed: %1")
                    .arg(remuxOutputPath_),
                9000);
        } else {
            QFile::remove(remuxPartialPath_);
            recordingStatusLabel_->setText(
                QStringLiteral("MKV ready — MP4 rename failed"));
        }
    } else {
        QFile::remove(remuxPartialPath_);
        const QString detail = stderrText.isEmpty()
            ? QStringLiteral("ffmpeg exited with code %1").arg(exitCode)
            : stderrText;
        recordingStatusLabel_->setText(
            QStringLiteral("MKV ready — MP4 remux failed"));
        recordingStatusLabel_->setToolTip(detail);
        recordingDiagnostics_ += QStringLiteral(" | remux: %1").arg(detail);
    }

    refreshRecordingEstimate();
    updateRecordingControls();
}

QString MainWindow::recordingOutputDirectory() const
{
    QSettings settings;
    QString path = settings.value(
        QStringLiteral("recording/outputDirectoryV1"))
                       .toString();
    if (path.trimmed().isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (path.trimmed().isEmpty()) {
            path = QDir::homePath();
        }
        path = QDir(path).filePath(QStringLiteral("Vuttara Studio"));
    }
    return QDir::cleanPath(path);
}

void MainWindow::chooseRecordingFolder()
{
    const QString selected = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Choose Recording Folder"),
        recordingOutputDirectory());
    if (selected.isEmpty()) {
        return;
    }

    const QString cleanPath = QDir::cleanPath(selected);
    QSettings settings;
    settings.setValue(QStringLiteral("recording/outputDirectoryV1"), cleanPath);
    settings.sync();
    refreshRecordingEstimate();
    updateRecordingControls();
}

void MainWindow::openRecordingFolder()
{
    const QString path = recordingOutputDirectory();
    QDir().mkpath(path);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(
            this,
            QStringLiteral("Open Recording Folder"),
            QStringLiteral("Windows could not open: %1").arg(path));
    }
}

void MainWindow::startStreaming()
{
    if (previewInteractionOverlay_ != nullptr) {
        static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)->cancelActiveInteraction();
    }
    if (engine_ == nullptr || engine_->isStreaming()) return;
    const Vuttara::StreamingSettings settings = streamingSettings();
    if (settings.server.isEmpty() || settings.streamKey.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Start Streaming"), QStringLiteral("Open Settings > Streaming and choose a service, server, and stream key first."));
        return;
    }
    if (!engine_->startStreaming(settings)) {
        QMessageBox::critical(this, QStringLiteral("Start Streaming"), engine_->lastError());
    } else {
        statusBar()->showMessage(QStringLiteral("Streaming connection started."), 7000);
        if (streamingTimer_ != nullptr) streamingTimer_->start();
    }
    updateStreamingControls();
}

void MainWindow::stopStreaming()
{
    if (engine_ == nullptr || !engine_->isStreaming()) return;
    if (!engine_->stopStreaming()) {
        QMessageBox::warning(this, QStringLiteral("Stop Streaming"), engine_->lastError());
    }
    updateStreamingControls();
}

void MainWindow::updateStreamingControls()
{
    if (engine_ == nullptr || streamingStatusLabel_ == nullptr) return;
    const Vuttara::StreamingInfo info = engine_->streamingInfo();
    const qint64 totalSeconds = qMax<qint64>(0, info.elapsedMilliseconds / 1000);
    streamingElapsedLabel_->setText(QStringLiteral("%1:%2:%3")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0')));
    streamingBitrateLabel_->setText(QStringLiteral("%1 Kbps").arg(info.currentBitrateKbps, 0, 'f', 0));
    streamingDroppedLabel_->setText(QStringLiteral("%1 / %2").arg(info.droppedFrames).arg(info.totalFrames));
    QString status;
    switch (info.state) {
    case Vuttara::StreamingState::Connecting: status = QStringLiteral("Connecting…"); break;
    case Vuttara::StreamingState::Streaming: status = QStringLiteral("Live"); break;
    case Vuttara::StreamingState::Reconnecting: status = QStringLiteral("Reconnecting…"); break;
    case Vuttara::StreamingState::Stopping: status = QStringLiteral("Stopping…"); break;
    case Vuttara::StreamingState::Error: status = QStringLiteral("Error: %1").arg(info.error); break;
    default: status = info.totalBytes > 0 ? QStringLiteral("Ready — last stream ended") : QStringLiteral("Ready"); break;
    }
    streamingStatusLabel_->setText(status);
    streamingStatusLabel_->setToolTip(info.diagnostics + (info.server.isEmpty() ? QString{} : QStringLiteral("\nServer: %1").arg(info.server)));
    startStreamingButton_->setEnabled(engine_->isReady() && !engine_->isStreaming());
    stopStreamingButton_->setEnabled(engine_->isStreaming() && !info.stopping);
    const bool needsRefresh = engine_->isStreaming();
    if (streamingTimer_ != nullptr) {
        if (needsRefresh && !streamingTimer_->isActive()) streamingTimer_->start();
        if (!needsRefresh && streamingTimer_->isActive()) streamingTimer_->stop();
    }
    refreshPreviewInteractionOverlay();
}

void MainWindow::startRecording()
{
    if (previewInteractionOverlay_ != nullptr) {
        static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)->cancelActiveInteraction();
    }

    if (engine_ == nullptr) {
        return;
    }

    Vuttara::RecordingSettings settings = recordingSettings();
    settings.outputDirectory = recordingOutputDirectory();

    QStorageInfo storage(settings.outputDirectory);
    storage.refresh();
    const std::uint64_t freeBytes =
        storage.isValid() && storage.isReady()
        ? static_cast<std::uint64_t>(
            std::max<qint64>(0, storage.bytesAvailable()))
        : 0ULL;
    const std::uint64_t oneHourEstimate =
        estimatedRecordingBytes(settings, 3600);

    if (storage.isValid() && storage.isReady() && freeBytes < 256ULL * 1024ULL * 1024ULL) {
        QMessageBox::critical(
            this,
            QStringLiteral("Start Recording"),
            QStringLiteral(
                "The recording drive has less than 256 MB available. Free space before recording."));
        return;
    }

    if (
        storage.isValid() &&
        storage.isReady() &&
        freeBytes < oneHourEstimate) {
        const auto answer = QMessageBox::warning(
            this,
            QStringLiteral("Low Recording Space"),
            QStringLiteral(
                "The current settings estimate %1 per hour, but the drive has %2 free. Start anyway?")
                .arg(humanBytes(oneHourEstimate), humanBytes(freeBytes)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    remuxRequestedForCurrentRecording_ =
        settings.automaticRemuxToMp4 &&
        !findFfmpegExecutable().isEmpty();
    lastHandledRecordingPath_.clear();
    lastRemuxedPath_.clear();
    recordingDiagnostics_.clear();

    if (!engine_->startRecording(settings)) {
        QMessageBox::critical(
            this,
            QStringLiteral("Start Recording"),
            engine_->lastError());
    } else {
        const Vuttara::RecordingInfo info = engine_->recordingInfo();
        recordingDiagnostics_ = info.diagnostics;
        statusBar()->showMessage(
            QStringLiteral("Recording started: %1").arg(info.outputPath),
            7000);

        if (recordingTimer_ != nullptr) {
            recordingTimer_->start();
        }
    }
    updateRecordingControls();
}
void MainWindow::stopRecording()
{
    if (engine_ == nullptr || !engine_->isRecording()) {
        return;
    }

    if (!engine_->stopRecording()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Stop Recording"),
            engine_->lastError());
    }
    updateRecordingControls();
}

void MainWindow::updateRecordingControls()
{
    if (engine_ == nullptr || recordingStatusLabel_ == nullptr) {
        return;
    }

    const Vuttara::RecordingInfo info = engine_->recordingInfo();
    maybeStartAutomaticRemux(info);

    const qint64 totalSeconds = qMax<qint64>(0, info.elapsedMilliseconds / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;

    const auto setLabelText = [](QLabel* label, const QString& text) {
        if (label != nullptr && label->text() != text) {
            label->setText(text);
        }
    };

    setLabelText(
        recordingElapsedLabel_,
        QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));

    setLabelText(
        recordingSizeLabel_,
        QStringLiteral("%1 MB")
            .arg(
                static_cast<double>(info.totalBytes) / (1024.0 * 1024.0),
                0,
                'f',
                1));

    QString encoderText = info.encoderName.isEmpty()
        ? QStringLiteral("Automatic")
        : info.encoderName;
    if (!info.encoderName.isEmpty()) {
        encoderText += QStringLiteral(" • %1x%2/%3")
            .arg(info.outputWidth)
            .arg(info.outputHeight)
            .arg(info.framesPerSecond);
    }
    setLabelText(recordingEncoderLabel_, encoderText);

    QString status;
    if (remuxProcess_ != nullptr) {
        status = QStringLiteral("Remuxing MKV to MP4…");
    } else {
        switch (info.state) {
        case Vuttara::RecordingState::Recording:
            status = QStringLiteral("Recording");
            break;
        case Vuttara::RecordingState::Stopping:
            status = QStringLiteral("Finalizing…");
            break;
        case Vuttara::RecordingState::Error:
            status = QStringLiteral("Error: %1").arg(info.error);
            break;
        default:
            if (!lastRemuxedPath_.isEmpty()) {
                status = QStringLiteral("Ready — MP4 copy created");
            } else {
                status = info.outputPath.isEmpty()
                    ? QStringLiteral("Ready")
                    : QStringLiteral("Ready — last recording finalized");
            }
            break;
        }
    }
    setLabelText(recordingStatusLabel_, status);

    const QString shownPath = !lastRemuxedPath_.isEmpty()
        ? lastRemuxedPath_
        : info.outputPath;
    QString tooltip = !recordingDiagnostics_.isEmpty()
        ? recordingDiagnostics_
        : info.diagnostics;
    if (!shownPath.isEmpty()) {
        if (!tooltip.isEmpty()) {
            tooltip += QLatin1Char('\n');
        }
        tooltip += shownPath;
    }
    recordingStatusLabel_->setToolTip(tooltip);

    const bool busy = engine_->isRecording() || remuxProcess_ != nullptr;
    startRecordingButton_->setEnabled(engine_->isReady() && !busy);
    stopRecordingButton_->setEnabled(info.state == Vuttara::RecordingState::Recording);

    const bool needsLiveRefresh =
        info.state == Vuttara::RecordingState::Recording ||
        info.state == Vuttara::RecordingState::Stopping;
    if (recordingTimer_ != nullptr) {
        if (needsLiveRefresh && !recordingTimer_->isActive()) {
            recordingTimer_->start();
        } else if (!needsLiveRefresh && recordingTimer_->isActive()) {
            recordingTimer_->stop();
        }
    }

    if (!needsLiveRefresh && remuxProcess_ == nullptr) {
        maybeStartAutomaticRemux(info);
    }

    refreshPreviewInteractionOverlay();
}

void MainWindow::refreshAudioDeviceLists()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const Vuttara::AudioChannelInfo desktop = engine_->audioChannelInfo(Vuttara::AudioChannelKind::Desktop);
    const Vuttara::AudioChannelInfo microphone = engine_->audioChannelInfo(Vuttara::AudioChannelKind::Microphone);

    auto populate = [](QComboBox* combo, const QVector<Vuttara::AudioDeviceInfo>& devices, const QString& selectedId) {
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(QStringLiteral("Disabled"), QString{});
        for (const Vuttara::AudioDeviceInfo& device : devices) {
            combo->addItem(device.name, device.deviceId);
        }

        int selectedIndex = combo->findData(selectedId);
        if (!selectedId.isEmpty() && selectedIndex < 0) {
            combo->addItem(QStringLiteral("Unavailable saved device"), selectedId);
            selectedIndex = combo->count() - 1;
        }
        combo->setCurrentIndex(std::max(selectedIndex, 0));
    };

    populate(desktopDeviceCombo_, engine_->availableDesktopAudioDevices(), desktop.deviceId);
    populate(microphoneDeviceCombo_, engine_->availableMicrophoneDevices(), microphone.deviceId);
    updateAudioControls();
    statusBar()->showMessage(QStringLiteral("Windows audio device lists refreshed."), 4000);
}

void MainWindow::updateAudioControls()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    auto update = [](const Vuttara::AudioChannelInfo& info,
                     QLabel* state,
                     QSlider* volume,
                     QLabel* volumeValue,
                     QPushButton* muteButton) {
        const QSignalBlocker blocker(volume);
        volume->setValue(info.volumePercent);
        volume->setEnabled(info.connected);
        volumeValue->setText(QStringLiteral("%1%").arg(info.volumePercent));
        muteButton->setEnabled(info.connected);
        muteButton->setText(info.muted ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
        state->setText(info.connected
                ? QStringLiteral("Connected — %1%2")
                      .arg(info.deviceName, info.muted ? QStringLiteral(" — Muted") : QString{})
                : QStringLiteral("Disabled — choose a device"));
    };

    update(
        engine_->audioChannelInfo(Vuttara::AudioChannelKind::Desktop),
        desktopAudioState_, desktopVolume_, desktopVolumeValue_, desktopMuteButton_);
    update(
        engine_->audioChannelInfo(Vuttara::AudioChannelKind::Microphone),
        microphoneAudioState_, microphoneVolume_, microphoneVolumeValue_, microphoneMuteButton_);
}

void MainWindow::updateAudioMeters()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    auto update = [](const Vuttara::AudioChannelInfo& info, QProgressBar* meter, QLabel* label) {
        const float db = info.connected && !info.muted ? info.peakDb : -std::numeric_limits<float>::infinity();
        if (!std::isfinite(db) || db <= -60.0F) {
            meter->setValue(0);
            label->setText(QStringLiteral("−∞ dB"));
            return;
        }

        const int value = std::clamp(static_cast<int>(std::lround((db + 60.0F) / 60.0F * 100.0F)), 0, 100);
        meter->setValue(value);
        label->setText(QStringLiteral("%1 dB").arg(db, 0, 'f', 1));
    };

    update(engine_->audioChannelInfo(Vuttara::AudioChannelKind::Desktop), desktopAudioMeter_, desktopAudioLevel_);
    update(engine_->audioChannelInfo(Vuttara::AudioChannelKind::Microphone), microphoneAudioMeter_, microphoneAudioLevel_);
}

void MainWindow::applyAudioDevice(Vuttara::AudioChannelKind kind, QComboBox* combo)
{
    if (engine_ == nullptr || !engine_->isReady() || combo == nullptr) {
        return;
    }

    const QString deviceId = combo->currentData().toString();
    bool succeeded = false;
    if (deviceId.isEmpty()) {
        succeeded = engine_->disconnectAudioDevice(kind);
    } else {
        Vuttara::AudioDeviceInfo device;
        device.deviceId = deviceId;
        device.name = combo->currentText();
        device.defaultDevice = deviceId == QStringLiteral("default");
        succeeded = engine_->setAudioDevice(kind, device);
    }

    if (!succeeded) {
        QMessageBox::warning(this, QStringLiteral("Audio Device"), engine_->lastError());
        refreshAudioDeviceLists();
        return;
    }

    saveProjectState();
    updateAudioControls();
    updateAudioMeters();
}

void MainWindow::applyAudioVolume(Vuttara::AudioChannelKind kind, int volumePercent)
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    engine_->setAudioVolume(kind, volumePercent);
    if (kind == Vuttara::AudioChannelKind::Desktop) {
        desktopVolumeValue_->setText(QStringLiteral("%1%").arg(volumePercent));
    } else {
        microphoneVolumeValue_->setText(QStringLiteral("%1%").arg(volumePercent));
    }
}

void MainWindow::toggleAudioMute(Vuttara::AudioChannelKind kind)
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const Vuttara::AudioChannelInfo info = engine_->audioChannelInfo(kind);
    if (!engine_->setAudioMuted(kind, !info.muted)) {
        QMessageBox::warning(this, QStringLiteral("Audio Mute"), engine_->lastError());
        return;
    }

    saveProjectState();
    updateAudioControls();
    updateAudioMeters();
}

void MainWindow::showAboutDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("About Vuttara Studio"));
    dialog.setMinimumWidth(520);

    auto* layout = new QVBoxLayout(&dialog);
    auto* logo = new QLabel(&dialog);
    QPixmap logoPixmap(QStringLiteral(":/branding/vuttara-studio-wordmark.png"));
    logo->setPixmap(logoPixmap.scaledToWidth(340, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    const QString engineVersion = engine_ != nullptr && engine_->isReady()
        ? engine_->versionString()
        : QStringLiteral("Unavailable");
#ifdef VUTTARA_STUDIO_RELEASE_BUILD
    const QString distributionDescription = QStringLiteral(
        "Windows x64 — stable release with verified automatic updates");
#else
    const QString distributionDescription = QStringLiteral(
        "Windows x64 — local development build");
#endif
    auto* text = new QLabel(
        QStringLiteral(
            "Version %1\nVuttara Studio\n"
            "Scenes, source transforms, Desktop Audio, Mic/Aux, recording, and streaming\n"
            "Display and Window Capture through libobs %2\n%3")
            .arg(QStringLiteral(VUTTARA_STUDIO_VERSION))
            .arg(engineVersion)
            .arg(distributionDescription),
        &dialog);
    text->setAlignment(Qt::AlignCenter);
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::addScene()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    bool accepted = false;
    const QString requested = QInputDialog::getText(
        this,
        QStringLiteral("Add Scene"),
        QStringLiteral("Scene name:"),
        QLineEdit::Normal,
        QStringLiteral("Scene"),
        &accepted);
    if (!accepted) {
        return;
    }

    QString createdName;
    if (!engine_->addScene(requested, &createdName)) {
        QMessageBox::critical(this, QStringLiteral("Add Scene"), engine_->lastError());
        return;
    }

    saveProjectState();
    refreshSceneList();
    refreshSourceList();
    updatePreviewInformation();
    statusBar()->showMessage(QStringLiteral("%1 created and activated.").arg(createdName), 5000);
}

void MainWindow::removeSelectedScene()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QString sceneName = selectedSceneName();
    if (sceneName.isEmpty()) {
        return;
    }

    QSettings settings;
    if (
        settings.value(QStringLiteral("general/confirmSceneRemovalV1"), true).toBool() &&
        QMessageBox::question(
            this,
            QStringLiteral("Remove Scene"),
            QStringLiteral("Remove %1 and every source it contains?").arg(sceneName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    if (!engine_->removeScene(sceneName)) {
        QMessageBox::warning(this, QStringLiteral("Remove Scene"), engine_->lastError());
        return;
    }

    sourceGroupsByScene_.remove(sceneName);
    saveProjectState();
    refreshSceneList();
    refreshSourceGroupTabs();
    refreshSourceList();
    updatePreviewInformation();
}

void MainWindow::handleSceneSelectionChanged(QListWidgetItem* current, QListWidgetItem*)
{
    if (previewInteractionOverlay_ != nullptr) {
        static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)->cancelActiveInteraction();
    }
    if (current == nullptr || engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QString sceneName = current->data(NameRole).toString();
    if (sceneName.isEmpty() || sceneName == engine_->activeSceneName()) {
        return;
    }

    if (!engine_->switchScene(sceneName)) {
        QMessageBox::warning(this, QStringLiteral("Switch Scene"), engine_->lastError());
        refreshSceneList();
        return;
    }

    saveProjectState();
    refreshSourceGroupTabs();
    refreshSourceList();
    updatePreviewInformation();
    statusBar()->showMessage(QStringLiteral("Active scene: %1").arg(sceneName), 4000);
    refreshPreviewInteractionOverlay();
}

void MainWindow::refreshSceneList()
{
    const QSignalBlocker blocker(scenesList_);
    scenesList_->clear();

    if (engine_ == nullptr || !engine_->isReady()) {
        removeSceneButton_->setEnabled(false);
        return;
    }

    for (const Vuttara::SceneInfo& scene : engine_->sceneInfos()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  (%2 source%3)")
                .arg(scene.name)
                .arg(scene.sourceCount)
                .arg(scene.sourceCount == 1 ? QString{} : QStringLiteral("s")),
            scenesList_);
        item->setData(NameRole, scene.name);
        item->setData(RemovableRole, scene.removable);
        if (scene.active) {
            scenesList_->setCurrentItem(item);
        }
    }

    const QListWidgetItem* current = scenesList_->currentItem();
    removeSceneButton_->setEnabled(
        current != nullptr && current->data(RemovableRole).toBool());
}

void MainWindow::showAddDisplayCaptureDialog()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        QMessageBox::warning(this, QStringLiteral("Display Capture"), QStringLiteral("The media engine is not ready."));
        return;
    }

    if (engine_->hasDisplayCapture()) {
        QMessageBox::information(
            this,
            QStringLiteral("Display Capture"),
            QStringLiteral("Remove the existing Display Capture source from this scene before selecting another monitor."));
        return;
    }

    const QVector<Vuttara::DisplayInfo> displays = engine_->availableDisplays();
    if (displays.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Display Capture"),
            QStringLiteral("Windows did not report any capturable displays."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Display Capture"));
    dialog.setMinimumWidth(560);

    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(
        QStringLiteral(
            "Choose a connected Windows display. The source is added to %1 and fitted inside the 1920 × 1080 canvas.")
            .arg(engine_->activeSceneName()),
        &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout();
    auto* displayCombo = new QComboBox(&dialog);
    for (const Vuttara::DisplayInfo& display : displays) {
        displayCombo->addItem(display.description, display.monitorId);
    }
    form->addRow(QStringLiteral("Display:"), displayCombo);

    auto* sourceName = new QLineEdit(QStringLiteral("Display Capture"), &dialog);
    sourceName->setMaxLength(80);
    form->addRow(QStringLiteral("Source name:"), sourceName);

    auto* captureCursor = new QCheckBox(QStringLiteral("Capture mouse cursor"), &dialog);
    captureCursor->setChecked(true);
    form->addRow(QString{}, captureCursor);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add Source"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString selectedMonitorId = displayCombo->currentData().toString();
    const auto selectedDisplay = std::find_if(
        displays.cbegin(),
        displays.cend(),
        [&selectedMonitorId](const Vuttara::DisplayInfo& display) {
            return display.monitorId == selectedMonitorId;
        });
    if (selectedDisplay == displays.cend()) {
        QMessageBox::warning(this, QStringLiteral("Display Capture"), QStringLiteral("The selected display is no longer available."));
        return;
    }

    QString createdName;
    if (!engine_->addDisplayCapture(
            *selectedDisplay,
            captureCursor->isChecked(),
            sourceName->text(),
            &createdName)) {
        QMessageBox::critical(this, QStringLiteral("Display Capture"), engine_->lastError());
        return;
    }

    addSourceToCurrentGroup(createdName);
    saveProjectState();
    refreshSceneList();
    refreshSourceList(createdName);
    updatePreviewInformation();
    statusBar()->showMessage(QStringLiteral("%1 added to %2.").arg(createdName, engine_->activeSceneName()), 6000);
}

void MainWindow::showAddWindowCaptureDialog()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        QMessageBox::warning(this, QStringLiteral("Window Capture"), QStringLiteral("The media engine is not ready."));
        return;
    }

    if (engine_->hasWindowCapture()) {
        QMessageBox::information(
            this,
            QStringLiteral("Window Capture"),
            QStringLiteral("Remove the existing Window Capture source from this scene before selecting another window."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Window Capture"));
    dialog.setMinimumWidth(720);

    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(
        QStringLiteral(
            "Choose a currently open application window for %1. Refresh updates the list without closing this dialog.")
            .arg(engine_->activeSceneName()),
        &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout();
    auto* selectorContainer = new QWidget(&dialog);
    auto* selectorLayout = new QHBoxLayout(selectorContainer);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    auto* windowCombo = new QComboBox(selectorContainer);
    auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), selectorContainer);
    selectorLayout->addWidget(windowCombo, 1);
    selectorLayout->addWidget(refreshButton);
    form->addRow(QStringLiteral("Window:"), selectorContainer);

    auto* sourceName = new QLineEdit(QStringLiteral("Window Capture"), &dialog);
    sourceName->setMaxLength(80);
    form->addRow(QStringLiteral("Source name:"), sourceName);

    auto* methodCombo = new QComboBox(&dialog);
    methodCombo->addItems({
        QStringLiteral("Automatic"),
        QStringLiteral("BitBlt"),
        QStringLiteral("Windows Graphics Capture"),
    });
    form->addRow(QStringLiteral("Capture method:"), methodCombo);

    auto* priorityCombo = new QComboBox(&dialog);
    priorityCombo->addItems({
        QStringLiteral("Match window class"),
        QStringLiteral("Match window title"),
        QStringLiteral("Match executable"),
    });
    priorityCombo->setCurrentIndex(1);
    form->addRow(QStringLiteral("Match priority:"), priorityCombo);

    auto* captureCursor = new QCheckBox(QStringLiteral("Capture mouse cursor"), &dialog);
    captureCursor->setChecked(true);
    form->addRow(QString{}, captureCursor);

    auto* clientArea = new QCheckBox(QStringLiteral("Capture client area only"), &dialog);
    clientArea->setChecked(true);
    form->addRow(QString{}, clientArea);
    layout->addLayout(form);

    QVector<Vuttara::WindowInfo> windows;
    const auto refreshWindows = [&]() {
        const QString previous = windowCombo->currentData().toString();
        windows = engine_->availableWindows();
        windowCombo->clear();
        for (const Vuttara::WindowInfo& window : windows) {
            windowCombo->addItem(window.description, window.encodedValue);
        }
        const int previousIndex = windowCombo->findData(previous);
        if (previousIndex >= 0) {
            windowCombo->setCurrentIndex(previousIndex);
        }
    };
    connect(refreshButton, &QPushButton::clicked, &dialog, refreshWindows);
    refreshWindows();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add Source"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (windows.isEmpty()) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        QMessageBox::information(
            &dialog,
            QStringLiteral("Window Capture"),
            QStringLiteral("No capturable application windows are currently open. Open a window and select Refresh."));
    }

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString selectedEncodedValue = windowCombo->currentData().toString();
    const auto selectedWindow = std::find_if(
        windows.cbegin(),
        windows.cend(),
        [&selectedEncodedValue](const Vuttara::WindowInfo& window) {
            return window.encodedValue == selectedEncodedValue;
        });
    if (selectedWindow == windows.cend()) {
        QMessageBox::warning(this, QStringLiteral("Window Capture"), QStringLiteral("The selected window is no longer available. Refresh and try again."));
        return;
    }

    QString createdName;
    if (!engine_->addWindowCapture(
            *selectedWindow,
            windowMethodFromIndex(methodCombo->currentIndex()),
            windowPriorityFromIndex(priorityCombo->currentIndex()),
            captureCursor->isChecked(),
            clientArea->isChecked(),
            sourceName->text(),
            &createdName)) {
        QMessageBox::critical(this, QStringLiteral("Window Capture"), engine_->lastError());
        return;
    }

    addSourceToCurrentGroup(createdName);
    saveProjectState();
    refreshSceneList();
    refreshSourceList(createdName);
    updatePreviewInformation();
    statusBar()->showMessage(QStringLiteral("%1 added to %2.").arg(createdName, engine_->activeSceneName()), 6000);
}

void MainWindow::showSelectedSourceProperties()
{
    const QString originalName = selectedSourceName();
    if (originalName.isEmpty() || engine_ == nullptr || !engine_->isReady()) {
        return;
    }
    if (engine_->isRecording() || remuxProcess_ != nullptr) {
        QMessageBox::information(
            this,
            QStringLiteral("Source Properties Locked"),
            QStringLiteral("Stop recording and wait for any remux to finish before changing source properties."));
        return;
    }

    const Vuttara::SourcePropertiesModel model = engine_->sourceProperties(originalName);
    if (model.sourceName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Source Properties"), engine_->lastError());
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("sourcePropertiesDialog"));
    dialog.setWindowTitle(QStringLiteral("Properties — %1").arg(model.sourceName));
    dialog.setMinimumWidth(620);

    auto* outerLayout = new QVBoxLayout(&dialog);
    auto* header = new QLabel(
        QStringLiteral("%1 • %2").arg(model.sourceName, model.sourceTypeName),
        &dialog);
    header->setObjectName(QStringLiteral("sourcePropertiesHeading"));
    QFont headerFont = header->font();
    headerFont.setPointSize(12);
    headerFont.setBold(true);
    header->setFont(headerFont);
    outerLayout->addWidget(header);

    auto* explanation = new QLabel(
        QStringLiteral(
            "These controls are generated from Vuttara's reusable source-properties adapter. Future source types register their own property definitions through the same dialog."),
        &dialog);
    explanation->setWordWrap(true);
    explanation->setObjectName(QStringLiteral("sourcePropertiesDescription"));
    outerLayout->addWidget(explanation);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* formContainer = new QWidget(scroll);
    auto* form = new QFormLayout(formContainer);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QHash<QString, QWidget*> editors;
    QHash<QString, std::uint32_t> colors;

    for (const Vuttara::SourcePropertyDefinition& property : model.properties) {
        QWidget* editor = nullptr;
        switch (property.kind) {
        case Vuttara::SourcePropertyKind::Text: {
            auto* lineEdit = new QLineEdit(property.value.toString(), formContainer);
            lineEdit->setReadOnly(property.readOnly);
            lineEdit->setMaxLength(80);
            editor = lineEdit;
            break;
        }
        case Vuttara::SourcePropertyKind::Boolean: {
            auto* checkBox = new QCheckBox(property.label, formContainer);
            checkBox->setChecked(property.value.toBool());
            editor = checkBox;
            break;
        }
        case Vuttara::SourcePropertyKind::Integer: {
            auto* spin = new QSpinBox(formContainer);
            spin->setRange(property.minimum, property.maximum);
            spin->setSingleStep(std::max(1, property.step));
            spin->setValue(property.value.toInt());
            spin->setReadOnly(property.readOnly);
            editor = spin;
            break;
        }
        case Vuttara::SourcePropertyKind::Choice: {
            auto* combo = new QComboBox(formContainer);
            for (const Vuttara::SourcePropertyOption& option : property.options) {
                combo->addItem(option.label, option.value.toVariant());
            }
            const int index = combo->findData(property.value.toVariant());
            combo->setCurrentIndex(index >= 0 ? index : 0);
            combo->setEnabled(!property.readOnly);
            editor = combo;
            break;
        }
        case Vuttara::SourcePropertyKind::Color: {
            const std::uint32_t rgba = static_cast<std::uint32_t>(property.value.toDouble());
            colors.insert(property.key, rgba);
            auto* button = new QPushButton(formContainer);
            const auto updateButton = [button](std::uint32_t value) {
                const QColor color = QColor::fromRgba(value);
                button->setText(color.name(QColor::HexArgb));
                button->setStyleSheet(QStringLiteral(
                    "QPushButton { background: %1; color: %2; min-height: 28px; }")
                    .arg(
                        color.name(QColor::HexArgb),
                        color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
            };
            updateButton(rgba);
            connect(button, &QPushButton::clicked, &dialog, [&, key = property.key, button, updateButton]() {
                const QColor selected = QColorDialog::getColor(
                    QColor::fromRgba(colors.value(key)),
                    &dialog,
                    QStringLiteral("Choose Source Color"),
                    QColorDialog::ShowAlphaChannel);
                if (selected.isValid()) {
                    colors.insert(key, selected.rgba());
                    updateButton(selected.rgba());
                }
            });
            editor = button;
            break;
        }
        case Vuttara::SourcePropertyKind::Information: {
            auto* label = new QLabel(property.value.toString(), formContainer);
            label->setWordWrap(true);
            editor = label;
            break;
        }
        }

        if (editor == nullptr) {
            continue;
        }
        editor->setToolTip(property.description);
        editors.insert(property.key, editor);
        if (property.kind == Vuttara::SourcePropertyKind::Boolean) {
            form->addRow(QString{}, editor);
        } else {
            form->addRow(property.label, editor);
        }
    }

    scroll->setWidget(formContainer);
    outerLayout->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok |
        QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply |
        QDialogButtonBox::RestoreDefaults,
        &dialog);
    outerLayout->addWidget(buttons);

    const auto collectValues = [&]() {
        QJsonObject values;
        for (const Vuttara::SourcePropertyDefinition& property : model.properties) {
            QWidget* editor = editors.value(property.key);
            if (editor == nullptr || property.kind == Vuttara::SourcePropertyKind::Information) {
                continue;
            }
            switch (property.kind) {
            case Vuttara::SourcePropertyKind::Text:
                values.insert(property.key, qobject_cast<QLineEdit*>(editor)->text().trimmed());
                break;
            case Vuttara::SourcePropertyKind::Boolean:
                values.insert(property.key, qobject_cast<QCheckBox*>(editor)->isChecked());
                break;
            case Vuttara::SourcePropertyKind::Integer:
                values.insert(property.key, qobject_cast<QSpinBox*>(editor)->value());
                break;
            case Vuttara::SourcePropertyKind::Choice:
                values.insert(property.key, QJsonValue::fromVariant(qobject_cast<QComboBox*>(editor)->currentData()));
                break;
            case Vuttara::SourcePropertyKind::Color:
                values.insert(property.key, static_cast<double>(colors.value(property.key)));
                break;
            case Vuttara::SourcePropertyKind::Information:
                break;
            }
        }
        return values;
    };

    QJsonObject originalValues;
    for (const Vuttara::SourcePropertyDefinition& property : model.properties) {
        if (property.kind != Vuttara::SourcePropertyKind::Information) {
            originalValues.insert(property.key, property.value);
        }
    }

    QString currentName = originalName;
    bool applied = false;
    const auto applyProperties = [&]() -> bool {
        QString updatedName;
        if (!engine_->applySourceProperties(currentName, collectValues(), &updatedName)) {
            QMessageBox::warning(&dialog, QStringLiteral("Source Properties"), engine_->lastError());
            return false;
        }
        if (updatedName != currentName) {
            renameSourceInGroups(engine_->activeSceneName(), currentName, updatedName);
            currentName = updatedName;
            dialog.setWindowTitle(QStringLiteral("Properties — %1").arg(currentName));
            header->setText(QStringLiteral("%1 • %2").arg(currentName, model.sourceTypeName));
        }
        applied = true;
        saveProjectState();
        refreshSceneList();
        refreshSourceList(currentName);
        updatePreviewInformation();
        statusBar()->showMessage(QStringLiteral("Properties applied to %1.").arg(currentName), 4000);
        return true;
    };

    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&]() {
        applyProperties();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (applyProperties()) {
            dialog.accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&]() {
        for (const Vuttara::SourcePropertyDefinition& property : model.properties) {
            QWidget* editor = editors.value(property.key);
            if (editor == nullptr || property.readOnly) {
                continue;
            }
            switch (property.kind) {
            case Vuttara::SourcePropertyKind::Text:
                qobject_cast<QLineEdit*>(editor)->setText(property.defaultValue.toString());
                break;
            case Vuttara::SourcePropertyKind::Boolean:
                qobject_cast<QCheckBox*>(editor)->setChecked(property.defaultValue.toBool());
                break;
            case Vuttara::SourcePropertyKind::Integer:
                qobject_cast<QSpinBox*>(editor)->setValue(property.defaultValue.toInt());
                break;
            case Vuttara::SourcePropertyKind::Choice: {
                auto* combo = qobject_cast<QComboBox*>(editor);
                const int index = combo->findData(property.defaultValue.toVariant());
                combo->setCurrentIndex(index >= 0 ? index : 0);
                break;
            }
            case Vuttara::SourcePropertyKind::Color: {
                const std::uint32_t value = static_cast<std::uint32_t>(property.defaultValue.toDouble());
                colors.insert(property.key, value);
                auto* button = qobject_cast<QPushButton*>(editor);
                const QColor color = QColor::fromRgba(value);
                button->setText(color.name(QColor::HexArgb));
                button->setStyleSheet(QStringLiteral(
                    "QPushButton { background: %1; color: %2; min-height: 28px; }")
                    .arg(
                        color.name(QColor::HexArgb),
                        color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
                break;
            }
            case Vuttara::SourcePropertyKind::Information:
                break;
            }
        }
    });

    if (dialog.exec() == QDialog::Rejected && applied) {
        QString restoredName;
        if (engine_->applySourceProperties(currentName, originalValues, &restoredName)) {
            renameSourceInGroups(engine_->activeSceneName(), currentName, restoredName);
            saveProjectState();
            refreshSceneList();
            refreshSourceList(restoredName);
            updatePreviewInformation();
        }
    }
}

void MainWindow::showSourceContextMenu(const QPoint& position)
{
    if (sourcesList_ == nullptr) {
        return;
    }

    QListWidgetItem* item = sourcesList_->itemAt(position);
    if (item != nullptr) {
        selectSourceDockItem(item, Qt::NoModifier);
    }

    showSourceContextMenuAtGlobal(
        sourcesList_->viewport()->mapToGlobal(position),
        item);
}

void MainWindow::showSourceContextMenuAtGlobal(
    const QPoint& globalPosition,
    QListWidgetItem* item)
{
    if (sourcesList_ == nullptr) {
        return;
    }

    QMenu menu(this);
    auto* addMenu = menu.addMenu(QStringLiteral("Add Source"));
    auto* addDisplay = addMenu->addAction(QStringLiteral("Display Capture"));
    auto* addWindow = addMenu->addAction(QStringLiteral("Window Capture"));
    addDisplay->setEnabled(addDisplaySourceAction_ == nullptr || addDisplaySourceAction_->isEnabled());
    addWindow->setEnabled(addWindowSourceAction_ == nullptr || addWindowSourceAction_->isEnabled());

    if (item == nullptr) {
        QAction* selected = menu.exec(globalPosition);
        if (selected == addDisplay) {
            showAddDisplayCaptureDialog();
        } else if (selected == addWindow) {
            showAddWindowCaptureDialog();
        }
        return;
    }

    const QStringList selectedNames = selectedSourceNames();
    const int selectedCount = selectedNames.size();
    const bool groupItemSelected = item->data(GroupRowRole).toBool();
    bool anyLocked = false;
    bool anyRemovable = false;
    bool allVisible = true;
    if (engine_ != nullptr && engine_->isReady()) {
        for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
            if (!selectedNames.contains(source.name)) {
                continue;
            }
            anyLocked = anyLocked || source.locked;
            anyRemovable = anyRemovable || source.removable;
            allVisible = allVisible && source.visible;
        }
    }
    const bool hasCurrentGroup = !currentSourceGroupName().isEmpty();

    menu.addSeparator();
    auto* properties = menu.addAction(QStringLiteral("Properties…"));
    properties->setEnabled(selectedCount == 1 && !groupItemSelected);
    auto* transformDialog = menu.addAction(QStringLiteral("Transform…"));
    transformDialog->setEnabled(selectedCount == 1 && !anyLocked && !groupItemSelected);
    auto* duplicate = menu.addAction(QStringLiteral("Duplicate Selected"));
    duplicate->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));

    menu.addSeparator();
    auto* group = menu.addAction(QStringLiteral("Group Selected…"));
    group->setEnabled(selectedCount >= 2);
    auto* ungroup = menu.addAction(QStringLiteral("Ungroup Selected"));
    ungroup->setEnabled(selectedCount >= 1);
    auto* removeGroup = menu.addAction(QStringLiteral("Remove Current Group Folder"));
    removeGroup->setEnabled(hasCurrentGroup);

    menu.addSeparator();
    auto* orderMenu = menu.addMenu(QStringLiteral("Order"));
    auto* moveUp = orderMenu->addAction(QStringLiteral("Move Up"));
    auto* moveDown = orderMenu->addAction(QStringLiteral("Move Down"));
    orderMenu->addSeparator();
    auto* moveTop = orderMenu->addAction(QStringLiteral("Move to Top"));
    auto* moveBottom = orderMenu->addAction(QStringLiteral("Move to Bottom"));
    orderMenu->setEnabled(selectedCount == 1 && !anyLocked && !groupItemSelected);

    auto* transformMenu = menu.addMenu(QStringLiteral("Transform"));
    transformMenu->setEnabled(selectedCount >= 1 && !anyLocked);
    auto* reset = transformMenu->addAction(QStringLiteral("Reset Transform"));
    transformMenu->addSeparator();
    auto* fit = transformMenu->addAction(QStringLiteral("Fit to Canvas"));
    auto* stretch = transformMenu->addAction(QStringLiteral("Stretch to Canvas"));
    transformMenu->addSeparator();
    auto* center = transformMenu->addAction(QStringLiteral("Center to Screen"));
    auto* centerHorizontal = transformMenu->addAction(QStringLiteral("Center Horizontally"));
    auto* centerVertical = transformMenu->addAction(QStringLiteral("Center Vertically"));
    transformMenu->addSeparator();
    auto* flipHorizontal = transformMenu->addAction(QStringLiteral("Flip Horizontal"));
    auto* flipVertical = transformMenu->addAction(QStringLiteral("Flip Vertical"));
    transformMenu->addSeparator();
    auto* rotateClockwise = transformMenu->addAction(QStringLiteral("Rotate 90° clockwise"));
    auto* rotateCounterclockwise = transformMenu->addAction(QStringLiteral("Rotate 90° counterclockwise"));

    menu.addSeparator();
    auto* visibility = menu.addAction(
        allVisible ? QStringLiteral("Hide Selected Sources") : QStringLiteral("Show Selected Sources"));
    auto* lock = menu.addAction(
        anyLocked ? QStringLiteral("Unlock Selected Sources") : QStringLiteral("Lock Selected Sources"));

    menu.addSeparator();
    auto* remove = menu.addAction(QStringLiteral("Remove Selected Sources"));
    remove->setEnabled(anyRemovable);

    QAction* selected = menu.exec(globalPosition);
    if (selected == addDisplay) {
        showAddDisplayCaptureDialog();
    } else if (selected == addWindow) {
        showAddWindowCaptureDialog();
    } else if (selected == properties) {
        showSelectedSourceProperties();
    } else if (selected == transformDialog) {
        showSelectedSourceTransform();
    } else if (selected == duplicate) {
        duplicateSelectedSources();
    } else if (selected == group) {
        createSourceGroup();
    } else if (selected == ungroup) {
        ungroupSelectedSources();
    } else if (selected == removeGroup) {
        removeCurrentSourceGroup();
    } else if (selected == moveUp) {
        moveSelectedSource(Vuttara::SourceOrderMovement::Up);
    } else if (selected == moveDown) {
        moveSelectedSource(Vuttara::SourceOrderMovement::Down);
    } else if (selected == moveTop) {
        moveSelectedSource(Vuttara::SourceOrderMovement::Top);
    } else if (selected == moveBottom) {
        moveSelectedSource(Vuttara::SourceOrderMovement::Bottom);
    } else if (selected == reset) {
        transformSelectedSources(TransformCommand::Reset);
    } else if (selected == fit) {
        transformSelectedSources(TransformCommand::Fit);
    } else if (selected == stretch) {
        transformSelectedSources(TransformCommand::Stretch);
    } else if (selected == center) {
        transformSelectedSources(TransformCommand::Center);
    } else if (selected == centerHorizontal) {
        transformSelectedSources(TransformCommand::CenterHorizontal);
    } else if (selected == centerVertical) {
        transformSelectedSources(TransformCommand::CenterVertical);
    } else if (selected == flipHorizontal) {
        transformSelectedSources(TransformCommand::FlipHorizontal);
    } else if (selected == flipVertical) {
        transformSelectedSources(TransformCommand::FlipVertical);
    } else if (selected == rotateClockwise) {
        transformSelectedSources(TransformCommand::RotateClockwise);
    } else if (selected == rotateCounterclockwise) {
        transformSelectedSources(TransformCommand::RotateCounterclockwise);
    } else if (selected == visibility) {
        setSourceVisibility(selectedNames, !allVisible);
    } else if (selected == lock) {
        toggleSelectedSourceLock();
    } else if (selected == remove) {
        removeSelectedSource();
    }
}


void MainWindow::renameSourceInGroups(
    const QString& sceneName,
    const QString& oldName,
    const QString& newName)
{
    if (oldName == newName || oldName.isEmpty() || newName.isEmpty()) {
        return;
    }
    QVector<SourceGroup>& groups = sourceGroupsByScene_[sceneName];
    for (SourceGroup& group : groups) {
        for (QString& sourceName : group.sourceNames) {
            if (sourceName == oldName) {
                sourceName = newName;
            }
        }
        group.sourceNames.removeDuplicates();
    }
}

void MainWindow::removeSelectedSource()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QStringList selectedNames = selectedSourceNames();
    if (selectedNames.isEmpty()) {
        return;
    }

    QStringList removableNames;
    for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
        if (selectedNames.contains(source.name) && source.removable) {
            removableNames.append(source.name);
        }
    }
    if (removableNames.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("Remove Sources"),
            QStringLiteral("The selected foundation source cannot be removed."));
        return;
    }

    QSettings settings;
    if (settings.value(QStringLiteral("general/confirmSourceRemovalV1"), true).toBool()) {
        const QString prompt = removableNames.size() == 1
            ? QStringLiteral("Remove %1 from %2?")
                  .arg(removableNames.first(), engine_->activeSceneName())
            : QStringLiteral("Remove %1 selected sources from %2?")
                  .arg(removableNames.size())
                  .arg(engine_->activeSceneName());
        if (QMessageBox::question(
                this,
                QStringLiteral("Remove Sources"),
                prompt,
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    QJsonObject beforeProject = engine_->projectState();
    beforeProject.insert(QStringLiteral("sourceGroups"), sourceGroupsState());
    beforeProject.insert(
        QStringLiteral("uiStage"),
        QStringLiteral("sources-organization-preview-selection-stage8c-v1-fix3"));
    QJsonObject afterProject = beforeProject;
    QJsonArray scenes = afterProject.value(QStringLiteral("scenes")).toArray();
    for (int sceneIndex = 0; sceneIndex < scenes.size(); ++sceneIndex) {
        QJsonObject scene = scenes.at(sceneIndex).toObject();
        if (scene.value(QStringLiteral("name")).toString() != engine_->activeSceneName()) {
            continue;
        }
        QJsonArray filteredSources;
        for (const QJsonValue& value : scene.value(QStringLiteral("sources")).toArray()) {
            const QJsonObject source = value.toObject();
            if (!removableNames.contains(source.value(QStringLiteral("name")).toString())) {
                filteredSources.append(source);
            }
        }
        scene.insert(QStringLiteral("sources"), filteredSources);
        scenes.replace(sceneIndex, scene);
        break;
    }
    afterProject.insert(QStringLiteral("scenes"), scenes);

    QJsonObject groupScenes = afterProject.value(QStringLiteral("sourceGroups")).toObject();
    QJsonArray groups = groupScenes.value(engine_->activeSceneName()).toArray();
    QJsonArray filteredGroups;
    for (const QJsonValue& groupValue : groups) {
        QJsonObject group = groupValue.toObject();
        QJsonArray retained;
        for (const QJsonValue& sourceValue : group.value(QStringLiteral("sources")).toArray()) {
            if (!removableNames.contains(sourceValue.toString())) {
                retained.append(sourceValue);
            }
        }
        if (!retained.isEmpty()) {
            group.insert(QStringLiteral("sources"), retained);
            filteredGroups.append(group);
        }
    }
    if (filteredGroups.isEmpty()) {
        groupScenes.remove(engine_->activeSceneName());
    } else {
        groupScenes.insert(engine_->activeSceneName(), filteredGroups);
    }
    afterProject.insert(QStringLiteral("sourceGroups"), groupScenes);

    const auto writeProject = [this](const QJsonObject& project) {
        QSaveFile file(Vuttara::AppPaths::projectStatePath());
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        const QJsonDocument document(project);
        return file.write(document.toJson(QJsonDocument::Indented)) >= 0 && file.commit();
    };

    if (!writeProject(afterProject)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Remove Sources"),
            QStringLiteral("The project file could not be updated, so no sources were removed."));
        return;
    }

    QStringList removedNames;
    for (const QString& name : removableNames) {
        if (!engine_->removeSource(name)) {
            const QString removalError = engine_->lastError();
            const bool fileRestored = writeProject(beforeProject);
            const bool liveRestored = engine_->restoreProjectState(beforeProject);
            restoreSourceGroups(beforeProject);
            refreshSceneList();
            refreshSourceGroupTabs();
            refreshSourceList();
            updatePreviewInformation();
            QMessageBox::critical(
                this,
                QStringLiteral("Remove Sources"),
                QStringLiteral(
                    "%1\n\nRollback results:\nProject file: %2\nLive scene and sources: %3")
                    .arg(
                        removalError,
                        fileRestored ? QStringLiteral("restored") : QStringLiteral("FAILED"),
                        liveRestored ? QStringLiteral("restored") : QStringLiteral("FAILED")));
            return;
        }
        removedNames.append(name);
        removeSourceFromGroups(engine_->activeSceneName(), name);
    }

    refreshSceneList();
    refreshSourceGroupTabs();
    refreshSourceList();
    updatePreviewInformation();
    statusBar()->showMessage(
        QStringLiteral("Removed %1 source%2 without interrupting active output.")
            .arg(removedNames.size())
            .arg(removedNames.size() == 1 ? QString{} : QStringLiteral("s")),
        4500);
}


void MainWindow::createSourceGroup()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QStringList names = selectedSourceNames();
    if (names.size() < 2) {
        QMessageBox::information(
            this,
            QStringLiteral("Group Sources"),
            QStringLiteral(
                "Select at least two sources with Ctrl or Shift, then choose Group Selected."));
        return;
    }

    bool accepted = false;
    QString requestedName = QInputDialog::getText(
        this,
        QStringLiteral("Group Sources"),
        QStringLiteral("Source folder name:"),
        QLineEdit::Normal,
        QStringLiteral("Source Group"),
        &accepted).trimmed();

    if (!accepted || requestedName.isEmpty()) {
        return;
    }

    requestedName = requestedName.left(40);
    const QString sceneName = engine_->activeSceneName();
    QVector<SourceGroup>& groups = sourceGroupsByScene_[sceneName];
    const QVector<SourceGroup> previousGroups = groups;

    const auto duplicate = std::find_if(
        groups.cbegin(),
        groups.cend(),
        [&requestedName](const SourceGroup& group) {
            return group.name.compare(requestedName, Qt::CaseInsensitive) == 0;
        });
    if (duplicate != groups.cend()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Group Sources"),
            QStringLiteral("A source group named %1 already exists.").arg(requestedName));
        return;
    }

    for (SourceGroup& group : groups) {
        for (const QString& name : names) {
            group.sourceNames.removeAll(name);
        }
    }

    groups.erase(
        std::remove_if(
            groups.begin(),
            groups.end(),
            [](const SourceGroup& group) {
                return group.sourceNames.isEmpty();
            }),
        groups.end());

    SourceGroup group;
    group.name = requestedName;
    group.sourceNames = names;
    group.expanded = true;
    groups.append(group);

    if (!saveProjectState()) {
        groups = previousGroups;
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Group Sources"),
            QStringLiteral("The source folder could not be saved, so the previous folder layout was restored."));
        return;
    }
    refreshSourceGroupTabs(requestedName);
    refreshSourceList(names.first());
    statusBar()->showMessage(
        QStringLiteral("%1 sources grouped under %2.")
            .arg(names.size())
            .arg(requestedName),
        5000);
}

void MainWindow::ungroupSelectedSources()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QStringList names = selectedSourceNames();
    if (names.isEmpty()) {
        return;
    }

    const QString sceneName = engine_->activeSceneName();
    QVector<SourceGroup>& groups = sourceGroupsByScene_[sceneName];
    const QVector<SourceGroup> previousGroups = groups;

    for (SourceGroup& group : groups) {
        for (const QString& name : names) {
            group.sourceNames.removeAll(name);
        }
    }

    groups.erase(
        std::remove_if(
            groups.begin(),
            groups.end(),
            [](const SourceGroup& group) {
                return group.sourceNames.isEmpty();
            }),
        groups.end());

    if (!saveProjectState()) {
        groups = previousGroups;
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Ungroup Sources"),
            QStringLiteral("The folder change could not be saved, so the previous folder layout was restored."));
        return;
    }
    refreshSourceGroupTabs();
    refreshSourceList();
    statusBar()->showMessage(QStringLiteral("Selected sources removed from their folder."), 4000);
}

void MainWindow::removeCurrentSourceGroup()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QString groupName = currentSourceGroupName();
    if (groupName.isEmpty()) {
        return;
    }

    if (QMessageBox::question(
            this,
            QStringLiteral("Remove Source Group"),
            QStringLiteral(
                "Remove the %1 folder? The sources themselves will not be removed.")
                .arg(groupName))
        != QMessageBox::Yes) {
        return;
    }

    QVector<SourceGroup>& groups =
        sourceGroupsByScene_[engine_->activeSceneName()];
    const QVector<SourceGroup> previousGroups = groups;
    groups.erase(
        std::remove_if(
            groups.begin(),
            groups.end(),
            [&groupName](const SourceGroup& group) {
                return group.name == groupName;
            }),
        groups.end());

    if (!saveProjectState()) {
        groups = previousGroups;
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Remove Source Group"),
            QStringLiteral("The folder removal could not be saved, so the folder was restored."));
        return;
    }
    refreshSourceGroupTabs();
    refreshSourceList();
}

void MainWindow::handleSourceGroupTabChanged(int)
{
    refreshSourceList();
}

void MainWindow::refreshSourceGroupTabs(const QString& preferredGroup)
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    pruneSourceGroupsForActiveScene();
    if (preferredGroup.isEmpty()) {
        return;
    }

    QVector<SourceGroup>& groups = sourceGroupsByScene_[engine_->activeSceneName()];
    for (SourceGroup& group : groups) {
        if (group.name == preferredGroup) {
            group.expanded = true;
            break;
        }
    }
}

void MainWindow::pruneSourceGroupsForActiveScene()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QString sceneName = engine_->activeSceneName();
    QVector<SourceGroup>& groups = sourceGroupsByScene_[sceneName];

    QSet<QString> existingNames;
    for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
        existingNames.insert(source.name);
    }

    QSet<QString> claimedNames;
    for (SourceGroup& group : groups) {
        QStringList retained;
        for (const QString& name : group.sourceNames) {
            if (
                existingNames.contains(name) &&
                !claimedNames.contains(name) &&
                !retained.contains(name)) {
                retained.append(name);
                claimedNames.insert(name);
            }
        }
        group.sourceNames = retained;
    }

    groups.erase(
        std::remove_if(
            groups.begin(),
            groups.end(),
            [](const SourceGroup& group) {
                return group.name.trimmed().isEmpty() ||
                    group.sourceNames.isEmpty();
            }),
        groups.end());
}

void MainWindow::addSourceToCurrentGroup(const QString& sourceName)
{
    if (engine_ == nullptr || sourceName.isEmpty()) {
        return;
    }

    const QString groupName = currentSourceGroupName();
    if (groupName.isEmpty()) {
        return;
    }

    QVector<SourceGroup>& groups =
        sourceGroupsByScene_[engine_->activeSceneName()];
    for (SourceGroup& group : groups) {
        if (group.name == groupName && !group.sourceNames.contains(sourceName)) {
            group.sourceNames.append(sourceName);
            group.expanded = true;
            return;
        }
    }
}

void MainWindow::removeSourceFromGroups(
    const QString& sceneName,
    const QString& sourceName)
{
    QVector<SourceGroup>& groups = sourceGroupsByScene_[sceneName];

    for (SourceGroup& group : groups) {
        group.sourceNames.removeAll(sourceName);
    }

    groups.erase(
        std::remove_if(
            groups.begin(),
            groups.end(),
            [](const SourceGroup& group) {
                return group.sourceNames.isEmpty();
            }),
        groups.end());
}

QString MainWindow::currentSourceGroupName() const
{
    if (sourcesList_ == nullptr || sourcesList_->currentItem() == nullptr) {
        return {};
    }
    return sourcesList_->currentItem()->data(GroupNameRole).toString();
}

QStringList MainWindow::selectedSourceNames() const
{
    QStringList names;
    if (sourcesList_ == nullptr) {
        return names;
    }

    const QString activeScene =
        engine_ != nullptr && engine_->isReady()
        ? engine_->activeSceneName()
        : QString{};
    const QVector<SourceGroup> groups = sourceGroupsByScene_.value(activeScene);

    for (QListWidgetItem* item : sourcesList_->selectedItems()) {
        if (item == nullptr) {
            continue;
        }
        if (item->data(GroupRowRole).toBool()) {
            const QString groupName = item->data(GroupNameRole).toString();
            const auto group = std::find_if(
                groups.cbegin(),
                groups.cend(),
                [&groupName](const SourceGroup& candidate) {
                    return candidate.name == groupName;
                });
            if (group != groups.cend()) {
                for (const QString& sourceName : group->sourceNames) {
                    if (!names.contains(sourceName)) {
                        names.append(sourceName);
                    }
                }
            }
            continue;
        }

        const QString name = item->data(NameRole).toString();
        if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
        }
    }

    return names;
}

QJsonObject MainWindow::sourceGroupsState() const
{
    QJsonObject scenesObject;

    for (auto sceneIterator = sourceGroupsByScene_.cbegin();
         sceneIterator != sourceGroupsByScene_.cend();
         ++sceneIterator) {
        QJsonArray groupsArray;

        for (const SourceGroup& group : sceneIterator.value()) {
            QJsonArray sourcesArray;
            for (const QString& sourceName : group.sourceNames) {
                sourcesArray.append(sourceName);
            }

            QJsonObject groupObject;
            groupObject.insert(QStringLiteral("name"), group.name);
            groupObject.insert(QStringLiteral("expanded"), group.expanded);
            groupObject.insert(QStringLiteral("sources"), sourcesArray);
            groupsArray.append(groupObject);
        }

        if (!groupsArray.isEmpty()) {
            scenesObject.insert(sceneIterator.key(), groupsArray);
        }
    }

    return scenesObject;
}

void MainWindow::restoreSourceGroups(const QJsonObject& project)
{
    sourceGroupsByScene_.clear();

    const QJsonObject scenesObject =
        project.value(QStringLiteral("sourceGroups")).toObject();

    for (auto sceneIterator = scenesObject.constBegin();
         sceneIterator != scenesObject.constEnd();
         ++sceneIterator) {
        QVector<SourceGroup> groups;
        const QJsonArray groupsArray = sceneIterator.value().toArray();

        for (const QJsonValue& groupValue : groupsArray) {
            const QJsonObject groupObject = groupValue.toObject();
            const QString groupName =
                groupObject.value(QStringLiteral("name")).toString().trimmed();
            if (groupName.isEmpty()) {
                continue;
            }

            SourceGroup group;
            group.name = groupName.left(40);
            group.expanded = groupObject.value(QStringLiteral("expanded")).toBool(true);

            const QJsonArray sourcesArray =
                groupObject.value(QStringLiteral("sources")).toArray();
            for (const QJsonValue& sourceValue : sourcesArray) {
                const QString sourceName = sourceValue.toString();
                if (!sourceName.isEmpty() && !group.sourceNames.contains(sourceName)) {
                    group.sourceNames.append(sourceName);
                }
            }

            if (!group.sourceNames.isEmpty()) {
                groups.append(group);
            }
        }

        if (!groups.isEmpty()) {
            sourceGroupsByScene_.insert(sceneIterator.key(), groups);
        }
    }

    pruneSourceGroupsForActiveScene();
}

void MainWindow::setSourceVisibility(
    const QStringList& sourceNames,
    bool visible)
{
    if (engine_ == nullptr || !engine_->isReady() || sourceNames.isEmpty()) {
        return;
    }

    QHash<QString, bool> previous;
    QStringList changed;
    for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
        if (!sourceNames.contains(source.name) || source.visible == visible) {
            continue;
        }
        previous.insert(source.name, source.visible);
        if (!engine_->setSourceVisible(source.name, visible)) {
            for (const QString& restoredName : changed) {
                engine_->setSourceVisible(restoredName, previous.value(restoredName));
            }
            QMessageBox::warning(this, QStringLiteral("Source Visibility"), engine_->lastError());
            refreshSourceList();
            return;
        }
        changed.append(source.name);
    }

    if (changed.isEmpty()) {
        return;
    }

    if (!saveProjectState()) {
        for (const QString& restoredName : changed) {
            engine_->setSourceVisible(restoredName, previous.value(restoredName));
        }
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Source Visibility"),
            QStringLiteral("The visibility change could not be saved, so it was rolled back."));
        return;
    }

    refreshSourceList();
    updatePreviewInformation();
    statusBar()->showMessage(
        QStringLiteral("%1 %2 source%3.")
            .arg(visible ? QStringLiteral("Showed") : QStringLiteral("Hid"))
            .arg(changed.size())
            .arg(changed.size() == 1 ? QString{} : QStringLiteral("s")),
        3500);
}

void MainWindow::setSourceLockState(
    const QStringList& sourceNames,
    bool locked)
{
    if (engine_ == nullptr || !engine_->isReady() || sourceNames.isEmpty()) {
        return;
    }

    QHash<QString, bool> previous;
    QStringList changed;
    for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
        if (!sourceNames.contains(source.name) || source.locked == locked) {
            continue;
        }
        previous.insert(source.name, source.locked);
        if (!engine_->setSourceLocked(source.name, locked)) {
            for (const QString& restoredName : changed) {
                engine_->setSourceLocked(restoredName, previous.value(restoredName));
            }
            QMessageBox::warning(this, QStringLiteral("Source Lock"), engine_->lastError());
            refreshSourceList();
            return;
        }
        changed.append(source.name);
    }

    if (changed.isEmpty()) {
        return;
    }

    if (!saveProjectState()) {
        for (const QString& restoredName : changed) {
            engine_->setSourceLocked(restoredName, previous.value(restoredName));
        }
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Source Lock"),
            QStringLiteral("The lock change could not be saved, so the previous lock state was restored."));
        return;
    }

    refreshSourceList();
    statusBar()->showMessage(
        QStringLiteral("%1 %2 source%3.")
            .arg(locked ? QStringLiteral("Locked") : QStringLiteral("Unlocked"))
            .arg(changed.size())
            .arg(changed.size() == 1 ? QString{} : QStringLiteral("s")),
        3500);
}

void MainWindow::toggleSourceGroupExpanded(const QString& groupName)
{
    if (engine_ == nullptr || !engine_->isReady() || groupName.isEmpty()) {
        return;
    }

    QVector<SourceGroup>& groups = sourceGroupsByScene_[engine_->activeSceneName()];
    auto group = std::find_if(
        groups.begin(),
        groups.end(),
        [&groupName](const SourceGroup& candidate) {
            return candidate.name == groupName;
        });
    if (group == groups.end()) {
        return;
    }

    const bool previous = group->expanded;
    group->expanded = !group->expanded;
    if (!saveProjectState()) {
        group->expanded = previous;
        QMessageBox::warning(
            this,
            QStringLiteral("Source Group"),
            QStringLiteral("The folder state could not be saved, so it was restored."));
    }
    refreshSourceList();
}

void MainWindow::startSourceDockDrag(QListWidgetItem* item)
{
    if (
        sourcesList_ == nullptr ||
        item == nullptr ||
        engine_ == nullptr ||
        !engine_->isReady()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("scene"), engine_->activeSceneName());
    if (item->data(GroupRowRole).toBool()) {
        const QString groupName = item->data(GroupNameRole).toString();
        if (groupName.isEmpty()) {
            return;
        }
        payload.insert(QStringLiteral("kind"), QStringLiteral("group"));
        payload.insert(QStringLiteral("group"), groupName);
    } else {
        const QString itemName = item->data(NameRole).toString();
        QStringList names = selectedSourceNames();
        if (!itemName.isEmpty() && !names.contains(itemName)) {
            names = QStringList{itemName};
            selectSourcesFromPreview(names);
        }
        if (names.isEmpty()) {
            return;
        }
        QJsonArray sourceArray;
        for (const QString& name : names) {
            sourceArray.append(name);
        }
        payload.insert(QStringLiteral("kind"), QStringLiteral("sources"));
        payload.insert(QStringLiteral("sources"), sourceArray);
    }

    auto* mimeData = new QMimeData();
    mimeData->setData(
        SourceDockMimeType,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

    auto* drag = new QDrag(sourcesList_);
    drag->setMimeData(mimeData);
    if (QWidget* rowWidget = sourcesList_->itemWidget(item)) {
        const QPixmap pixmap = rowWidget->grab();
        if (!pixmap.isNull()) {
            drag->setPixmap(pixmap);
            drag->setHotSpot(QPoint(18, pixmap.height() / 2));
        }
    }
    statusBar()->showMessage(
        QStringLiteral("Drag above or below a row to reorder, or onto a folder to group sources."),
        3500);
    drag->exec(Qt::MoveAction);
}

bool MainWindow::handleSourceDockDrop(
    const QByteArray& payloadBytes,
    QListWidgetItem* target,
    int dropPosition)
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(payloadBytes);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject payload = document.object();
    const QString sceneName = payload.value(QStringLiteral("scene")).toString();
    if (sceneName != engine_->activeSceneName()) {
        statusBar()->showMessage(QStringLiteral("Sources can only be reordered inside their current scene."), 4500);
        return false;
    }

    const QVector<Vuttara::SourceInfo> sourceInfos = engine_->sourceInfos();
    QStringList currentOrder;
    for (const Vuttara::SourceInfo& source : sourceInfos) {
        currentOrder.append(source.name);
    }

    const QString kind = payload.value(QStringLiteral("kind")).toString();
    const QString draggedGroupName = payload.value(QStringLiteral("group")).toString();
    QStringList requestedNames;
    if (kind == QStringLiteral("group")) {
        const QVector<SourceGroup>& groups = sourceGroupsByScene_[sceneName];
        const auto group = std::find_if(
            groups.cbegin(),
            groups.cend(),
            [&draggedGroupName](const SourceGroup& candidate) {
                return candidate.name == draggedGroupName;
            });
        if (group == groups.cend()) {
            return false;
        }
        requestedNames = group->sourceNames;
    } else if (kind == QStringLiteral("sources")) {
        for (const QJsonValue& value : payload.value(QStringLiteral("sources")).toArray()) {
            const QString name = value.toString();
            if (!name.isEmpty() && !requestedNames.contains(name)) {
                requestedNames.append(name);
            }
        }
    } else {
        return false;
    }

    QStringList draggedNames;
    for (const QString& name : currentOrder) {
        if (requestedNames.contains(name)) {
            draggedNames.append(name);
        }
    }
    if (draggedNames.isEmpty()) {
        return false;
    }

    const bool targetIsGroup = target != nullptr && target->data(GroupRowRole).toBool();
    const QString targetGroupName = target != nullptr
        ? target->data(GroupNameRole).toString()
        : QString{};
    const QString targetSourceName = target != nullptr && !targetIsGroup
        ? target->data(NameRole).toString()
        : QString{};

    if (
        kind == QStringLiteral("group") &&
        !draggedGroupName.isEmpty() &&
        targetGroupName == draggedGroupName) {
        return false;
    }
    if (!targetSourceName.isEmpty() && draggedNames.contains(targetSourceName)) {
        return false;
    }

    const QJsonObject previousProject = engine_->projectState();
    QVector<SourceGroup>& groups = sourceGroupsByScene_[sceneName];
    const QVector<SourceGroup> previousGroups = groups;

    const auto rollback = [&]() {
        engine_->restoreProjectState(previousProject);
        groups = previousGroups;
        refreshSourceGroupTabs();
        refreshSourceList();
    };

    if (kind == QStringLiteral("sources")) {
        QString destinationGroup;
        if (
            !targetGroupName.isEmpty() &&
            (targetIsGroup ? dropPosition == SourceDropOn : true)) {
            destinationGroup = targetGroupName;
        }

        for (SourceGroup& group : groups) {
            for (const QString& name : draggedNames) {
                group.sourceNames.removeAll(name);
            }
        }

        if (!destinationGroup.isEmpty()) {
            auto group = std::find_if(
                groups.begin(),
                groups.end(),
                [&destinationGroup](const SourceGroup& candidate) {
                    return candidate.name == destinationGroup;
                });
            if (group == groups.end()) {
                groups = previousGroups;
                return false;
            }
            int childIndex = group->sourceNames.size();
            if (!targetSourceName.isEmpty()) {
                const int targetIndex = group->sourceNames.indexOf(targetSourceName);
                if (targetIndex >= 0) {
                    childIndex = targetIndex + (dropPosition == SourceDropBelow ? 1 : 0);
                }
            }
            for (const QString& name : draggedNames) {
                group->sourceNames.insert(childIndex, name);
                ++childIndex;
            }
            group->expanded = true;
        }

        groups.erase(
            std::remove_if(
                groups.begin(),
                groups.end(),
                [](const SourceGroup& group) {
                    return group.sourceNames.isEmpty();
                }),
            groups.end());
    } else {
        const auto draggedGroup = std::find_if(
            groups.begin(),
            groups.end(),
            [&draggedGroupName](const SourceGroup& candidate) {
                return candidate.name == draggedGroupName;
            });
        if (draggedGroup == groups.end()) {
            return false;
        }
        SourceGroup movingGroup = *draggedGroup;
        groups.erase(draggedGroup);
        int groupIndex = groups.size();
        if (!targetGroupName.isEmpty()) {
            const auto targetGroup = std::find_if(
                groups.begin(),
                groups.end(),
                [&targetGroupName](const SourceGroup& candidate) {
                    return candidate.name == targetGroupName;
                });
            if (targetGroup != groups.end()) {
                groupIndex = static_cast<int>(std::distance(groups.begin(), targetGroup));
                if (dropPosition != SourceDropAbove) {
                    ++groupIndex;
                }
            }
        }
        groups.insert(std::clamp(groupIndex, 0, static_cast<int>(groups.size())), movingGroup);
    }

    QStringList desiredOrder = currentOrder;
    for (const QString& name : draggedNames) {
        desiredOrder.removeAll(name);
    }

    int insertionIndex = desiredOrder.size();
    if (!targetSourceName.isEmpty()) {
        const int targetIndex = desiredOrder.indexOf(targetSourceName);
        if (targetIndex >= 0) {
            insertionIndex = targetIndex + (dropPosition == SourceDropBelow ? 1 : 0);
        }
    } else if (!targetGroupName.isEmpty()) {
        QStringList targetMembers;
        const auto targetGroup = std::find_if(
            groups.cbegin(),
            groups.cend(),
            [&targetGroupName](const SourceGroup& candidate) {
                return candidate.name == targetGroupName;
            });
        if (targetGroup != groups.cend()) {
            for (const QString& name : desiredOrder) {
                if (targetGroup->sourceNames.contains(name)) {
                    targetMembers.append(name);
                }
            }
        }
        if (!targetMembers.isEmpty()) {
            const int firstIndex = desiredOrder.indexOf(targetMembers.first());
            const int lastIndex = desiredOrder.indexOf(targetMembers.last());
            insertionIndex = dropPosition == SourceDropAbove
                ? firstIndex
                : lastIndex + 1;
        }
    }
    insertionIndex = std::clamp(insertionIndex, 0, static_cast<int>(desiredOrder.size()));
    for (int index = 0; index < draggedNames.size(); ++index) {
        desiredOrder.insert(insertionIndex + index, draggedNames.at(index));
    }

    for (SourceGroup& group : groups) {
        QStringList orderedChildren;
        for (const QString& name : desiredOrder) {
            if (group.sourceNames.contains(name)) {
                orderedChildren.append(name);
            }
        }
        group.sourceNames = orderedChildren;
    }

    for (int index = desiredOrder.size() - 1; index >= 0; --index) {
        if (!engine_->moveSource(desiredOrder.at(index), Vuttara::SourceOrderMovement::Top)) {
            rollback();
            QMessageBox::warning(this, QStringLiteral("Source Order"), engine_->lastError());
            return false;
        }
    }

    if (!saveProjectState()) {
        rollback();
        QMessageBox::warning(
            this,
            QStringLiteral("Source Order"),
            QStringLiteral("The drag-and-drop change could not be saved, so the previous source layout was restored."));
        return false;
    }

    refreshSourceGroupTabs(
        kind == QStringLiteral("group") ? draggedGroupName : targetGroupName);
    refreshSourceList();
    if (kind == QStringLiteral("group")) {
        for (int row = 0; row < sourcesList_->count(); ++row) {
            QListWidgetItem* item = sourcesList_->item(row);
            if (
                item != nullptr &&
                item->data(GroupRowRole).toBool() &&
                item->data(GroupNameRole).toString() == draggedGroupName) {
                selectSourceDockItem(item, Qt::NoModifier);
                break;
            }
        }
    } else {
        selectSourcesFromPreview(draggedNames);
    }
    updatePreviewInformation();
    statusBar()->showMessage(QStringLiteral("Source order and folder placement saved."), 4000);
    return true;
}

void MainWindow::selectSourceDockItem(
    QListWidgetItem* item,
    Qt::KeyboardModifiers modifiers)
{
    if (sourcesList_ == nullptr || item == nullptr) {
        return;
    }

    const QSignalBlocker blocker(sourcesList_);
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        QListWidgetItem* anchor = sourcesList_->currentItem();
        const int anchorRow = anchor != nullptr ? sourcesList_->row(anchor) : -1;
        const int targetRow = sourcesList_->row(item);
        if (!modifiers.testFlag(Qt::ControlModifier)) {
            sourcesList_->clearSelection();
        }
        if (anchorRow >= 0 && targetRow >= 0) {
            const int first = std::min(anchorRow, targetRow);
            const int last = std::max(anchorRow, targetRow);
            for (int row = first; row <= last; ++row) {
                if (QListWidgetItem* rangeItem = sourcesList_->item(row)) {
                    rangeItem->setSelected(true);
                }
            }
        } else {
            item->setSelected(true);
        }
        sourcesList_->setCurrentItem(item, QItemSelectionModel::NoUpdate);
    } else if (modifiers.testFlag(Qt::ControlModifier)) {
        const bool selected = item->isSelected();
        item->setSelected(!selected);
        sourcesList_->setCurrentItem(item, QItemSelectionModel::NoUpdate);
    } else {
        sourcesList_->clearSelection();
        item->setSelected(true);
        sourcesList_->setCurrentItem(item, QItemSelectionModel::NoUpdate);
    }

    refreshSourceRowVisuals();
    updateSourceControls();
    refreshPreviewInteractionOverlay();
}

void MainWindow::refreshSourceRowVisuals()
{
    if (sourcesList_ == nullptr) {
        return;
    }
    QListWidgetItem* current = sourcesList_->currentItem();
    for (int row = 0; row < sourcesList_->count(); ++row) {
        QListWidgetItem* item = sourcesList_->item(row);
        auto* rowWidget = dynamic_cast<SourceDockRow*>(sourcesList_->itemWidget(item));
        if (item != nullptr && rowWidget != nullptr) {
            rowWidget->setSelectionVisual(item->isSelected(), item == current);
        }
    }
}

void MainWindow::handleSourceVisibilityChanged(QListWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }
    const QString sourceName = item->data(NameRole).toString();
    if (!sourceName.isEmpty()) {
        setSourceVisibility(QStringList{sourceName}, item->checkState() == Qt::Checked);
    }
}

void MainWindow::moveSelectedSource(Vuttara::SourceOrderMovement movement)
{
    const QString sourceName = selectedSourceName();
    if (sourceName.isEmpty() || engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QJsonObject beforeProject = engine_->projectState();
    if (!engine_->moveSource(sourceName, movement)) {
        QMessageBox::warning(this, QStringLiteral("Source Order"), engine_->lastError());
        return;
    }

    if (!saveProjectState()) {
        engine_->restoreProjectState(beforeProject);
        refreshSourceList(sourceName);
        QMessageBox::warning(
            this,
            QStringLiteral("Source Order"),
            QStringLiteral("The source order could not be saved, so the previous order was restored."));
        return;
    }
    refreshSourceList(sourceName);
}

void MainWindow::toggleSelectedSourceLock()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QStringList names = selectedSourceNames();
    if (names.isEmpty()) {
        return;
    }

    bool allLocked = true;
    for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
        if (names.contains(source.name) && !source.locked) {
            allLocked = false;
            break;
        }
    }
    setSourceLockState(names, !allLocked);
}

void MainWindow::showSelectedSourceTransform()
{
    const QString sourceName = selectedSourceName();
    if (sourceName.isEmpty() || engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    const QVector<Vuttara::SourceInfo> sources = engine_->sourceInfos();
    const auto selected = std::find_if(
        sources.cbegin(),
        sources.cend(),
        [&sourceName](const Vuttara::SourceInfo& candidate) {
            return candidate.name == sourceName;
        });
    if (selected == sources.cend()) {
        return;
    }
    if (selected->locked) {
        QMessageBox::information(
            this,
            QStringLiteral("Edit Transform"),
            QStringLiteral("Unlock %1 before changing its transform.").arg(sourceName));
        return;
    }

    const Vuttara::SourceTransform original = selected->transform;
    Vuttara::SourceTransform current = original;
    const double sourceWidth = std::max(1.0, static_cast<double>(selected->sourceWidth));
    const double sourceHeight = std::max(1.0, static_cast<double>(selected->sourceHeight));

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("sourceTransformDialog"));
    dialog.setWindowTitle(QStringLiteral("Transform — %1").arg(sourceName));
    dialog.setMinimumWidth(480);

    auto* outerLayout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    const auto createSpin = [&dialog](double minimum, double maximum, double value) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setRange(minimum, maximum);
        spin->setDecimals(1);
        spin->setSingleStep(1.0);
        spin->setValue(value);
        return spin;
    };

    auto* positionX = createSpin(-10000.0, 10000.0, original.x);
    auto* positionY = createSpin(-10000.0, 10000.0, original.y);
    auto* boundsWidth = createSpin(1.0, 7680.0, original.width);
    auto* boundsHeight = createSpin(1.0, 7680.0, original.height);
    auto* rotation = createSpin(-360.0, 360.0, original.rotation);
    rotation->setSuffix(QStringLiteral("°"));
    auto* cropLeft = createSpin(0.0, sourceWidth - 1.0, original.cropLeft);
    auto* cropTop = createSpin(0.0, sourceHeight - 1.0, original.cropTop);
    auto* cropRight = createSpin(0.0, sourceWidth - 1.0, original.cropRight);
    auto* cropBottom = createSpin(0.0, sourceHeight - 1.0, original.cropBottom);
    auto* flipHorizontal = new QCheckBox(QStringLiteral("Flip horizontal"), &dialog);
    auto* flipVertical = new QCheckBox(QStringLiteral("Flip vertical"), &dialog);
    auto* stretch = new QCheckBox(QStringLiteral("Stretch source into bounds"), &dialog);
    flipHorizontal->setChecked(original.flipHorizontal);
    flipVertical->setChecked(original.flipVertical);
    stretch->setChecked(original.stretchToBounds);

    form->addRow(QStringLiteral("Position X"), positionX);
    form->addRow(QStringLiteral("Position Y"), positionY);
    form->addRow(QStringLiteral("Bounds width"), boundsWidth);
    form->addRow(QStringLiteral("Bounds height"), boundsHeight);
    form->addRow(QStringLiteral("Rotation"), rotation);
    form->addRow(QStringLiteral("Crop left"), cropLeft);
    form->addRow(QStringLiteral("Crop top"), cropTop);
    form->addRow(QStringLiteral("Crop right"), cropRight);
    form->addRow(QStringLiteral("Crop bottom"), cropBottom);
    form->addRow(QString{}, flipHorizontal);
    form->addRow(QString{}, flipVertical);
    form->addRow(QString{}, stretch);
    outerLayout->addLayout(form);

    auto* quickActions = new QHBoxLayout();
    auto* fitButton = new QPushButton(QStringLiteral("Fit to Canvas"), &dialog);
    auto* centerButton = new QPushButton(QStringLiteral("Center"), &dialog);
    auto* resetButton = new QPushButton(QStringLiteral("Reset Values"), &dialog);
    quickActions->addWidget(fitButton);
    quickActions->addWidget(centerButton);
    quickActions->addWidget(resetButton);
    quickActions->addStretch();
    outerLayout->addLayout(quickActions);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        &dialog);
    outerLayout->addWidget(buttons);

    const auto values = [&]() {
        Vuttara::SourceTransform transform = current;
        transform.x = positionX->value();
        transform.y = positionY->value();
        transform.width = boundsWidth->value();
        transform.height = boundsHeight->value();
        transform.rotation = rotation->value();
        transform.cropLeft = cropLeft->value();
        transform.cropTop = cropTop->value();
        transform.cropRight = cropRight->value();
        transform.cropBottom = cropBottom->value();
        transform.flipHorizontal = flipHorizontal->isChecked();
        transform.flipVertical = flipVertical->isChecked();
        transform.stretchToBounds = stretch->isChecked();
        return transform;
    };
    const auto loadValues = [&](const Vuttara::SourceTransform& transform) {
        positionX->setValue(transform.x);
        positionY->setValue(transform.y);
        boundsWidth->setValue(transform.width);
        boundsHeight->setValue(transform.height);
        rotation->setValue(transform.rotation);
        cropLeft->setValue(transform.cropLeft);
        cropTop->setValue(transform.cropTop);
        cropRight->setValue(transform.cropRight);
        cropBottom->setValue(transform.cropBottom);
        flipHorizontal->setChecked(transform.flipHorizontal);
        flipVertical->setChecked(transform.flipVertical);
        stretch->setChecked(transform.stretchToBounds);
    };
    const auto applyValues = [&]() -> bool {
        const Vuttara::SourceTransform requested = values();
        if (!applyTransformEdit(
                QHash<QString, Vuttara::SourceTransform>{{sourceName, current}},
                QHash<QString, Vuttara::SourceTransform>{{sourceName, requested}},
                QStringLiteral("Edit transform"),
                false)) {
            return false;
        }
        current = requested;
        return true;
    };

    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&]() {
        applyValues();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (applyValues()) {
            dialog.accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(resetButton, &QPushButton::clicked, &dialog, [&]() {
        Vuttara::SourceTransform reset;
        reset.x = 960.0;
        reset.y = 540.0;
        reset.width = sourceWidth;
        reset.height = sourceHeight;
        loadValues(reset);
    });
    connect(fitButton, &QPushButton::clicked, &dialog, [&]() {
        Vuttara::SourceTransform fit = values();
        fit.x = 960.0;
        fit.y = 540.0;
        fit.width = 1920.0;
        fit.height = 1080.0;
        fit.rotation = 0.0;
        fit.cropLeft = 0.0;
        fit.cropTop = 0.0;
        fit.cropRight = 0.0;
        fit.cropBottom = 0.0;
        fit.stretchToBounds = false;
        loadValues(fit);
    });
    connect(centerButton, &QPushButton::clicked, &dialog, [&]() {
        positionX->setValue(960.0);
        positionY->setValue(540.0);
    });

    const int result = dialog.exec();
    if (result == QDialog::Rejected && current != original) {
        applyTransformEdit(
            QHash<QString, Vuttara::SourceTransform>{{sourceName, current}},
            QHash<QString, Vuttara::SourceTransform>{{sourceName, original}},
            QStringLiteral("Cancel transform edit"),
            false);
        return;
    }
    if (result == QDialog::Accepted && current != original) {
        pushTransformHistory(TransformHistoryEntry{
            engine_->activeSceneName(),
            QHash<QString, Vuttara::SourceTransform>{{sourceName, original}},
            QHash<QString, Vuttara::SourceTransform>{{sourceName, current}},
            QStringLiteral("Edit transform"),
        });
    }
}

void MainWindow::fitSelectedSourceToCanvas()
{
    transformSelectedSources(TransformCommand::Fit);
}

void MainWindow::centerSelectedSource()
{
    transformSelectedSources(TransformCommand::Center);
}


void MainWindow::syncPreviewInteractionOverlay()
{
    if (previewInteractionOverlay_ == nullptr || previewWidget_ == nullptr) {
        return;
    }

    const bool shouldShow =
        isVisible() &&
        !isMinimized() &&
        previewWidget_->isVisible() &&
        previewWidget_->width() > 0 &&
        previewWidget_->height() > 0 &&
        !shouldSuppressPreviewInteractionOverlay();

    if (!shouldShow) {
        suppressPreviewInteractionOverlay();
        return;
    }

    const QRect targetGeometry(
        previewWidget_->mapToGlobal(QPoint(0, 0)),
        previewWidget_->size());
    if (previewInteractionOverlay_->geometry() != targetGeometry) {
        previewInteractionOverlay_->setGeometry(targetGeometry);
    }
    if (!previewInteractionOverlay_->isVisible()) {
        previewInteractionOverlay_->show();
    }
    previewInteractionOverlay_->raise();
    refreshPreviewInteractionOverlay();
}

void MainWindow::refreshPreviewInteractionOverlay()
{
    if (previewInteractionOverlay_ != nullptr) {
        static_cast<PreviewInteractionOverlay*>(previewInteractionOverlay_)->refreshOverlay();
    }
}

void MainWindow::selectSourcesFromPreview(const QStringList& sourceNames)
{
    if (sourcesList_ == nullptr || engine_ == nullptr || !engine_->isReady()) {
        return;
    }

    QVector<SourceGroup>& groups = sourceGroupsByScene_[engine_->activeSceneName()];
    QHash<QString, bool> previousExpanded;
    bool expandedChanged = false;
    for (SourceGroup& group : groups) {
        bool containsRequestedSource = false;
        for (const QString& sourceName : sourceNames) {
            if (group.sourceNames.contains(sourceName)) {
                containsRequestedSource = true;
                break;
            }
        }
        if (containsRequestedSource && !group.expanded) {
            previousExpanded.insert(group.name, group.expanded);
            group.expanded = true;
            expandedChanged = true;
        }
    }

    if (expandedChanged) {
        if (!saveProjectState()) {
            for (SourceGroup& group : groups) {
                if (previousExpanded.contains(group.name)) {
                    group.expanded = previousExpanded.value(group.name);
                }
            }
        } else {
            refreshSourceList();
        }
    }

    const QSignalBlocker blocker(sourcesList_);
    sourcesList_->clearSelection();
    QListWidgetItem* current = nullptr;
    for (int row = 0; row < sourcesList_->count(); ++row) {
        QListWidgetItem* item = sourcesList_->item(row);
        if (
            item != nullptr &&
            !item->data(GroupRowRole).toBool() &&
            sourceNames.contains(item->data(NameRole).toString())) {
            item->setSelected(true);
            current = item;
        }
    }
    sourcesList_->setCurrentItem(current, QItemSelectionModel::NoUpdate);
    if (current != nullptr) {
        sourcesList_->scrollToItem(current, QAbstractItemView::PositionAtCenter);
    }
    refreshSourceRowVisuals();
    updateSourceControls();
    refreshPreviewInteractionOverlay();
}

QHash<QString, Vuttara::SourceTransform> MainWindow::selectedSourceTransforms() const
{
    QHash<QString, Vuttara::SourceTransform> transforms;
    if (engine_ == nullptr || !engine_->isReady()) {
        return transforms;
    }
    const QStringList names = selectedSourceNames();
    for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
        if (names.contains(source.name) && !source.locked) {
            transforms.insert(source.name, source.transform);
        }
    }
    return transforms;
}

bool MainWindow::applyPreviewSourceTransforms(
    const QHash<QString, Vuttara::SourceTransform>& transforms)
{
    if (engine_ == nullptr || !engine_->isReady() || transforms.isEmpty()) {
        return false;
    }

    QHash<QString, Vuttara::SourceTransform> previous;
    for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
        if (transforms.contains(source.name)) {
            previous.insert(source.name, source.transform);
        }
    }

    QStringList applied;
    for (auto iterator = transforms.cbegin(); iterator != transforms.cend(); ++iterator) {
        if (!engine_->setSourceTransform(iterator.key(), iterator.value())) {
            for (const QString& name : applied) {
                engine_->setSourceTransform(name, previous.value(name));
            }
            statusBar()->showMessage(engine_->lastError(), 4500);
            refreshPreviewInteractionOverlay();
            return false;
        }
        applied.append(iterator.key());
    }
    refreshPreviewInteractionOverlay();
    return true;
}

void MainWindow::pushTransformHistory(const TransformHistoryEntry& entry)
{
    if (entry.before.isEmpty() || entry.after.isEmpty()) {
        return;
    }
    transformUndoStack_.append(entry);
    while (transformUndoStack_.size() > 100) {
        transformUndoStack_.removeFirst();
    }
    transformRedoStack_.clear();
    if (undoTransformAction_ != nullptr) undoTransformAction_->setEnabled(true);
    if (redoTransformAction_ != nullptr) redoTransformAction_->setEnabled(false);
}

bool MainWindow::applyTransformEdit(
    const QHash<QString, Vuttara::SourceTransform>& before,
    const QHash<QString, Vuttara::SourceTransform>& after,
    const QString& label,
    bool recordHistory)
{
    if (before.isEmpty() || after.isEmpty() || engine_ == nullptr || !engine_->isReady()) {
        return false;
    }
    if (!applyPreviewSourceTransforms(after)) {
        return false;
    }
    if (!saveProjectState()) {
        applyPreviewSourceTransforms(before);
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Preview Editing"),
            QStringLiteral("The edit could not be saved, so all selected sources were restored."));
        return false;
    }
    if (recordHistory) {
        pushTransformHistory(TransformHistoryEntry{
            engine_->activeSceneName(),
            before,
            after,
            label,
        });
    }
    refreshSourceList();
    updatePreviewInformation();
    refreshPreviewInteractionOverlay();
    statusBar()->showMessage(
        QStringLiteral("%1 saved for %2 source%3 in %4.")
            .arg(label)
            .arg(after.size())
            .arg(after.size() == 1 ? QString{} : QStringLiteral("s"))
            .arg(engine_->activeSceneName()),
        4000);
    return true;
}

void MainWindow::commitPreviewSourceTransforms(
    const QHash<QString, Vuttara::SourceTransform>& originals,
    const QHash<QString, Vuttara::SourceTransform>& current)
{
    if (engine_ == nullptr || !engine_->isReady() || originals.isEmpty()) {
        return;
    }
    if (!saveProjectState()) {
        applyPreviewSourceTransforms(originals);
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Preview Transform"),
            QStringLiteral("The completed preview edit could not be saved, so every affected source was restored."));
        return;
    }
    pushTransformHistory(TransformHistoryEntry{
        engine_->activeSceneName(),
        originals,
        current,
        QStringLiteral("Preview transform"),
    });
    refreshSourceList();
    updatePreviewInformation();
    refreshPreviewInteractionOverlay();
    statusBar()->showMessage(
        QStringLiteral("Saved preview edit for %1 source%2 in %3.")
            .arg(current.size())
            .arg(current.size() == 1 ? QString{} : QStringLiteral("s"))
            .arg(engine_->activeSceneName()),
        4500);
}

void MainWindow::cancelPreviewSourceTransforms(
    const QHash<QString, Vuttara::SourceTransform>& originals)
{
    applyPreviewSourceTransforms(originals);
    refreshSourceList();
    updatePreviewInformation();
    refreshPreviewInteractionOverlay();
}

void MainWindow::undoTransform()
{
    if (transformUndoStack_.isEmpty() || engine_ == nullptr) {
        return;
    }
    const TransformHistoryEntry entry = transformUndoStack_.last();
    if (entry.sceneName != engine_->activeSceneName()) {
        statusBar()->showMessage(QStringLiteral("Switch to %1 to undo that transform.").arg(entry.sceneName), 4500);
        return;
    }
    if (applyTransformEdit(entry.after, entry.before, QStringLiteral("Undo %1").arg(entry.label), false)) {
        transformUndoStack_.removeLast();
        transformRedoStack_.append(entry);
        if (undoTransformAction_ != nullptr) undoTransformAction_->setEnabled(!transformUndoStack_.isEmpty());
        if (redoTransformAction_ != nullptr) redoTransformAction_->setEnabled(true);
    }
}

void MainWindow::redoTransform()
{
    if (transformRedoStack_.isEmpty() || engine_ == nullptr) {
        return;
    }
    const TransformHistoryEntry entry = transformRedoStack_.last();
    if (entry.sceneName != engine_->activeSceneName()) {
        statusBar()->showMessage(QStringLiteral("Switch to %1 to redo that transform.").arg(entry.sceneName), 4500);
        return;
    }
    if (applyTransformEdit(entry.before, entry.after, QStringLiteral("Redo %1").arg(entry.label), false)) {
        transformRedoStack_.removeLast();
        transformUndoStack_.append(entry);
        if (undoTransformAction_ != nullptr) undoTransformAction_->setEnabled(true);
        if (redoTransformAction_ != nullptr) redoTransformAction_->setEnabled(!transformRedoStack_.isEmpty());
    }
}

void MainWindow::nudgeSelectedSources(double dx, double dy)
{
    if (shouldSuppressPreviewInteractionOverlay()) {
        return;
    }
    QWidget* focus = QApplication::focusWidget();
    if (
        qobject_cast<QLineEdit*>(focus) != nullptr ||
        qobject_cast<QComboBox*>(focus) != nullptr ||
        qobject_cast<QAbstractSpinBox*>(focus) != nullptr ||
        qobject_cast<QKeySequenceEdit*>(focus) != nullptr) {
        return;
    }
    QHash<QString, Vuttara::SourceTransform> before = selectedSourceTransforms();
    QHash<QString, Vuttara::SourceTransform> after = before;
    for (auto iterator = after.begin(); iterator != after.end(); ++iterator) {
        iterator->x += dx;
        iterator->y += dy;
    }
    applyTransformEdit(before, after, QStringLiteral("Nudge transform"));
}

void MainWindow::duplicateSelectedSources()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return;
    }
    const QStringList names = selectedSourceNames();
    if (names.isEmpty()) {
        return;
    }

    QStringList createdNames;
    for (const QString& name : names) {
        QString created;
        if (!engine_->duplicateSource(name, &created)) {
            for (const QString& rollbackName : createdNames) {
                engine_->removeSource(rollbackName);
                removeSourceFromGroups(engine_->activeSceneName(), rollbackName);
            }
            QMessageBox::critical(this, QStringLiteral("Duplicate Sources"), engine_->lastError());
            refreshSourceList();
            return;
        }
        createdNames.append(created);
        addSourceToCurrentGroup(created);
    }

    if (!saveProjectState()) {
        for (const QString& name : createdNames) {
            engine_->removeSource(name);
            removeSourceFromGroups(engine_->activeSceneName(), name);
        }
        refreshSourceList();
        QMessageBox::warning(
            this,
            QStringLiteral("Duplicate Sources"),
            QStringLiteral("The duplicated sources could not be saved, so they were removed."));
        return;
    }
    refreshSceneList();
    refreshSourceList(createdNames.isEmpty() ? QString{} : createdNames.last());
    selectSourcesFromPreview(createdNames);
    updatePreviewInformation();
    statusBar()->showMessage(
        QStringLiteral("Duplicated %1 source%2 without interrupting active output.")
            .arg(createdNames.size())
            .arg(createdNames.size() == 1 ? QString{} : QStringLiteral("s")),
        4500);
}

void MainWindow::transformSelectedSources(TransformCommand command)
{
    QHash<QString, Vuttara::SourceTransform> before = selectedSourceTransforms();
    if (before.isEmpty()) {
        return;
    }
    QHash<QString, Vuttara::SourceTransform> after = before;
    QString label;
    for (auto iterator = after.begin(); iterator != after.end(); ++iterator) {
        Vuttara::SourceTransform& transform = iterator.value();
        switch (command) {
        case TransformCommand::Reset:
            transform = Vuttara::SourceTransform{};
            label = QStringLiteral("Reset transform");
            break;
        case TransformCommand::Fit:
            transform.x = 960.0;
            transform.y = 540.0;
            transform.width = 1920.0;
            transform.height = 1080.0;
            transform.rotation = 0.0;
            transform.cropLeft = 0.0;
            transform.cropTop = 0.0;
            transform.cropRight = 0.0;
            transform.cropBottom = 0.0;
            transform.flipHorizontal = false;
            transform.flipVertical = false;
            transform.stretchToBounds = false;
            label = QStringLiteral("Fit to canvas");
            break;
        case TransformCommand::Stretch:
            transform.x = 960.0;
            transform.y = 540.0;
            transform.width = 1920.0;
            transform.height = 1080.0;
            transform.rotation = 0.0;
            transform.cropLeft = 0.0;
            transform.cropTop = 0.0;
            transform.cropRight = 0.0;
            transform.cropBottom = 0.0;
            transform.flipHorizontal = false;
            transform.flipVertical = false;
            transform.stretchToBounds = true;
            label = QStringLiteral("Stretch to canvas");
            break;
        case TransformCommand::Center:
            transform.x = 960.0;
            transform.y = 540.0;
            label = QStringLiteral("Center to screen");
            break;
        case TransformCommand::CenterHorizontal:
            transform.x = 960.0;
            label = QStringLiteral("Center horizontally");
            break;
        case TransformCommand::CenterVertical:
            transform.y = 540.0;
            label = QStringLiteral("Center vertically");
            break;
        case TransformCommand::FlipHorizontal:
            transform.flipHorizontal = !transform.flipHorizontal;
            label = QStringLiteral("Flip horizontal");
            break;
        case TransformCommand::FlipVertical:
            transform.flipVertical = !transform.flipVertical;
            label = QStringLiteral("Flip vertical");
            break;
        case TransformCommand::RotateClockwise:
            transform.rotation += 90.0;
            label = QStringLiteral("Rotate 90 degrees clockwise");
            break;
        case TransformCommand::RotateCounterclockwise:
            transform.rotation -= 90.0;
            label = QStringLiteral("Rotate 90 degrees counterclockwise");
            break;
        }
    }
    applyTransformEdit(before, after, label);
}


void MainWindow::showPreviewSourceContextMenu(
    const QString& sourceName,
    const QPoint& globalPosition)
{
    if (!sourceName.isEmpty() && !selectedSourceNames().contains(sourceName)) {
        selectSourcesFromPreview(QStringList{sourceName});
    }
    QListWidgetItem* item = nullptr;
    if (!sourceName.isEmpty() && sourcesList_ != nullptr) {
        for (int row = 0; row < sourcesList_->count(); ++row) {
            QListWidgetItem* candidate = sourcesList_->item(row);
            if (candidate != nullptr && candidate->data(NameRole).toString() == sourceName) {
                item = candidate;
                sourcesList_->setCurrentItem(candidate, QItemSelectionModel::NoUpdate);
                refreshSourceRowVisuals();
                break;
            }
        }
    }
    showSourceContextMenuAtGlobal(globalPosition, item);
}


void MainWindow::refreshSourceList(const QString& preferredSelection)
{
    if (sourcesList_ == nullptr) {
        return;
    }

    QStringList selections = selectedSourceNames();
    QString preferredGroup;
    if (
        preferredSelection.isEmpty() &&
        sourcesList_->currentItem() != nullptr &&
        sourcesList_->currentItem()->data(GroupRowRole).toBool()) {
        preferredGroup = sourcesList_->currentItem()->data(GroupNameRole).toString();
    }
    if (!preferredSelection.isEmpty()) {
        selections = {preferredSelection};
        preferredGroup.clear();
    }

    const QSignalBlocker blocker(sourcesList_);
    sourcesList_->clear();

    if (engine_ == nullptr || !engine_->isReady()) {
        updateSourceControls();
        return;
    }

    pruneSourceGroupsForActiveScene();
    const QVector<Vuttara::SourceInfo> sources = engine_->sourceInfos();
    QVector<SourceGroup>& groups = sourceGroupsByScene_[engine_->activeSceneName()];

    QListWidgetItem* current = nullptr;
    QListWidgetItem* firstSourceItem = nullptr;

    const auto sourceTooltip = [](const Vuttara::SourceInfo& source) {
        return QStringLiteral(
            "Order %1 | X %2 Y %3 | %4 × %5 | Rotation %6° | Crop L%7 T%8 R%9 B%10 | Flip H%11 V%12")
            .arg(source.orderPosition)
            .arg(source.transform.x, 0, 'f', 1)
            .arg(source.transform.y, 0, 'f', 1)
            .arg(source.transform.width, 0, 'f', 1)
            .arg(source.transform.height, 0, 'f', 1)
            .arg(source.transform.rotation, 0, 'f', 1)
            .arg(source.transform.cropLeft, 0, 'f', 0)
            .arg(source.transform.cropTop, 0, 'f', 0)
            .arg(source.transform.cropRight, 0, 'f', 0)
            .arg(source.transform.cropBottom, 0, 'f', 0)
            .arg(source.transform.flipHorizontal)
            .arg(source.transform.flipVertical);
    };

    const auto addSourceRow = [&](const Vuttara::SourceInfo& source, const QString& groupName) {
        auto* item = new QListWidgetItem(sourcesList_);
        item->setData(NameRole, source.name);
        item->setData(TypeRole, source.type);
        item->setData(RemovableRole, source.removable);
        item->setData(LockedRole, source.locked);
        item->setData(GroupNameRole, groupName);
        item->setData(GroupRowRole, false);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setSizeHint(QSize(0, 31));
        item->setToolTip(sourceTooltip(source));

        const bool cropped =
            source.transform.cropLeft > 0.5 || source.transform.cropTop > 0.5 ||
            source.transform.cropRight > 0.5 || source.transform.cropBottom > 0.5;
        auto* rowWidget = new SourceDockRow(
            source.name,
            source.type,
            false,
            true,
            source.visible,
            source.locked,
            cropped,
            sourcesList_);
        rowWidget->setCallbacks(
            [this, item](Qt::KeyboardModifiers modifiers) {
                selectSourceDockItem(item, modifiers);
            },
            [this, item]() {
                startSourceDockDrag(item);
            },
            {},
            [this, sourceName = source.name, visible = source.visible]() {
                QTimer::singleShot(0, this, [this, sourceName, visible]() {
                    setSourceVisibility(QStringList{sourceName}, !visible);
                });
            },
            [this, sourceName = source.name, locked = source.locked]() {
                QTimer::singleShot(0, this, [this, sourceName, locked]() {
                    setSourceLockState(QStringList{sourceName}, !locked);
                });
            },
            [this, sourceName = source.name]() {
                QTimer::singleShot(0, this, [this, sourceName]() {
                    selectSourcesFromPreview(QStringList{sourceName});
                    showSelectedSourceProperties();
                });
            },
            [this, sourceName = source.name](const QPoint& globalPosition) {
                QTimer::singleShot(0, this, [this, sourceName, globalPosition]() {
                    selectSourcesFromPreview(QStringList{sourceName});
                    if (sourcesList_ == nullptr) {
                        return;
                    }
                    for (int row = 0; row < sourcesList_->count(); ++row) {
                        QListWidgetItem* candidate = sourcesList_->item(row);
                        if (
                            candidate != nullptr &&
                            !candidate->data(GroupRowRole).toBool() &&
                            candidate->data(NameRole).toString() == sourceName) {
                            showSourceContextMenuAtGlobal(globalPosition, candidate);
                            return;
                        }
                    }
                });
            });
        sourcesList_->setItemWidget(item, rowWidget);

        if (firstSourceItem == nullptr) {
            firstSourceItem = item;
        }
        if (selections.contains(source.name)) {
            item->setSelected(true);
            current = item;
        }
    };

    const auto addGroupRow = [&](SourceGroup& group) {
        QStringList validNames;
        bool allVisible = true;
        bool allLocked = true;
        for (const Vuttara::SourceInfo& source : sources) {
            if (!group.sourceNames.contains(source.name)) {
                continue;
            }
            validNames.append(source.name);
            allVisible = allVisible && source.visible;
            allLocked = allLocked && source.locked;
        }
        if (validNames.isEmpty()) {
            return;
        }

        auto* groupItem = new QListWidgetItem(sourcesList_);
        groupItem->setData(NameRole, QString{});
        groupItem->setData(GroupNameRole, group.name);
        groupItem->setData(GroupRowRole, true);
        groupItem->setData(LockedRole, allLocked);
        groupItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        groupItem->setSizeHint(QSize(0, 33));
        groupItem->setToolTip(QStringLiteral("%1 source%2 in this folder")
                                  .arg(validNames.size())
                                  .arg(validNames.size() == 1 ? QString{} : QStringLiteral("s")));

        auto* rowWidget = new SourceDockRow(
            group.name,
            QStringLiteral("group"),
            true,
            group.expanded,
            allVisible,
            allLocked,
            false,
            sourcesList_);
        rowWidget->setCallbacks(
            [this, groupItem](Qt::KeyboardModifiers modifiers) {
                selectSourceDockItem(groupItem, modifiers);
            },
            [this, groupItem]() {
                startSourceDockDrag(groupItem);
            },
            [this, groupName = group.name]() {
                QTimer::singleShot(0, this, [this, groupName]() {
                    toggleSourceGroupExpanded(groupName);
                });
            },
            [this, validNames, allVisible]() {
                QTimer::singleShot(0, this, [this, validNames, allVisible]() {
                    setSourceVisibility(validNames, !allVisible);
                });
            },
            [this, validNames, allLocked]() {
                QTimer::singleShot(0, this, [this, validNames, allLocked]() {
                    setSourceLockState(validNames, !allLocked);
                });
            },
            [this, groupName = group.name]() {
                QTimer::singleShot(0, this, [this, groupName]() {
                    toggleSourceGroupExpanded(groupName);
                });
            },
            [this, groupName = group.name](const QPoint& globalPosition) {
                QTimer::singleShot(0, this, [this, groupName, globalPosition]() {
                    if (sourcesList_ == nullptr) {
                        return;
                    }
                    for (int row = 0; row < sourcesList_->count(); ++row) {
                        QListWidgetItem* candidate = sourcesList_->item(row);
                        if (
                            candidate != nullptr &&
                            candidate->data(GroupRowRole).toBool() &&
                            candidate->data(GroupNameRole).toString() == groupName) {
                            selectSourceDockItem(candidate, Qt::NoModifier);
                            showSourceContextMenuAtGlobal(globalPosition, candidate);
                            return;
                        }
                    }
                });
            });
        sourcesList_->setItemWidget(groupItem, rowWidget);

        if (preferredGroup == group.name) {
            groupItem->setSelected(true);
            current = groupItem;
        }

        if (group.expanded) {
            for (const Vuttara::SourceInfo& source : sources) {
                if (group.sourceNames.contains(source.name)) {
                    addSourceRow(source, group.name);
                }
            }
        }
    };

    QHash<QString, int> groupIndexBySource;
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        for (const QString& sourceName : groups.at(groupIndex).sourceNames) {
            groupIndexBySource.insert(sourceName, groupIndex);
        }
    }

    QSet<int> emittedGroups;
    for (const Vuttara::SourceInfo& source : sources) {
        const auto groupIndex = groupIndexBySource.constFind(source.name);
        if (groupIndex == groupIndexBySource.cend()) {
            addSourceRow(source, QString{});
            continue;
        }
        if (!emittedGroups.contains(groupIndex.value())) {
            emittedGroups.insert(groupIndex.value());
            addGroupRow(groups[groupIndex.value()]);
        }
    }

    if (current != nullptr) {
        sourcesList_->setCurrentItem(current, QItemSelectionModel::NoUpdate);
    } else if (firstSourceItem != nullptr) {
        firstSourceItem->setSelected(true);
        sourcesList_->setCurrentItem(firstSourceItem, QItemSelectionModel::NoUpdate);
    }

    refreshSourceRowVisuals();
    updateSourceControls();
    refreshPreviewInteractionOverlay();
}

void MainWindow::updateSourceControls()
{
    const QStringList names = selectedSourceNames();
    const bool hasSource = !names.isEmpty();
    const bool groupItemSelected =
        sourcesList_ != nullptr &&
        sourcesList_->currentItem() != nullptr &&
        sourcesList_->currentItem()->data(GroupRowRole).toBool();
    bool anyRemovable = false;
    bool anyLocked = false;
    if (engine_ != nullptr && engine_->isReady()) {
        for (const Vuttara::SourceInfo& source : engine_->sourceInfos()) {
            if (names.contains(source.name)) {
                anyRemovable = anyRemovable || source.removable;
                anyLocked = anyLocked || source.locked;
            }
        }
    }

    if (removeSourceButton_ != nullptr) removeSourceButton_->setEnabled(anyRemovable);
    if (sourceActionsButton_ != nullptr) sourceActionsButton_->setEnabled(true);
    if (sourcePropertiesAction_ != nullptr) sourcePropertiesAction_->setEnabled(names.size() == 1 && !groupItemSelected);
    if (sourceTransformAction_ != nullptr) sourceTransformAction_->setEnabled(names.size() == 1 && !anyLocked && !groupItemSelected);
    if (duplicateSourceAction_ != nullptr) duplicateSourceAction_->setEnabled(hasSource);
    if (fitSourceAction_ != nullptr) fitSourceAction_->setEnabled(hasSource && !anyLocked);
    if (centerSourceAction_ != nullptr) centerSourceAction_->setEnabled(hasSource && !anyLocked);
    if (lockSourceAction_ != nullptr) {
        lockSourceAction_->setEnabled(hasSource);
        lockSourceAction_->setText(anyLocked ? QStringLiteral("Unlock Selected Sources") : QStringLiteral("Lock Selected Sources"));
    }
    if (removeSourceAction_ != nullptr) removeSourceAction_->setEnabled(anyRemovable);
    if (undoTransformAction_ != nullptr) undoTransformAction_->setEnabled(!transformUndoStack_.isEmpty());
    if (redoTransformAction_ != nullptr) redoTransformAction_->setEnabled(!transformRedoStack_.isEmpty());

    if (engine_ != nullptr && engine_->isReady()) {
        if (addDisplaySourceAction_ != nullptr) addDisplaySourceAction_->setEnabled(!engine_->hasDisplayCapture());
        if (addWindowSourceAction_ != nullptr) addWindowSourceAction_->setEnabled(!engine_->hasWindowCapture());
    }
}



void MainWindow::updatePreviewInformation()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        previewTitle_->setText(QStringLiteral("Preview Unavailable"));
        previewInformation_->setText(QStringLiteral("libobs did not initialize. Review the startup log."));
        return;
    }

    const QString sceneName = engine_->activeSceneName();
    const QVector<Vuttara::SourceInfo> sources = engine_->sourceInfos();
    const int visibleCount = static_cast<int>(std::count_if(
        sources.cbegin(),
        sources.cend(),
        [](const Vuttara::SourceInfo& source) {
            return source.visible;
        }));

    previewTitle_->setText(QStringLiteral("%1 Preview").arg(sceneName));
    previewInformation_->setText(QStringLiteral(
        "%1 scene%2  •  %3 source%4  •  %5 visible  •  1920 × 1080 @ 60 FPS  •  Local project")
                                     .arg(engine_->sceneInfos().size())
                                     .arg(engine_->sceneInfos().size() == 1 ? QString{} : QStringLiteral("s"))
                                     .arg(sources.size())
                                     .arg(sources.size() == 1 ? QString{} : QStringLiteral("s"))
                                     .arg(visibleCount));
    previewInformation_->setToolTip(QStringLiteral("Active scene: %1\nProject: %2")
                                        .arg(sceneName, Vuttara::AppPaths::projectStatePath()));
    refreshPreviewInteractionOverlay();
}

bool MainWindow::loadProjectState()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return false;
    }

    QFile file(Vuttara::AppPaths::projectStatePath());
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(QStringLiteral("Could not open the saved project; using the default project."), 6000);
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        statusBar()->showMessage(QStringLiteral("The saved project is invalid; using the default project."), 6000);
        return false;
    }

    if (!engine_->restoreProjectState(document.object())) {
        statusBar()->showMessage(QStringLiteral("Project restore failed: %1").arg(engine_->lastError()), 8000);
        engine_->resetProjectToDefault();
        return false;
    }

    restoreSourceGroups(document.object());
    return true;
}

bool MainWindow::saveProjectState()
{
    if (engine_ == nullptr || !engine_->isReady()) {
        return false;
    }

    QSaveFile file(Vuttara::AppPaths::projectStatePath());
    if (!file.open(QIODevice::WriteOnly)) {
        statusBar()->showMessage(QStringLiteral("Could not save the local project."), 6000);
        return false;
    }

    QJsonObject project = engine_->projectState();
    project.insert(QStringLiteral("sourceGroups"), sourceGroupsState());
    project.insert(
        QStringLiteral("uiStage"),
        QStringLiteral("sources-organization-preview-selection-stage8c-v1-fix3"));

    const QJsonDocument document(project);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        statusBar()->showMessage(QStringLiteral("Could not commit the local project file."), 6000);
        return false;
    }

    return true;
}

void MainWindow::migrateLegacyCaptureSettings()
{
    restoreLegacyDisplayCapture();
    restoreLegacyWindowCapture();

    QSettings settings;
    settings.remove(QStringLiteral("captureSourcesStage3A"));
    settings.remove(QStringLiteral("captureSourcesStage3BWindow"));
    settings.sync();
}

void MainWindow::restoreLegacyDisplayCapture()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("captureSourcesStage3A"));
    const bool enabled = settings.value(QStringLiteral("enabled"), false).toBool();
    const QString monitorId = settings.value(QStringLiteral("monitorId")).toString();
    const QString sourceName = settings.value(QStringLiteral("sourceName"), QStringLiteral("Display Capture")).toString();
    const bool captureCursor = settings.value(QStringLiteral("captureCursor"), true).toBool();
    const bool visible = settings.value(QStringLiteral("visible"), true).toBool();
    settings.endGroup();

    if (!enabled || monitorId.isEmpty() || engine_->hasDisplayCapture()) {
        return;
    }

    const QVector<Vuttara::DisplayInfo> displays = engine_->availableDisplays();
    const auto display = std::find_if(
        displays.cbegin(),
        displays.cend(),
        [&monitorId](const Vuttara::DisplayInfo& candidate) {
            return candidate.monitorId == monitorId;
        });
    if (display == displays.cend()) {
        return;
    }

    QString createdName;
    if (engine_->addDisplayCapture(*display, captureCursor, sourceName, &createdName)) {
        engine_->setSourceVisible(createdName, visible);
    }
}

void MainWindow::restoreLegacyWindowCapture()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("captureSourcesStage3BWindow"));
    const bool enabled = settings.value(QStringLiteral("enabled"), false).toBool();
    const QString encodedValue = settings.value(QStringLiteral("encodedValue")).toString();
    const QString sourceName = settings.value(QStringLiteral("sourceName"), QStringLiteral("Window Capture")).toString();
    const int method = settings.value(QStringLiteral("method"), 0).toInt();
    const int priority = settings.value(QStringLiteral("priority"), 1).toInt();
    const bool captureCursor = settings.value(QStringLiteral("captureCursor"), true).toBool();
    const bool clientArea = settings.value(QStringLiteral("clientArea"), true).toBool();
    const bool visible = settings.value(QStringLiteral("visible"), true).toBool();
    settings.endGroup();

    if (!enabled || encodedValue.isEmpty() || engine_->hasWindowCapture()) {
        return;
    }

    Vuttara::WindowInfo window;
    window.encodedValue = encodedValue;
    window.description = QStringLiteral("saved application window");
    QString createdName;
    if (engine_->addWindowCapture(
            window,
            windowMethodFromIndex(method),
            windowPriorityFromIndex(priority),
            captureCursor,
            clientArea,
            sourceName,
            &createdName)) {
        engine_->setSourceVisible(createdName, visible);
    }
}

QString MainWindow::selectedSceneName() const
{
    const QListWidgetItem* item = scenesList_ != nullptr ? scenesList_->currentItem() : nullptr;
    return item != nullptr ? item->data(NameRole).toString() : QString{};
}

QString MainWindow::selectedSourceName() const
{
    const QListWidgetItem* item = sourcesList_ != nullptr ? sourcesList_->currentItem() : nullptr;
    return item != nullptr ? item->data(NameRole).toString() : QString{};
}
