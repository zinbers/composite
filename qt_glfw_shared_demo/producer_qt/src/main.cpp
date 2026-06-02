#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <csignal>
#include <atomic>

#include "qml_offscreen_renderer.h"
#include "shared_texture_publisher.h"
#include "log.h"

static std::atomic<bool> g_quit{false};
static void onSignal(int) { g_quit = true; }

int main(int argc, char *argv[])
{
    // Must be set before QGuiApplication is constructed
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
    // Belt-and-suspenders: also force via env var so the RHI backend is
    // definitely OpenGL even if another backend has higher priority.
    qputenv("QSG_RHI_BACKEND", "opengl");

    QGuiApplication app(argc, argv);

    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    constexpr int W = 800;
    constexpr int H = 600;

    // ── IPC publisher ─────────────────────────────────────────────────────────
    SharedTexturePublisher publisher;
    if (!publisher.initialize(W, H)) {
        LOG_ERROR("producer_qt: failed to init IPC publisher");
        return 1;
    }

    // ── QML off-screen renderer ───────────────────────────────────────────────
    QmlOffscreenRenderer renderer(W, H);
    if (!renderer.initialize(QStringLiteral("qrc:/qml/Main.qml"))) {
        LOG_ERROR("producer_qt: failed to init QML renderer");
        return 1;
    }

    // On every rendered frame, ship the pixels to the consumer via IPC
    QObject::connect(&renderer, &QmlOffscreenRenderer::frameReady, [&]() {
        publisher.publishFrame(renderer.pixels(), W, H);
    });

    // Drive rendering at ~33 fps (one QTimer tick = one attempted render)
    QTimer renderTimer;
    renderTimer.setInterval(33);
    QObject::connect(&renderTimer, &QTimer::timeout, [&]() {
        if (g_quit) {
            renderTimer.stop();
            publisher.shutdown();
            QGuiApplication::quit();
            return;
        }
        renderer.renderFrame();
    });
    renderTimer.start();

    LOG_INFO("producer_qt: running at %dx%d  (Ctrl-C to exit)", W, H);
    return app.exec();
}
