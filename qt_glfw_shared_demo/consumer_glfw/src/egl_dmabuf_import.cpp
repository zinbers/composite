#include "egl_dmabuf_import.h"
#include "log.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

// ─── EGL / GL extension function pointers ────────────────────────────────────
namespace {

struct EglImportProcs {
    PFNEGLCREATEIMAGEKHRPROC  createImage  = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;

    // GL_OES_EGL_image
    typedef void (*GLEGT2D)(GLenum, GLeglImageOES);
    GLEGT2D glEGLImageTargetTexture2DOES = nullptr;

    bool loaded = false;

    bool load()
    {
        if (loaded) return true;

        createImage  = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
            eglGetProcAddress("eglCreateImageKHR"));
        destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            eglGetProcAddress("eglDestroyImageKHR"));
        glEGLImageTargetTexture2DOES = reinterpret_cast<GLEGT2D>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));

        if (!createImage || !destroyImage || !glEGLImageTargetTexture2DOES) {
            LOG_ERROR("EglDmaBufImporter: required EGL/GL extensions missing "
                      "(need EGL_KHR_image_base + EGL_EXT_image_dma_buf_import "
                      "+ GL_OES_EGL_image)");
            return false;
        }
        loaded = true;
        return true;
    }
} g_procs;

} // namespace

// ─── Ctor / Dtor ─────────────────────────────────────────────────────────────
EglDmaBufImporter::EglDmaBufImporter()  = default;
EglDmaBufImporter::~EglDmaBufImporter() = default;

// ─── import ──────────────────────────────────────────────────────────────────
bool EglDmaBufImporter::import(void *eglDpy, int dmabufFd,
                                int width, int height,
                                uint32_t stride,
                                uint32_t drmFourcc, uint64_t modifier)
{
    if (!g_procs.load()) return false;

    // Destroy any previous import
    if (m_image) destroy(eglDpy);

    auto dpy = static_cast<EGLDisplay>(eglDpy);

    // Build EGL attribute list for EGL_LINUX_DMA_BUF_EXT
    // The modifier is included via EGL_EXT_image_dma_buf_import_modifiers.
    const EGLint attribs[] = {
        EGL_WIDTH,                          static_cast<EGLint>(width),
        EGL_HEIGHT,                         static_cast<EGLint>(height),
        EGL_LINUX_DRM_FOURCC_EXT,           static_cast<EGLint>(drmFourcc),
        EGL_DMA_BUF_PLANE0_FD_EXT,          static_cast<EGLint>(dmabufFd),
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,      0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,       static_cast<EGLint>(stride),
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, static_cast<EGLint>(modifier & 0xFFFFFFFFu),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, static_cast<EGLint>(modifier >> 32),
        EGL_NONE
    };

    m_image = g_procs.createImage(
        dpy, EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        nullptr,  // no client buffer – fd is in attribs
        attribs);

    if (m_image == EGL_NO_IMAGE_KHR) {
        LOG_ERROR("EglDmaBufImporter: eglCreateImageKHR failed (err=0x%x)",
                  static_cast<unsigned>(eglGetError()));
        return false;
    }

    // Create / re-use GL texture
    if (m_texId == 0)
        glGenTextures(1, &m_texId);

    glBindTexture(GL_TEXTURE_2D, m_texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Bind the EGLImage as the texture's backing storage – zero CPU copy.
    g_procs.glEGLImageTargetTexture2DOES(
        GL_TEXTURE_2D,
        static_cast<GLeglImageOES>(m_image));

    glBindTexture(GL_TEXTURE_2D, 0);

    LOG_INFO("EglDmaBufImporter: imported DMA-BUF  "
             "%dx%d  fmt=0x%08x  mod=0x%016llx  stride=%u  tex=%u",
             width, height, drmFourcc,
             static_cast<unsigned long long>(modifier),
             stride, m_texId);
    return true;
}

// ─── destroy ─────────────────────────────────────────────────────────────────
void EglDmaBufImporter::destroy(void *eglDpy)
{
    if (m_texId) {
        glDeleteTextures(1, &m_texId);
        m_texId = 0;
    }
    if (m_image && g_procs.destroyImage) {
        g_procs.destroyImage(static_cast<EGLDisplay>(eglDpy),
                             static_cast<EGLImageKHR>(m_image));
        m_image = nullptr;
    }
}
