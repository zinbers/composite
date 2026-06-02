#include "qml_offscreen_renderer.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QSurfaceFormat>
#include <QDebug>

#include "log.h"

// ─── Ctor / Dtor ─────────────────────────────────────────────────────────────
QmlOffscreenRenderer::QmlOffscreenRenderer(int width, int height, QObject *parent)
    : QObject(parent), m_width(width), m_height(height)
{
    m_pixels.resize(static_cast<size_t>(width) * height * 4);
}

QmlOffscreenRenderer::~QmlOffscreenRenderer()
{
    if (m_glCtx) m_glCtx->makeCurrent(m_surface);

    delete m_rootItem;   m_rootItem   = nullptr;
    delete m_component;  m_component  = nullptr;
    delete m_engine;     m_engine     = nullptr;
    delete m_quickWindow;m_quickWindow= nullptr;
    delete m_renderControl; m_renderControl = nullptr;

    destroyResources();

    if (m_glCtx) m_glCtx->doneCurrent();
    delete m_glCtx;  m_glCtx  = nullptr;
    delete m_surface;m_surface = nullptr;
}

// ─── initialize ──────────────────────────────────────────────────────────────
bool QmlOffscreenRenderer::initialize(const QString &qmlUrl)
{
    // Surface format – must match what GLFW consumer uses (OpenGL 4.1 Core)
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    // Off-screen surface
    m_surface = new QOffscreenSurface(nullptr, this);
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) {
        LOG_ERROR("QmlOffscreenRenderer: failed to create QOffscreenSurface");
        return false;
    }

    // OpenGL context (shares format with the surface)
    m_glCtx = new QOpenGLContext(this);
    m_glCtx->setFormat(fmt);
    if (!m_glCtx->create()) {
        LOG_ERROR("QmlOffscreenRenderer: failed to create QOpenGLContext");
        return false;
    }
    if (!m_glCtx->makeCurrent(m_surface)) {
        LOG_ERROR("QmlOffscreenRenderer: makeCurrent failed");
        return false;
    }

    // QQuickRenderControl + headless QQuickWindow
    m_renderControl = new QQuickRenderControl(this);
    m_quickWindow   = new QQuickWindow(m_renderControl);
    m_quickWindow->setGeometry(0, 0, m_width, m_height);
    m_quickWindow->contentItem()->setSize(QSizeF(m_width, m_height));

    connect(m_renderControl, &QQuickRenderControl::sceneChanged,
            this, &QmlOffscreenRenderer::onSceneChanged);
    connect(m_renderControl, &QQuickRenderControl::renderRequested,
            this, &QmlOffscreenRenderer::onRenderRequested);

    // initialize() uses the current GL context (Qt6 RHI / OpenGL path)
    if (!m_renderControl->initialize()) {
        LOG_ERROR("QmlOffscreenRenderer: QQuickRenderControl::initialize failed");
        return false;
    }

    // GL texture + readback FBO
    if (!createResources()) return false;

    // Tell Qt Quick to render into our texture
    m_quickWindow->setRenderTarget(
        QQuickRenderTarget::fromOpenGLTexture(m_colorTex, QSize(m_width, m_height)));

    // Load QML
    m_engine    = new QQmlEngine(this);
    m_component = new QQmlComponent(m_engine, QUrl(qmlUrl), this);
    if (m_component->isLoading()) {
        // Synchronous load – shouldn't happen for qrc: URLs, but handle it
        QObject::connect(m_component, &QQmlComponent::statusChanged,
                         m_component, [](QQmlComponent::Status s) {
                             if (s != QQmlComponent::Ready)
                                 qWarning() << "QML component not ready";
                         });
    }
    if (m_component->isError()) {
        qWarning() << "QML errors:" << m_component->errors();
        return false;
    }

    QObject *obj = m_component->create();
    if (!obj) {
        qWarning() << "Failed to create QML root object:" << m_component->errors();
        return false;
    }
    m_rootItem = qobject_cast<QQuickItem *>(obj);
    if (!m_rootItem) {
        LOG_ERROR("QmlOffscreenRenderer: root QML item is not a QQuickItem");
        delete obj;
        return false;
    }
    m_rootItem->setParentItem(m_quickWindow->contentItem());
    m_rootItem->setSize(QSizeF(m_width, m_height));

    m_dirty = true;
    LOG_INFO("QmlOffscreenRenderer: ready (%dx%d)", m_width, m_height);
    return true;
}

// ─── GL resource helpers ─────────────────────────────────────────────────────
bool QmlOffscreenRenderer::createResources()
{
    auto *f = m_glCtx->functions();

    // Colour texture – Qt will render into this
    f->glGenTextures(1, &m_colorTex);
    f->glBindTexture(GL_TEXTURE_2D, m_colorTex);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    // Readback FBO – attaches the same texture so we can call glReadPixels
    f->glGenFramebuffers(1, &m_readFbo);
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_readFbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_colorTex, 0);
    GLenum status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("QmlOffscreenRenderer: readback FBO incomplete (status=0x%x)",
                  static_cast<unsigned>(status));
        return false;
    }
    return true;
}

void QmlOffscreenRenderer::destroyResources()
{
    if (!m_glCtx) return;
    auto *f = m_glCtx->functions();
    if (m_readFbo)  { f->glDeleteFramebuffers(1, &m_readFbo);  m_readFbo  = 0; }
    if (m_colorTex) { f->glDeleteTextures(1,    &m_colorTex);  m_colorTex = 0; }
}

// ─── renderFrame ─────────────────────────────────────────────────────────────
bool QmlOffscreenRenderer::renderFrame()
{
    if (!m_dirty) return false;
    m_dirty = false;

    if (!m_glCtx->makeCurrent(m_surface)) {
        LOG_WARN("QmlOffscreenRenderer: makeCurrent failed – skipping frame");
        return false;
    }

    // Qt6 render sequence
    m_renderControl->polishItems();
    m_renderControl->beginFrame();
    m_renderControl->sync();
    m_renderControl->render();
    m_renderControl->endFrame();

    // Ensure all GL commands are complete before readback
    auto *f = m_glCtx->functions();
    f->glFinish();

    // Readback: bind our FBO (colour attachment = m_colorTex) and read pixels
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_readFbo);
    f->glReadPixels(0, 0, m_width, m_height,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    emit frameReady();
    return true;
}

// ─── Dirty-flag slots ────────────────────────────────────────────────────────
void QmlOffscreenRenderer::onSceneChanged()    { m_dirty = true; }
void QmlOffscreenRenderer::onRenderRequested() { m_dirty = true; }
