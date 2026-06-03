# Build & Run Guide (Debian)

## 1. Prerequisites

Tested on **Debian 12 (Bookworm)** / **Debian 13 (Trixie)** with an X11 or Wayland display server.

### System packages

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build pkg-config \
    qt6-base-dev qt6-declarative-dev libqt6opengl6-dev \
    libglfw3-dev libglew-dev \
    libgl1-mesa-dev libgles2-mesa-dev \
    libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev
```

> **Qt 6 version**: `qt6-base-dev` on Debian 12 provides Qt 6.4+.  
> On Debian 13 / Ubuntu 24.04 you get Qt 6.6+.  Both work fine.

---

## 2. Build

```bash
cd qt_glfw_shared_demo

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build -j$(nproc)
```

The build produces two executables:

| Path | Role |
|------|------|
| `build/consumer_glfw/consumer_glfw` | GLFW window process |
| `build/producer_qt/producer_qt`     | Qt QML off-screen process |

---

## 3. Run

Open **two terminals**:

**Terminal 1 – start the consumer first:**

```bash
./build/consumer_glfw/consumer_glfw
```

A 1280×720 GLFW window will appear with an animated background.  
The top-right quadrant shows a pulsing red placeholder while waiting for the producer.

**Terminal 2 – start the producer:**

```bash
./build/producer_qt/producer_qt
```

Within a second the Qt QML scene (dark background, spinning rectangle, bouncing ball, live clock) will appear in the top-right quadrant of the GLFW window.

**Stop**: press `Ctrl-C` in either terminal.  
The consumer detects the producer's shutdown flag and reverts to the red placeholder automatically.

---

## 4. Environment variables

| Variable | Value | Effect |
|----------|-------|--------|
| `QSG_RHI_BACKEND` | `opengl` | Force Qt to use the OpenGL RHI path (set automatically by the producer) |
| `QSG_INFO` | `1` | Print Qt Scene Graph diagnostic info |
| `LIBGL_ALWAYS_SOFTWARE` | `1` | Force Mesa software renderer (for headless CI / VMs without GPU) |

---

## 5. Headless / virtual machine

If you have no GPU, install the Mesa software renderer:

```bash
sudo apt install mesa-utils libgl1-mesa-dri
```

Then run:

```bash
LIBGL_ALWAYS_SOFTWARE=1 ./build/consumer_glfw/consumer_glfw &
LIBGL_ALWAYS_SOFTWARE=1 ./build/producer_qt/producer_qt
```

Performance will be lower (~10 fps) but functionally identical.

---

## 6. Acceptance checks

| # | Test |
|---|------|
| 1 | No Qt window appears – only the GLFW window |
| 2 | QML animation (spinner, bouncing ball, clock) is visible in the top-right quadrant at ≥ 30 fps |
| 3 | Demo runs stably for 5+ minutes without crashes |
| 4 | After `Ctrl-C` on the producer, the GLFW window shows the pulsing red placeholder |
| 5 | Restarting the producer causes the GLFW window to pick up the new stream automatically |

---

## 7. Troubleshooting

### `shm_open` / `sem_open` EEXIST
A previous crash left stale objects.  The producer cleans them on startup automatically.  
If the consumer fails to connect, manually clean:
```bash
rm -f /dev/shm/qt_glfw_demo
```
POSIX named semaphores live under `/dev/shm/sem.*` or `/run/shm/`:
```bash
ls /dev/shm/
```

### `GLFW: GLX: Failed to create context`
Make sure a display server is running or use a virtual framebuffer:
```bash
Xvfb :99 -screen 0 1280x720x24 &
export DISPLAY=:99
```

### Qt picks a non-OpenGL backend
Set `QSG_RHI_BACKEND=opengl` explicitly before running `producer_qt`.

### Low frame rate
- Check `htop` – `ipc_bridge_linux.cpp` uses `memcpy` for ~1.8 MB/frame; this is CPU-bound on slow hardware.
- The demo is designed for clarity, not throughput. For production, replace the shared-memory pixel path with DMA-BUF (EGL extension `EGL_EXT_image_dma_buf_import`).

---

## 8. DMA-BUF zero-copy path (X11 + EGL)

For zero-copy GPU texture sharing on Linux X11, the `--dmabuf` flag switches both processes to EGL:

1. Producer exports the GL texture as a DMA-BUF fd using `EGL_MESA_image_dma_buf_export`.
   Qt is kept on the `xcb` platform but forced to use the EGL GL integration instead of GLX
   via `QT_XCB_GL_INTEGRATION=xcb_egl` (set automatically by the producer at startup).
2. The fd is passed to the consumer via a Unix-domain socket (`SCM_RIGHTS`).
3. Consumer creates its GLFW window with `GLFW_EGL_CONTEXT_API` and imports the fd as an
   `EGLImage` with `EGL_EXT_image_dma_buf_import`.

Required Mesa packages (already pulled in by `libgles2-mesa-dev`):
