#pragma once
#include <cstdint>

// EglDmaBufImporter – consumer-side EGL/DMA-BUF helper.
//
// Given a DMA-BUF file-descriptor received from the producer, this class
// creates an EGLImageKHR via EGL_LINUX_DMA_BUF_EXT and then wraps it in a
// standard OpenGL texture using glEGLImageTargetTexture2DOES.
//
// The resulting GL texture ID can be used directly as a sampler2D in any
// shader – no pixel upload is performed at any point.
//
// Required EGL/GL extensions (checked at runtime):
//   EGL_KHR_image_base
//   EGL_EXT_image_dma_buf_import
//   EGL_EXT_image_dma_buf_import_modifiers  (for format modifier attributes)
//   GL_OES_EGL_image
//
// Usage:
//   EglDmaBufImporter imp;
//   imp.import(eglDisplay, fd, width, height, stride, fourcc, modifier);
//   GLuint tex = imp.textureId();   // use in shader
//   // … on next frame with same fd, no re-import needed …
//   imp.destroy(eglDisplay);

class EglDmaBufImporter {
public:
    EglDmaBufImporter();
    ~EglDmaBufImporter();

    EglDmaBufImporter(const EglDmaBufImporter &) = delete;
    EglDmaBufImporter &operator=(const EglDmaBufImporter &) = delete;

    // Import a DMA-BUF fd as an EGLImage and bind it to a new GL texture.
    // eglDpy is the EGLDisplay from glfwGetEGLDisplay().
    // Returns false if an extension is missing or the import fails.
    bool import(void *eglDpy, int dmabufFd,
                int width, int height, uint32_t stride,
                uint32_t drmFourcc, uint64_t modifier);

    // Destroy the EGLImage and delete the GL texture.
    void destroy(void *eglDpy);

    // The GL texture ID (valid after a successful import()).
    unsigned int textureId() const { return m_texId; }
    bool         isValid()   const { return m_image != nullptr; }

private:
    void        *m_image = nullptr; // EGLImageKHR
    unsigned int m_texId = 0;
};
