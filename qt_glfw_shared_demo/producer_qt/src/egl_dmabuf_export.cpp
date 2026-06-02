#include "egl_dmabuf_export.h"
#include "log.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <unistd.h>
#include <cstring>

// ─── EGL extension function pointers (loaded once) ───────────────────────────
namespace {

struct EglProcs {
    PFNEGLCREATEIMAGEKHRPROC          createImage    = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC         destroyImage   = nullptr;
    PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC queryExport = nullptr;
    PFNEGLEXPORTDMABUFIMAGEMESAPROC   doExport       = nullptr;
    bool loaded = false;

    bool load()
    {
        if (loaded) return true;
        createImage  = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
            eglGetProcAddress("eglCreateImageKHR"));
        destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            eglGetProcAddress("eglDestroyImageKHR"));
        queryExport  = reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC>(
            eglGetProcAddress("eglExportDMABUFImageQueryMESA"));
        doExport     = reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEMESAPROC>(
            eglGetProcAddress("eglExportDMABUFImageMESA"));

        if (!createImage || !destroyImage || !queryExport || !doExport) {
            LOG_ERROR("EglDmaBufExporter: required EGL extensions not available "
                      "(need EGL_KHR_image_base + EGL_MESA_image_dma_buf_export)");
            return false;
        }
        loaded = true;
        return true;
    }
} g_procs;

} // namespace

// ─── Ctor / Dtor ─────────────────────────────────────────────────────────────
EglDmaBufExporter::EglDmaBufExporter()  = default;
EglDmaBufExporter::~EglDmaBufExporter() = default;

// ─── init ────────────────────────────────────────────────────────────────────
bool EglDmaBufExporter::init(EGLDisplayHandle eglDpy, EGLImageHandle eglCtx,
                              unsigned int glTexId,
                              int /*width*/, int /*height*/)
{
    if (!g_procs.load()) return false;

    auto dpy = static_cast<EGLDisplay>(eglDpy);
    auto ctx = static_cast<EGLContext>(eglCtx);

    // Wrap the existing GL texture in an EGLImage via EGL_GL_TEXTURE_2D_KHR.
    // This does NOT copy any pixel data – it aliases the GPU storage.
    const EGLint attribs[] = { EGL_NONE };
    m_image = g_procs.createImage(
        dpy, ctx,
        EGL_GL_TEXTURE_2D_KHR,
        reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(glTexId)),
        attribs);

    if (m_image == EGL_NO_IMAGE_KHR) {
        LOG_ERROR("EglDmaBufExporter: eglCreateImageKHR failed (err=0x%x)",
                  static_cast<unsigned>(eglGetError()));
        return false;
    }
    return true;
}

// ─── exportFd ────────────────────────────────────────────────────────────────
int EglDmaBufExporter::exportFd(EGLDisplayHandle eglDpy)
{
    if (!m_image) return -1;

    auto dpy = static_cast<EGLDisplay>(eglDpy);

    int      fd     = -1;
    EGLint   stride = 0;
    EGLint   offset = 0;

    if (!g_procs.doExport(dpy, static_cast<EGLImageKHR>(m_image),
                           &fd, &stride, &offset)) {
        LOG_ERROR("EglDmaBufExporter: eglExportDMABUFImageMESA failed (err=0x%x)",
                  static_cast<unsigned>(eglGetError()));
        return -1;
    }
    return fd;
}

// ─── queryFormat ─────────────────────────────────────────────────────────────
bool EglDmaBufExporter::queryFormat(EGLDisplayHandle eglDpy,
                                     uint32_t &fourcc, uint64_t &modifier,
                                     uint32_t &stride)
{
    if (!m_image) return false;

    auto dpy = static_cast<EGLDisplay>(eglDpy);

    int         fourcc_i  = 0;
    int         nplanes   = 0;
    EGLuint64KHR mod      = 0;

    if (!g_procs.queryExport(dpy, static_cast<EGLImageKHR>(m_image),
                              &fourcc_i, &nplanes, &mod)) {
        LOG_ERROR("EglDmaBufExporter: eglExportDMABUFImageQueryMESA failed "
                  "(err=0x%x)", static_cast<unsigned>(eglGetError()));
        return false;
    }

    fourcc   = static_cast<uint32_t>(fourcc_i);
    modifier = static_cast<uint64_t>(mod);

    // Also read the stride via a single-plane export (fd / offset discarded)
    int      tmpFd  = -1;
    EGLint   strideI = 0;
    EGLint   offset  = 0;
    if (g_procs.doExport(dpy, static_cast<EGLImageKHR>(m_image),
                          &tmpFd, &strideI, &offset)) {
        if (tmpFd >= 0) {
            close(tmpFd);
        }
        stride = static_cast<uint32_t>(strideI);
    } else {
        stride = 0;
    }

    return true;
}

// ─── destroy ─────────────────────────────────────────────────────────────────
void EglDmaBufExporter::destroy(EGLDisplayHandle eglDpy)
{
    if (m_image && g_procs.destroyImage) {
        g_procs.destroyImage(static_cast<EGLDisplay>(eglDpy),
                             static_cast<EGLImageKHR>(m_image));
        m_image = nullptr;
    }
}
