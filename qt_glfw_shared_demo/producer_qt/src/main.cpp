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
            // On X11, force the xcb platform to use EGL instead of GLX,
            // so Qt obtains an EGLContext (needed for EGLImage/DMA-BUF export).
            qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
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
        // Phase 1: create the GL context only (context will be current after this).
        if (!renderer.initGLContext()) {
            LOG_ERROR("producer_qt: failed to create GL context");
            return 1;
        }

        auto *glCtx = renderer.glContext();
        void *eglDpy = qtEglDisplay(glCtx);
        void *eglCtx = qtEglContext(glCtx);
        if (!eglDpy || !eglCtx) {
            LOG_ERROR("producer_qt: could not obtain EGL display/context "
                      "(is Qt using EGL? ensure QT_XCB_GL_INTEGRATION=xcb_egl "
                      "and libqt6opengl6-dev is installed)");
            return 1;
        }

        // Phase 2: allocate two DMA-BUF compatible GL textures while the
        // context is current.
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

        // Phase 3: tell the renderer to use the external DMA-BUF textures, then
        // finish the full initialization (loads QML, creates FBOs).
        renderer.setExternalRenderTextures(texIds);
        if (!renderer.initialize(QStringLiteral("qrc:/qml/Main.qml"))) {
            LOG_ERROR("producer_qt: failed to init QML renderer");
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
