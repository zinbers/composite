#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <csignal>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "shared_texture_receiver.h"
#include "dmabuf_glfw_receiver.h"
#include "compositor.h"
#include "log.h"

// EGL headers – only used in DMA-BUF mode
#include <EGL/egl.h>

// ─── Globals ─────────────────────────────────────────────────────────────────
static std::atomic<bool> g_quit{false};
static void onSignal(int) { g_quit = true; }

static void onFramebufferResize(GLFWwindow *win, int w, int h)
{
    auto *comp = static_cast<Compositor *>(glfwGetWindowUserPointer(win));
    if (comp) comp->resize(w, h);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    bool useDmaBuf = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--dmabuf") {
            useDmaBuf = true;
            LOG_INFO("consumer_glfw: DMA-BUF zero-copy mode enabled");
        }
    }

    // ── GLFW + window ─────────────────────────────────────────────────────────
    if (!glfwInit()) {
        LOG_ERROR("glfwInit failed");
        return 1;
    }

    if (useDmaBuf) {
        // Request an EGL context so we can call eglCreateImageKHR.
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    constexpr int WIN_W = 1280;
    constexpr int WIN_H = 720;
    GLFWwindow *window = glfwCreateWindow(WIN_W, WIN_H,
                                          "Qt+GLFW Composite Demo", nullptr, nullptr);
    if (!window) {
        LOG_ERROR("glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync

    // ── GLEW ──────────────────────────────────────────────────────────────────
    glewExperimental = GL_TRUE;
    if (GLenum err = glewInit(); err != GLEW_OK) {
        LOG_ERROR("glewInit failed: %s", glewGetErrorString(err));
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // ── Compositor ────────────────────────────────────────────────────────────
    Compositor compositor;
    if (!compositor.initialize(WIN_W, WIN_H)) {
        LOG_ERROR("Compositor init failed");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glfwSetWindowUserPointer(window, &compositor);
    glfwSetFramebufferSizeCallback(window, onFramebufferResize);

    auto startTime = std::chrono::steady_clock::now();

    // ──────────────────────────────────────────────────────────────────────────
    // DMA-BUF zero-copy path
    // ──────────────────────────────────────────────────────────────────────────
    if (useDmaBuf) {
        void *eglDpy = static_cast<void *>(glfwGetEGLDisplay());
        if (!eglDpy || eglDpy == EGL_NO_DISPLAY) {
            LOG_ERROR("consumer_glfw: glfwGetEGLDisplay() returned no display – "
                      "is GLFW using EGL?");
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }

        DmaBufGlfwReceiver receiver;
        bool connected   = false;
        auto lastTry     = std::chrono::steady_clock::now() - std::chrono::seconds(2);
        uint64_t lastFid = 0;

        LOG_INFO("consumer_glfw: DMA-BUF mode – waiting for producer_qt --dmabuf");

        while (!glfwWindowShouldClose(window) && !g_quit) {
            glfwPollEvents();
            auto  now     = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - startTime).count();

            // ── Connect / reconnect ───────────────────────────────────────────
            if (!connected) {
                if (now - lastTry >= std::chrono::milliseconds(500)) {
                    lastTry = now;
                    if (receiver.tryConnect()) {
                        connected = true;
                        LOG_INFO("consumer_glfw: connected to DMA-BUF producer");
                    }
                }
            } else if (receiver.isShutdown()) {
                LOG_INFO("consumer_glfw: producer shut down – showing placeholder");
                connected = false;
                receiver  = DmaBufGlfwReceiver();
                lastTry   = now;
            }

            // ── Poll for new frame ────────────────────────────────────────────
            if (connected) {
                if (receiver.pollFrame(eglDpy, 1)) {
                    // frameId check is implicit: the receiver always tracks the
                    // latest buffer index internally.
                    lastFid = receiver.frameId();
                }
            }

            // ── Render ───────────────────────────────────────────────────────
            unsigned int tex = connected ? receiver.textureId() : 0u;
            compositor.renderFrameWithExtTex(elapsed, connected,
                                             tex, receiver.hasFrame(),
                                             false /* DMA-BUF: no Y-flip */);
            glfwSwapBuffers(window);
        }

        (void)lastFid;
        LOG_INFO("consumer_glfw: exiting DMA-BUF mode");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Pixel-copy path (original IPC)
    // ──────────────────────────────────────────────────────────────────────────
    SharedTextureReceiver receiver;
    bool qtConnected = false;
    bool readySent   = false;

    auto lastConnectTry = std::chrono::steady_clock::now() - std::chrono::seconds(2);

    uint64_t lastFrameId    = 0;
    uint64_t renderedFrames = 0;

    LOG_INFO("consumer_glfw: window open – waiting for producer_qt (start it now)");

    while (!glfwWindowShouldClose(window) && !g_quit) {
        glfwPollEvents();

        auto  now     = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - startTime).count();

        if (!qtConnected) {
            if (now - lastConnectTry >= std::chrono::milliseconds(500)) {
                lastConnectTry = now;
                if (receiver.tryConnect()) {
                    qtConnected = true;
                    readySent   = false;
                    LOG_INFO("consumer_glfw: Qt producer connected");
                }
            }
        } else {
            if (!readySent) {
                receiver.signalReady();
                readySent = true;
            }
            if (receiver.isShutdown()) {
                LOG_INFO("consumer_glfw: producer signalled shutdown – showing placeholder");
                qtConnected = false;
                receiver    = SharedTextureReceiver();
                lastConnectTry = now;
            }
        }

        const uint8_t *newPixels = nullptr;
        int newW = 0, newH = 0;
        if (qtConnected) {
            if (receiver.pollFrame(1)) {
                uint64_t fid = receiver.frameId();
                if (fid != lastFrameId) {
                    lastFrameId = fid;
                    newPixels   = receiver.pixels();
                    newW        = receiver.width();
                    newH        = receiver.height();
                }
            }
        }

        compositor.renderFrame(elapsed, qtConnected, newPixels, newW, newH);
        glfwSwapBuffers(window);
        ++renderedFrames;
    }

    LOG_INFO("consumer_glfw: exiting (rendered %llu frames)",
             static_cast<unsigned long long>(renderedFrames));
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
