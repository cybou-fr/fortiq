package fr.cybou.fortiq;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.net.http.SslError;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.webkit.ConsoleMessage;
import android.webkit.CookieManager;
import android.webkit.GeolocationPermissions;
import android.webkit.HttpAuthHandler;
import android.webkit.JsPromptResult;
import android.webkit.JsResult;
import android.webkit.PermissionRequest;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.SslErrorHandler;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebStorage;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

import androidx.webkit.ProxyConfig;
import androidx.webkit.ProxyController;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;

import java.io.ByteArrayInputStream;
import java.util.Collections;
import java.util.Set;
import java.util.concurrent.Executor;

/**
 * The Browser engine of FORTIQ on Android: the system WebView (Chromium) whose ONLY way out is the loopback gateway of the selected
 * server (docs/BROWSER.md, "Android"). Everything here follows the same contract as the Qt WebEngine view on the desktop:
 *   - the proxy override (ProxyController) is applied and CONFIRMED before the first WebView of the process exists; if the WebView
 *     cannot do it (feature missing) nothing is created: there is no direct fallback;
 *   - the override is never cleared while the process lives: when the gateway goes away the WebView hits a closed port ("proxy
 *     unreachable"), never the open network;
 *   - no downloads, no permissions, no dialogs, no file chooser, no new windows, http(s) pages only, WebRTC/WebTransport removed
 *     from every frame before any page script, Safe Browsing off (it would contact Google), nothing kept on the device afterwards.
 * C++ (src/browser/engine/android/AndroidWebView.cpp) drives it; all WebView calls run on the UI thread. The pure rules (proxy plan,
 * URL judgement, credentials check) stay in C++ and are unit-tested there.
 */
public final class FortiqWeb {
    // Events towards C++ (kept in sync with AndroidWebView.cpp)
    static final int EV_READY = 1, EV_FAILED = 2, EV_STARTED = 3, EV_FINISHED = 4, EV_PROGRESS = 5, EV_TITLE = 6, EV_URL = 7,
            EV_ERROR = 8, EV_REFUSED = 9, EV_HISTORY = 10, EV_DOWNLOAD = 11, EV_CERT = 12;

    private static final Handler UI = new Handler(Looper.getMainLooper());
    private static WebView view;
    private static boolean proxyConfirmed;
    private static int epoch;
    // The foreground service follows the view with a delay: switching tabs destroys one WebView and creates the next within milliseconds,
    // and stopping the service before it has called startForeground() kills the app (ForegroundServiceDidNotStartInTimeException).
    private static boolean serviceWanted, serviceStarted;
    private static final long SERVICE_STOP_DELAY_MS = 4000;
    private static final Runnable STOP_SERVICE = new Runnable() {
        public void run() {
            if (serviceWanted || !serviceStarted) return;
            serviceStarted = false;
            Activity activity = activity();
            if (activity != null) {
                try { activity.stopService(new Intent(activity, GatewayService.class)); } catch (Throwable ignored) { /* already stopped */ }
            }
        }
    };

    private FortiqWeb() {}

    // ── natives (C++) ───────────────────────────────────────────────────────────────────────────────────────────────────────────
    /** "" when the navigation may proceed, otherwise the user-facing reason (src/browser/engine/EngineProfile.cpp judgeUrl). */
    static native String nativeJudge(String url, boolean topLevel);
    /** {user, password} for a 401/407 challenge of host/realm, or null (never for an origin server). */
    static native String[] nativeCredentials(String host, String realm);
    static native void nativeEvent(int kind, String a, String b, int n);

    private static void event(int kind, String a, String b, int n) {
        try { nativeEvent(kind, a, b, n); } catch (Throwable ignored) { /* C++ side not attached (yet/anymore) */ }
    }

    private static Activity host;   // the Qt activity, handed over by C++ (QNativeInterface::QAndroidApplication::context())

    private static Activity activity() { return host; }

    // ── lifecycle ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
    /**
     * Checks the WebView, applies the proxy override (rule + bypass list from C++), waits for the confirmation, then creates the view.
     * Result: EV_READY or EV_FAILED(reason).
     */
    public static void start(final Activity activity, final String proxyRule, final String[] bypass, final String script, final long timeoutMs) {
        UI.post(new Runnable() { public void run() { host = activity; startOnUi(proxyRule, bypass, script, timeoutMs); } });
    }

    private static void fail(String reason) { event(EV_FAILED, reason, "", 0); }

    private static void startOnUi(String proxyRule, String[] bypass, String script, long timeoutMs) {
        final Activity activity = activity();
        if (activity == null) { fail("Activity Android indisponible."); return; }
        if (view != null) destroyOnUi();
        final int myEpoch = ++epoch;
        boolean installed = false, override = false, docScript = false;
        try {
            installed = WebViewCompat.getCurrentWebViewPackage(activity) != null;
            override = installed && WebViewFeature.isFeatureSupported(WebViewFeature.PROXY_OVERRIDE);
            docScript = installed && WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT);
        } catch (Throwable t) { /* leaves the flags false */ }
        // The same refusal text as C++ (androidSupportRefusal) is produced there: only the three facts travel.
        if (!installed || !override || !docScript) {
            event(EV_FAILED, "support:" + installed + ":" + override + ":" + docScript, "", 0);
            return;
        }
        ProxyConfig.Builder builder = new ProxyConfig.Builder().addProxyRule(proxyRule);   // no "direct" rule: ever
        for (String rule : bypass) builder.addBypassRule(rule);
        final ProxyConfig config = builder.build();
        final boolean[] answered = new boolean[1];
        final Runnable timeout = new Runnable() {
            public void run() { if (!answered[0] && myEpoch == epoch) { answered[0] = true; fail("Le WebView n'a pas confirmé le proxy : le navigateur ne démarre pas."); } }
        };
        UI.postDelayed(timeout, timeoutMs);
        final Executor onUi = new Executor() { public void execute(Runnable r) { UI.post(r); } };
        ProxyController.getInstance().setProxyOverride(config, onUi, new Runnable() {
            public void run() {
                if (answered[0] || myEpoch != epoch) return;
                answered[0] = true;
                UI.removeCallbacks(timeout);
                proxyConfirmed = true;
                try { createView(activity, script); }
                catch (Throwable t) { fail("Création du WebView impossible : " + t.getClass().getSimpleName()); return; }
                event(EV_READY, "", "", 0);
            }
        });
    }

    private static void createView(Activity activity, String script) {
        if (!proxyConfirmed) throw new IllegalStateException("proxy not confirmed");
        // Services of the Chromium in the app process that would call out: nothing is allowed beyond the proxy.
        WebView.setWebContentsDebuggingEnabled(false);
        final WebView web = new WebView(activity);
        web.setBackgroundColor(0xFF101828);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setJavaScriptCanOpenWindowsAutomatically(false);
        s.setSupportMultipleWindows(false);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setAllowFileAccessFromFileURLs(false);
        s.setAllowUniversalAccessFromFileURLs(false);
        s.setGeolocationEnabled(false);
        s.setSaveFormData(false);
        s.setMediaPlaybackRequiresUserGesture(true);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        s.setSafeBrowsingEnabled(false);
        s.setBuiltInZoomControls(true);
        s.setDisplayZoomControls(false);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        s.setCacheMode(WebSettings.LOAD_DEFAULT);
        CookieManager cookies = CookieManager.getInstance();
        cookies.setAcceptCookie(true);
        cookies.setAcceptThirdPartyCookies(web, false);
        // Nothing of a previous run (this process or an earlier one) survives.
        wipe(web);
        // WebRTC would send UDP around the proxy; there is no WebView setting for it, so the API is removed from every frame.
        Set<String> origins = Collections.singleton("*");
        WebViewCompat.addDocumentStartJavaScript(web, script, origins);

        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
                String reason = nativeJudge(r.getUrl().toString(), r.isForMainFrame());
                if (reason.isEmpty()) return false;
                if (r.isForMainFrame()) event(EV_REFUSED, reason, "", 0);
                return true;
            }
            @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) {
                // Sub-resources of unwanted schemes (file:, content:, ftp:, ...) never leave the engine.
                if (nativeJudge(r.getUrl().toString(), false).isEmpty()) return null;
                return new WebResourceResponse("text/plain", "utf-8", 403, "Refused", Collections.<String, String>emptyMap(),
                        new ByteArrayInputStream(new byte[0]));
            }
            @Override public void onPageStarted(WebView v, String url, Bitmap icon) { event(EV_STARTED, url, "", 0); history(v); }
            @Override public void onPageFinished(WebView v, String url) { event(EV_FINISHED, url, v.getTitle() == null ? "" : v.getTitle(), 0); history(v); }
            @Override public void doUpdateVisitedHistory(WebView v, String url, boolean isReload) { event(EV_URL, url, "", 0); history(v); }
            @Override public void onReceivedError(WebView v, WebResourceRequest r, WebResourceError e) {
                if (r.isForMainFrame()) event(EV_ERROR, String.valueOf(e.getDescription()), String.valueOf(e.getErrorCode()), 0);
            }
            @Override public void onReceivedSslError(WebView v, SslErrorHandler h, SslError e) {
                h.cancel();
                event(EV_CERT, "Certificat refusé (" + e.getPrimaryError() + ")", "", 0);
            }
            @Override public void onReceivedHttpAuthRequest(WebView v, HttpAuthHandler h, String host, String realm) {
                String[] c = nativeCredentials(host, realm);
                if (c != null && c.length == 2) h.proceed(c[0], c[1]); else h.cancel();
            }
            @Override public boolean onRenderProcessGone(WebView v, RenderProcessGoneDetail d) {
                event(EV_ERROR, "Le moteur de la page s'est arrêté (mémoire ?).", "render-gone", 0);
                return true;   // the app survives; the view is recreated by the next open
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override public void onProgressChanged(WebView v, int p) { event(EV_PROGRESS, "", "", p); }
            @Override public void onReceivedTitle(WebView v, String t) { event(EV_TITLE, t == null ? "" : t, "", 0); }
            @Override public void onPermissionRequest(PermissionRequest r) { r.deny(); event(EV_REFUSED, "refus", "permission", 0); }
            @Override public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback cb) { cb.invoke(origin, false, false); event(EV_REFUSED, "refus", "permission", 0); }
            @Override public boolean onJsAlert(WebView v, String u, String m, JsResult r) { r.cancel(); return true; }
            @Override public boolean onJsConfirm(WebView v, String u, String m, JsResult r) { r.cancel(); return true; }
            @Override public boolean onJsPrompt(WebView v, String u, String m, String d, JsPromptResult r) { r.cancel(); return true; }
            @Override public boolean onJsBeforeUnload(WebView v, String u, String m, JsResult r) { r.cancel(); return true; }
            @Override public boolean onShowFileChooser(WebView v, ValueCallback<android.net.Uri[]> cb, FileChooserParams p) { cb.onReceiveValue(null); return true; }
            @Override public boolean onConsoleMessage(ConsoleMessage m) { return true; }
        });
        web.setDownloadListener(new android.webkit.DownloadListener() {
            public void onDownloadStart(String url, String ua, String cd, String mime, long len) { event(EV_DOWNLOAD, mime == null ? "" : mime, "", 0); }
        });
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(1, 1, Gravity.TOP | Gravity.START);
        web.setVisibility(View.INVISIBLE);
        activity.addContentView(web, lp);
        view = web;
        try {
            serviceWanted = true;
            UI.removeCallbacks(STOP_SERVICE);
            if (!serviceStarted) {
                serviceStarted = true;
                activity.startForegroundService(new Intent(activity, GatewayService.class));
            }
        } catch (Throwable t) { /* the page works in the foreground anyway; the service only keeps it alive in the background */ }
        web.loadUrl("about:blank");
    }

    private static void history(WebView v) {
        event(EV_HISTORY, "", "", (v.canGoBack() ? 1 : 0) | (v.canGoForward() ? 2 : 0));
    }

    private static void wipe(WebView web) {
        try {
            CookieManager.getInstance().removeAllCookies(null);
            CookieManager.getInstance().flush();
            WebStorage.getInstance().deleteAllData();
            web.clearCache(true);
            web.clearHistory();
            web.clearFormData();
            web.clearSslPreferences();
        } catch (Throwable ignored) { /* best effort: the profile is also wiped at the next start */ }
    }

    /** Removes the view and everything it stored. The proxy override stays (fail closed). */
    public static void destroy() { UI.post(new Runnable() { public void run() { destroyOnUi(); } }); }

    private static void destroyOnUi() {
        epoch++;   // a pending start never completes
        final WebView web = view;
        view = null;
        if (web != null) {
            try {
                web.stopLoading();
                web.loadUrl("about:blank");
                wipe(web);
                if (web.getParent() instanceof android.view.ViewGroup) ((android.view.ViewGroup) web.getParent()).removeView(web);
                web.destroy();
            } catch (Throwable ignored) { /* nothing more to do */ }
        }
        serviceWanted = false;
        UI.removeCallbacks(STOP_SERVICE);
        UI.postDelayed(STOP_SERVICE, SERVICE_STOP_DELAY_MS);
    }

    // ── commands from C++ ───────────────────────────────────────────────────────────────────────────────────────────────────────
    public static void load(final String url) { UI.post(new Runnable() { public void run() { if (view != null) view.loadUrl(url); } }); }
    public static void reload() { UI.post(new Runnable() { public void run() { if (view != null) view.reload(); } }); }
    public static void stop() { UI.post(new Runnable() { public void run() { if (view != null) view.stopLoading(); } }); }
    public static void back() { UI.post(new Runnable() { public void run() { if (view != null && view.canGoBack()) view.goBack(); } }); }
    public static void forward() { UI.post(new Runnable() { public void run() { if (view != null && view.canGoForward()) view.goForward(); } }); }

    /** Places the view over the QML item. x/y are SCREEN pixels; the offset of the content root is removed here. */
    public static void place(final int x, final int y, final int w, final int h, final boolean visible) {
        UI.post(new Runnable() {
            public void run() {
                if (view == null) return;
                int[] origin = new int[2];
                View root = (View) view.getParent();
                if (root != null) root.getLocationOnScreen(origin);
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(Math.max(1, w), Math.max(1, h), Gravity.TOP | Gravity.START);
                lp.leftMargin = x - origin[0];
                lp.topMargin = y - origin[1];
                view.setLayoutParams(lp);
                view.setVisibility(visible ? View.VISIBLE : View.INVISIBLE);
            }
        });
    }
}
