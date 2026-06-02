#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <csignal>
#include <atomic>

#include "qml_offscreen_renderer.h"
#include "shared_texture_publisher.h"
#include "dmabuf_qt_publisher.h"
#include "log.h"

// ── EGL / Qt native interface (DMA-BUF path) ──────────────────────────────────
#include <QOpenGLContext>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QtGui/qpa/qplatformnativeinterface.h>
#  include <QGuiApplication>
#endif

static std::atomic<bool> g_quit{false};
static void onSignal(int) { g_quit = true; }

// Return the EGLDisplay from Qt's current OpenGL context, or nullptr.
static void *qtEglDisplay(QOpenGLContext *ctx)
{
#if defined(Q_OS_LINUX)
    // Qt6: QNativeInterface::QEGLContext
#  if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (auto *iface = ctx->nativeInterface<QNativeInterface::QEGLContext>())
        return iface->display();
#  endif
#endif
    (void)ctx;
    return nullptr;
}

static void *qtEglContext(QOpenGLContext *ctx)
{
#if defined(Q_OS_LINUX)
#  if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (auto *iface = ctx->nativeInterface<QNativeInterface::QEGLContext>())
        return iface->nativeContext();
#  endif
#endif
    (void)ctx;
    return nullptr;
}

int main(int argc, char *argv[])
{
    // Must be set before QGuiApplication is constructed
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
    qputenv("QSG_RHI_BACKEND", "opengl");

    // DMA-BUF mode requires Wayland/EGL; force it when requested.
    bool useDmaBuf = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--dmabuf") {
            useDmaBuf = true;
            // Force Wayland so Qt uses EGL (needed for EGLImage export)
            qputenv("QT_QPA_PLATFORM", "wayland");
            LOG_INFO("producer_qt: DMA-BUF zero-copy mode enabled");
        }
    }

    QGuiApplication app(argc, argv);

    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    constexpr int W = 800;
    constexpr int H = 600;

    // ── QML off-screen renderer ───────────────────────────────────────────────
    QmlOffscreenRenderer renderer(W, H);

    // ── DMA-BUF path ──────────────────────────────────────────────────────────
    DmaBufQtPublisher dmaBufPub;

    if (useDmaBuf) {
        // Pre-allocate two GL textures that will be wrapped in EGLImages.
        // We must do this after the renderer has created its GL context.
        if (!renderer.initialize(QStringLiteral("qrc:/qml/Main.qml"))) {
            LOG_ERROR("producer_qt: failed to init QML renderer");
            return 1;
        }

        // The renderer now has a valid GL context; grab its EGL handle.
        // We need the context current to create the textures.
        auto *glCtx = QOpenGLContext::currentContext();
        if (!glCtx) {
            LOG_ERROR("producer_qt: no current GL context after renderer init");
            return 1;
        }

        // Allocate two DMA-BUF compatible textures (GL_RGBA8, linear).
        unsigned int texIds[2] = {0, 0};
        {
            auto *f = glCtx->functions();
            f->glGenTextures(2, texIds);
            for (int i = 0; i < 2; ++i) {
                f->glBindTexture(GL_TEXTURE_2D, texIds[i]);
                f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H,
                                0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                f->glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // Tell the renderer to use the two external DMA-BUF textures instead
        // of allocating its own.
        renderer.setExternalRenderTextures(texIds);

        void *eglDpy = qtEglDisplay(glCtx);
        void *eglCtx = qtEglContext(glCtx);
        if (!eglDpy || !eglCtx) {
            LOG_ERROR("producer_qt: could not obtain EGL display/context "
                      "(is Qt using EGL? try --dmabuf on Wayland)");
            return 1;
        }

        if (!dmaBufPub.initialize(W, H, eglDpy, eglCtx, texIds)) {
            LOG_ERROR("producer_qt: DmaBufQtPublisher init failed");
            return 1;
        }

        LOG_INFO("producer_qt: waiting for consumer_glfw to connect …");
        if (!dmaBufPub.awaitConsumer()) {
            LOG_ERROR("producer_qt: failed to accept consumer connection");
            return 1;
        }
        LOG_INFO("producer_qt: consumer connected – starting render loop");

        QObject::connect(&renderer, &QmlOffscreenRenderer::frameReady, [&]() {
            dmaBufPub.publishFrame(renderer.activeSlot(), W, H);
        });

    } else {
        // ── Pixel-copy path (original IPC) ────────────────────────────────────
        SharedTexturePublisher publisher;
        if (!publisher.initialize(W, H)) {
            LOG_ERROR("producer_qt: failed to init IPC publisher");
            return 1;
        }

        if (!renderer.initialize(QStringLiteral("qrc:/qml/Main.qml"))) {
            LOG_ERROR("producer_qt: failed to init QML renderer");
            return 1;
        }

        QObject::connect(&renderer, &QmlOffscreenRenderer::frameReady, [&]() {
            publisher.publishFrame(renderer.pixels(), W, H);
        });

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

        LOG_INFO("producer_qt: running at %dx%d  (pixel-copy, Ctrl-C to exit)",
                 W, H);
        return app.exec();
    }

    // ── Render loop (DMA-BUF path) ────────────────────────────────────────────
    QTimer renderTimer;
    renderTimer.setInterval(33);
    QObject::connect(&renderTimer, &QTimer::timeout, [&]() {
        if (g_quit) {
            renderTimer.stop();
            dmaBufPub.shutdown();
            QGuiApplication::quit();
            return;
        }
        renderer.renderFrame();
    });
    renderTimer.start();

    LOG_INFO("producer_qt: running at %dx%d  (DMA-BUF zero-copy, Ctrl-C to exit)",
             W, H);
    return app.exec();
}
