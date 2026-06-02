#pragma once

// EglDmaBufExporter – producer-side EGL/DMA-BUF helper.
//
// Given a pre-allocated OpenGL texture that Qt has rendered into, this class
// wraps it in an EGLImageKHR and then exports it as a DMA-BUF file-descriptor
// using the EGL_MESA_image_dma_buf_export extension.
//
// The caller is responsible for calling glFinish() before exportFd() to ensure
// the GPU has completed all rendering into the texture.
//
// Required EGL extensions (checked at runtime):
//   EGL_KHR_image_base
//   EGL_KHR_gl_texture_2D_image
//   EGL_MESA_image_dma_buf_export
//   GL_OES_EGL_image  (for glEGLImageTargetTexture2DOES, used on the consumer)
//
// Usage:
//   EglDmaBufExporter exp;
//   exp.init(eglDisplay, eglContext, glTexId, width, height);
//   // render into glTexId via FBO …
//   glFinish();
//   int fd = exp.exportFd(eglDisplay);
//   // send fd to consumer via DmaBufChannel::sendFrame()
//   close(fd);  // caller closes; the DMA-BUF stays alive in the consumer

#include <cstdint>

// Forward-declare opaque EGL types so callers don't need to include EGL headers.
typedef void *EGLDisplayHandle; // actually EGLDisplay (void*)
typedef void *EGLImageHandle;   // actually EGLImageKHR (void*)

class EglDmaBufExporter {
public:
    EglDmaBufExporter();
    ~EglDmaBufExporter();

    EglDmaBufExporter(const EglDmaBufExporter &) = delete;
    EglDmaBufExporter &operator=(const EglDmaBufExporter &) = delete;

    // Wrap an existing GL texture in an EGLImage.
    // eglDpy / eglCtx must be current (obtained from QNativeInterface::QEGLContext).
    // Returns false if required extensions are not available.
    bool init(EGLDisplayHandle eglDpy, EGLImageHandle eglCtx,
              unsigned int glTexId, int width, int height);

    // Export the EGLImage as a DMA-BUF fd.
    // A new fd is returned on every call; the caller must close() it.
    // Returns -1 on failure.
    int exportFd(EGLDisplayHandle eglDpy);

    // Query the DRM pixel format (fourcc) and format modifier.
    // stride is filled with the bytes-per-row for plane 0.
    bool queryFormat(EGLDisplayHandle eglDpy,
                     uint32_t &fourcc, uint64_t &modifier, uint32_t &stride);

    void destroy(EGLDisplayHandle eglDpy);

    bool isValid() const { return m_image != nullptr; }

private:
    EGLImageHandle m_image = nullptr;
};
