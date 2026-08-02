#include "PreviewWidget.hpp"

#include "engine/VuttaraEngine.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>
#include <cmath>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace {

#ifdef Q_OS_WIN
Qt::KeyboardModifiers nativeKeyboardModifiers()
{
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= Qt::ShiftModifier;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= Qt::ControlModifier;
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers |= Qt::AltModifier;
    }
    return modifiers;
}

Qt::MouseButtons nativeMouseButtons(WPARAM flags)
{
    Qt::MouseButtons buttons = Qt::NoButton;
    if ((flags & MK_LBUTTON) != 0) {
        buttons |= Qt::LeftButton;
    }
    if ((flags & MK_RBUTTON) != 0) {
        buttons |= Qt::RightButton;
    }
    if ((flags & MK_MBUTTON) != 0) {
        buttons |= Qt::MiddleButton;
    }
    if ((flags & MK_XBUTTON1) != 0) {
        buttons |= Qt::BackButton;
    }
    if ((flags & MK_XBUTTON2) != 0) {
        buttons |= Qt::ForwardButton;
    }
    return buttons;
}
#endif

} // namespace

PreviewWidget::PreviewWidget(Vuttara::VuttaraEngine* engine, QWidget* parent)
    : QWidget(parent)
    , engine_(engine)
{
    setObjectName(QStringLiteral("obsPreview"));
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setMinimumSize(640, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

PreviewWidget::~PreviewWidget()
{
    if (attached_ && engine_ != nullptr) {
        engine_->detachPreview();
    }
}

QPaintEngine* PreviewWidget::paintEngine() const
{
    return nullptr;
}

void PreviewWidget::setInteractionEventHandler(InteractionEventHandler handler)
{
    interactionEventHandler_ = std::move(handler);
}

bool PreviewWidget::dispatchInteractionEventForTest(QEvent* event)
{
    return dispatchInteractionEvent(event);
}

void PreviewWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    attachOrResize();
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    attachOrResize();
}

bool PreviewWidget::nativeEvent(
    const QByteArray& eventType,
    void* message,
    qintptr* result)
{
#ifdef Q_OS_WIN
    auto* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage != nullptr) {
        QEvent::Type mouseType = QEvent::None;
        Qt::MouseButton button = Qt::NoButton;
        switch (nativeMessage->message) {
        case WM_LBUTTONDOWN:
            mouseType = QEvent::MouseButtonPress;
            button = Qt::LeftButton;
            break;
        case WM_LBUTTONUP:
            mouseType = QEvent::MouseButtonRelease;
            button = Qt::LeftButton;
            break;
        case WM_LBUTTONDBLCLK:
            mouseType = QEvent::MouseButtonDblClick;
            button = Qt::LeftButton;
            break;
        case WM_MOUSEMOVE:
            mouseType = QEvent::MouseMove;
            break;
        default:
            break;
        }

        if (mouseType != QEvent::None && interactionEventHandler_) {
            const qreal pixelRatio = std::max<qreal>(1.0, devicePixelRatioF());
            const QPointF localPosition(
                GET_X_LPARAM(nativeMessage->lParam) / pixelRatio,
                GET_Y_LPARAM(nativeMessage->lParam) / pixelRatio);
            const QPointF globalPosition(mapToGlobal(localPosition.toPoint()));
            Qt::MouseButtons buttons = nativeMouseButtons(nativeMessage->wParam);
            if (mouseType == QEvent::MouseButtonPress || mouseType == QEvent::MouseButtonDblClick) {
                buttons |= button;
            } else if (mouseType == QEvent::MouseButtonRelease) {
                buttons &= ~button;
            }
            QMouseEvent forwarded(
                mouseType,
                localPosition,
                globalPosition,
                button,
                buttons,
                nativeKeyboardModifiers());
            dispatchInteractionEvent(&forwarded);
            if (result != nullptr) {
                *result = 0;
            }
            return true;
        }

        if (nativeMessage->message == WM_CONTEXTMENU && interactionEventHandler_) {
            POINT screenPoint{};
            if (nativeMessage->lParam == static_cast<LPARAM>(-1)) {
                GetCursorPos(&screenPoint);
            } else {
                screenPoint.x = GET_X_LPARAM(nativeMessage->lParam);
                screenPoint.y = GET_Y_LPARAM(nativeMessage->lParam);
            }
            POINT clientPoint = screenPoint;
            ScreenToClient(nativeMessage->hwnd, &clientPoint);
            const qreal pixelRatio = std::max<qreal>(1.0, devicePixelRatioF());
            const QPoint localPosition(
                static_cast<int>(std::lround(clientPoint.x / pixelRatio)),
                static_cast<int>(std::lround(clientPoint.y / pixelRatio)));
            QContextMenuEvent forwarded(
                QContextMenuEvent::Mouse,
                localPosition,
                mapToGlobal(localPosition),
                nativeKeyboardModifiers());
            dispatchInteractionEvent(&forwarded);
            if (result != nullptr) {
                *result = 0;
            }
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

bool PreviewWidget::dispatchInteractionEvent(QEvent* event)
{
    return event != nullptr && interactionEventHandler_ && interactionEventHandler_(event);
}

void PreviewWidget::attachOrResize()
{
    if (engine_ == nullptr || !engine_->isReady() || !isVisible()) {
        return;
    }

    const qreal ratio = devicePixelRatioF();
    const auto pixelWidth = static_cast<std::uint32_t>(
        std::max(1, static_cast<int>(std::lround(width() * ratio))));
    const auto pixelHeight = static_cast<std::uint32_t>(
        std::max(1, static_cast<int>(std::lround(height() * ratio))));

    if (!attached_) {
        attached_ = engine_->attachPreview(
            reinterpret_cast<void*>(winId()), pixelWidth, pixelHeight);
    } else {
        engine_->resizePreview(pixelWidth, pixelHeight);
    }
}
