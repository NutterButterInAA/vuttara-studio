#pragma once

#include <QByteArray>
#include <QWidget>

#include <functional>

namespace Vuttara {
class VuttaraEngine;
}

class QEvent;
class QPaintEngine;
class QResizeEvent;
class QShowEvent;

class PreviewWidget final : public QWidget
{
    Q_OBJECT

public:
    using InteractionEventHandler = std::function<bool(QEvent*)>;

    explicit PreviewWidget(Vuttara::VuttaraEngine* engine, QWidget* parent = nullptr);
    ~PreviewWidget() override;

    QPaintEngine* paintEngine() const override;
    void setInteractionEventHandler(InteractionEventHandler handler);
    bool dispatchInteractionEventForTest(QEvent* event);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    bool dispatchInteractionEvent(QEvent* event);
    void attachOrResize();

    Vuttara::VuttaraEngine* engine_ = nullptr;
    InteractionEventHandler interactionEventHandler_;
    bool attached_ = false;
};
