#include "compositor.h"
#include "shaders.h"
#include "log.h"

#include <GL/glew.h>
#include <cstddef>  // offsetof
#include <cstring>

// ─── Internal helpers ─────────────────────────────────────────────────────────
namespace {

unsigned int compileShader(GLenum type, const char *src)
{
    unsigned int sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
        LOG_ERROR("Shader compile:\n%s", buf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

unsigned int linkProgram(unsigned int vs, unsigned int fs)
{
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        LOG_ERROR("Program link:\n%s", buf);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

struct Vertex2D { float x, y, u, v; };
static const unsigned int QUAD_IDX[6] = {0, 1, 2, 2, 3, 0};

void buildQuad(unsigned int &vao, unsigned int &vbo, unsigned int &ebo,
               const Vertex2D verts[4])
{
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(Vertex2D), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(QUAD_IDX), QUAD_IDX, GL_STATIC_DRAW);

    // aPos (location 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                          reinterpret_cast<void *>(offsetof(Vertex2D, x)));
    glEnableVertexAttribArray(0);
    // aTexCoord (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                          reinterpret_cast<void *>(offsetof(Vertex2D, u)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

} // namespace

// ─── Compositor ──────────────────────────────────────────────────────────────
Compositor::Compositor()  = default;

Compositor::~Compositor()
{
    if (m_progBg)      glDeleteProgram(m_progBg);
    if (m_progOverlay) glDeleteProgram(m_progOverlay);

    if (m_vaoFull) {
        glDeleteVertexArrays(1, &m_vaoFull);
        glDeleteBuffers(1, &m_vboFull);
        glDeleteBuffers(1, &m_eboFull);
    }
    if (m_vaoCorner) {
        glDeleteVertexArrays(1, &m_vaoCorner);
        glDeleteBuffers(1, &m_vboCorner);
        glDeleteBuffers(1, &m_eboCorner);
    }
    if (m_qtTex) glDeleteTextures(1, &m_qtTex);
}

bool Compositor::initialize(int w, int h)
{
    m_winW = w;
    m_winH = h;

    if (!compileShaders()) return false;
    buildQuads();

    // Allocate the Qt texture object (pixels uploaded later)
    glGenTextures(1, &m_qtTex);
    glBindTexture(GL_TEXTURE_2D, m_qtTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_initialized = true;
    LOG_INFO("Compositor: initialized (%dx%d)", w, h);
    return true;
}

bool Compositor::compileShaders()
{
    unsigned int vs = compileShader(GL_VERTEX_SHADER, VERT_QUAD);
    if (!vs) return false;

    unsigned int fsBg = compileShader(GL_FRAGMENT_SHADER, FRAG_BACKGROUND);
    if (!fsBg) { glDeleteShader(vs); return false; }
    m_progBg = linkProgram(vs, fsBg);
    glDeleteShader(fsBg);

    unsigned int fsOv = compileShader(GL_FRAGMENT_SHADER, FRAG_QT_OVERLAY);
    if (!fsOv) { glDeleteShader(vs); return false; }
    m_progOverlay = linkProgram(vs, fsOv);
    glDeleteShader(fsOv);

    glDeleteShader(vs);
    return (m_progBg != 0) && (m_progOverlay != 0);
}

void Compositor::buildQuads()
{
    // Full-screen quad: NDC (-1,-1) → (1,1)
    Vertex2D full[4] = {
        {-1.f, -1.f, 0.f, 0.f},
        { 1.f, -1.f, 1.f, 0.f},
        { 1.f,  1.f, 1.f, 1.f},
        {-1.f,  1.f, 0.f, 1.f},
    };
    buildQuad(m_vaoFull, m_vboFull, m_eboFull, full);

    // Top-right quadrant: NDC (0,0) → (1,1)
    Vertex2D corner[4] = {
        {0.f, 0.f, 0.f, 0.f},
        {1.f, 0.f, 1.f, 0.f},
        {1.f, 1.f, 1.f, 1.f},
        {0.f, 1.f, 0.f, 1.f},
    };
    buildQuad(m_vaoCorner, m_vboCorner, m_eboCorner, corner);
}

void Compositor::uploadQtTexture(const uint8_t *pixels, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, m_qtTex);
    if (w != m_qtTexW || h != m_qtTexH) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        m_qtTexW = w;
        m_qtTexH = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    m_hasQtFrame = true;
}

void Compositor::renderFrame(float t, bool qtConnected,
                              const uint8_t *qtPixels, int qtW, int qtH)
{
    if (!m_initialized) return;

    // Upload new Qt pixels if available this frame
    if (qtConnected && qtPixels && qtW > 0 && qtH > 0)
        uploadQtTexture(qtPixels, qtW, qtH);

    glViewport(0, 0, m_winW, m_winH);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // ── 1. Animated background (GLFW own content) ────────────────────────────
    glUseProgram(m_progBg);
    glUniform1f(glGetUniformLocation(m_progBg, "uTime"), t);
    glBindVertexArray(m_vaoFull);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // ── 2. Qt overlay (top-right quadrant) ───────────────────────────────────
    // Show overlay when: disconnected (placeholder) OR at least one frame received
    const bool showOverlay = !qtConnected || m_hasQtFrame;
    if (showOverlay) {
        glUseProgram(m_progOverlay);
        glUniform1i(glGetUniformLocation(m_progOverlay, "uDisconnected"),
                    qtConnected ? 0 : 1);
        glUniform1f(glGetUniformLocation(m_progOverlay, "uTime"), t);
        glUniform1i(glGetUniformLocation(m_progOverlay, "uTexture"), 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_qtTex);

        glBindVertexArray(m_vaoCorner);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void Compositor::resize(int w, int h)
{
    m_winW = w;
    m_winH = h;
}
