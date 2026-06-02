#pragma once
#include <QObject>
#include <QSize>
#include <vector>
#include <cstdint>

QT_FORWARD_DECLARE_CLASS(QOpenGLContext)
QT_FORWARD_DECLARE_CLASS(QOffscreenSurface)
QT_FORWARD_DECLARE_CLASS(QQuickRenderControl)
QT_FORWARD_DECLARE_CLASS(QQuickWindow)
QT_FORWARD_DECLARE_CLASS(QQmlEngine)
QT_FORWARD_DECLARE_CLASS(QQmlComponent)
QT_FORWARD_DECLARE_CLASS(QQuickItem)

// QmlOffscreenRenderer drives Qt Quick / QML scene-graph rendering entirely
// off-screen (no visible window).  After each rendered frame the RGBA pixel
// data is available via pixels() and the frameReady() signal is emitted.
//
// Threading model: all methods must be called from the same thread that owns
// the QOpenGLContext (the Qt main thread in this demo).

class QmlOffscreenRenderer : public QObject
{
    Q_OBJECT
public:
    explicit QmlOffscreenRenderer(int width, int height,
                                   QObject *parent = nullptr);
    ~QmlOffscreenRenderer() override;

    // Load and initialise the QML scene. Must be called once before renderFrame().
    // If initGLContext() was already called this skips context creation and
    // proceeds directly to QML setup.
    bool initialize(const QString &qmlUrl);

    // ── DMA-BUF helper: two-phase init ───────────────────────────────────────
    // Creates the OpenGL context and off-screen surface without loading any QML
    // or creating GL resources.  Call this first, then allocate your textures
    // (the context is current after this call), then call
    // setExternalRenderTextures(), and finally initialize().
    bool initGLContext();

    // Returns the Qt OpenGL context (valid after initGLContext() or initialize()).
    QOpenGLContext *glContext() const { return m_glCtx; }

    // Drive one render cycle. Returns true when new pixels were produced.
    bool renderFrame();

    const uint8_t *pixels() const { return m_pixels.data(); }
    int width()  const { return m_width;  }
    int height() const { return m_height; }

    // ── DMA-BUF / external render target ─────────────────────────────────────
    // When the caller provides external GL textures (e.g. DMA-BUF backed), Qt
    // renders into those instead of creating its own.  Must be called after
    // initGLContext() but before initialize().  texIds must point to an array of
    // 2 valid GL texture objects that were allocated with the same dimensions and
    // GL_RGBA8 format.  After a successful initialize() the active slot index is
    // advanced by renderFrame() and exposed via activeSlot().
    void setExternalRenderTextures(const unsigned int texIds[2]);

    // Index (0 or 1) of the texture slot Qt rendered into most recently.
    int  activeSlot() const { return m_activeSlot; }

signals:
    void frameReady();

private slots:
    void onSceneChanged();
    void onRenderRequested();

private:
    bool createResources();
    void destroyResources();

    int  m_width  = 0;
    int  m_height = 0;
    bool m_dirty  = false;

    QOpenGLContext      *m_glCtx         = nullptr;
    QOffscreenSurface   *m_surface       = nullptr;
    QQuickRenderControl *m_renderControl = nullptr;
    QQuickWindow        *m_quickWindow   = nullptr;
    QQmlEngine          *m_engine        = nullptr;
    QQmlComponent       *m_component     = nullptr;
    QQuickItem          *m_rootItem      = nullptr;

    unsigned int m_colorTex  = 0; // GL texture Qt renders into (slot 0 in DMA-BUF mode)
    unsigned int m_readFbo   = 0; // FBO used only for glReadPixels

    // External render targets (DMA-BUF mode): both slots + the write FBO
    bool         m_useExtTex   = false;
    unsigned int m_extTex[2]   = {0, 0};
    unsigned int m_extFbo[2]   = {0, 0};
    int          m_activeSlot  = 0;

    std::vector<uint8_t> m_pixels;
};
