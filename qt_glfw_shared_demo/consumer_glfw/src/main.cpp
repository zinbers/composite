#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <csignal>
#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "shared_texture_receiver.h"
#include "compositor.h"
#include "log.h"

// ─── Globals ─────────────────────────────────────────────────────────────────
static std::atomic<bool> g_quit{false};
static void onSignal(int) { g_quit = true; }

// GLFW resize callback – forward to Compositor
static void onFramebufferResize(GLFWwindow *win, int w, int h)
{
    auto *comp = static_cast<Compositor *>(glfwGetWindowUserPointer(win));
    if (comp) comp->resize(w, h);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main()
{
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    // ── GLFW + window ─────────────────────────────────────────────────────────
    if (!glfwInit()) {
        LOG_ERROR("glfwInit failed");
        return 1;
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

    // ── GLEW (must be after context is current) ───────────────────────────────
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

    // ── IPC receiver ──────────────────────────────────────────────────────────
    SharedTextureReceiver receiver;
    bool qtConnected = false;
    bool readySent   = false;

    // Retry-connect timing
    auto lastConnectTry = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    auto startTime      = std::chrono::steady_clock::now();

    uint64_t lastFrameId   = 0;
    uint64_t renderedFrames = 0;

    LOG_INFO("consumer_glfw: window open – waiting for producer_qt (start it now)");

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window) && !g_quit) {
        glfwPollEvents();

        auto  now     = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - startTime).count();

        // ── Try to connect / detect disconnect ────────────────────────────────
        if (!qtConnected) {
            // Attempt a non-blocking connect every 500 ms
            if (now - lastConnectTry >= std::chrono::milliseconds(500)) {
                lastConnectTry = now;
                if (receiver.tryConnect()) {
                    qtConnected = true;
                    readySent   = false;
                    LOG_INFO("consumer_glfw: Qt producer connected");
                }
            }
        } else {
            // Signal ready once
            if (!readySent) {
                receiver.signalReady();
                readySent = true;
            }
            // Check for graceful shutdown from producer
            if (receiver.isShutdown()) {
                LOG_INFO("consumer_glfw: producer signalled shutdown – showing placeholder");
                qtConnected = false;
                // Reset receiver so tryConnect works with a fresh IpcBridge next time
                receiver = SharedTextureReceiver();
                lastConnectTry = now;
            }
        }

        // ── Poll for new Qt frame ─────────────────────────────────────────────
        const uint8_t *newPixels = nullptr;
        int newW = 0, newH = 0;

        if (qtConnected) {
            if (receiver.pollFrame(0)) { // non-blocking
                uint64_t fid = receiver.frameId();
                if (fid != lastFrameId) {
                    lastFrameId = fid;
                    newPixels   = receiver.pixels();
                    newW        = receiver.width();
                    newH        = receiver.height();
                }
            }
        }

        // ── Render ───────────────────────────────────────────────────────────
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
