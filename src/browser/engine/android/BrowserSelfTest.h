#pragma once

#include <QObject>

class QQmlEngine;
class QQuickWindow;

namespace fortiq::engine {

// DEVELOPER-ONLY (FORTIQ_ANDROID_BROWSER_SELFTEST, never in a shipped build): started by `--browser-selftest`, it runs the Android Browser
// engine against the disposable fixture so that tool/android_browser_check.py can prove, from outside the app, that every connection of the
// application's uid goes through the gateway. Input: <files>/browser-selftest.json (host, port, user, keyFile, fingerprint, base),
// the throw-away key in <files>; commands: <files>/browser-selftest-cmd.txt; output: <files>/browser-selftest-report.txt (key=value lines).
// It does what the user's window does (EngineBrowser + the real EngineView) without needing taps.
class BrowserSelfTest : public QObject {
    Q_OBJECT
public:
    BrowserSelfTest(QQmlEngine *engine, QQuickWindow *window, QObject *parent = nullptr);
    ~BrowserSelfTest() override;
    void start();

private:
    struct Impl;
    Impl *d;
};

}  // namespace fortiq::engine
