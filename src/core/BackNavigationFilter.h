#pragma once

#include <QKeyEvent>
#include <QObject>
#include <functional>
#include <utility>

namespace fortiq {

// Android backgrounds an Activity when its Qt window leaves Back unaccepted.
// Route the release at the window, regardless of the focused QML control.
class BackNavigationFilter final : public QObject {
public:
    BackNavigationFilter(QObject *window, std::function<bool()> navigate)
        : QObject(window), navigate_(std::move(navigate))
    {
        window->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::KeyRelease) return false;
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Back || key->isAutoRepeat()) return false;
        if (!navigate_()) return false; // Allow Android's normal background action.
        event->accept();
        return true;
    }

private:
    std::function<bool()> navigate_;
};

} // namespace fortiq
