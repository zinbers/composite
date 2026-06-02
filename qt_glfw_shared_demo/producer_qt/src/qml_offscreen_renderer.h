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
    bool initialize(const QString &qmlUrl);

    // Drive one render cycle. Returns true when new pixels were produced.
    bool renderFrame();

    const uint8_t *pixels() const { return m_pixels.data(); }
    int width()  const { return m_width;  }
    int height() const { return m_height; }

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

    unsigned int m_colorTex  = 0; // GL texture Qt renders into
    unsigned int m_readFbo   = 0; // FBO used only for glReadPixels

    std::vector<uint8_t> m_pixels;
};
