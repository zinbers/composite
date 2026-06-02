# Qt(QML) 画面融合到 GLFW 窗口 Demo 计划

## 1. 目标与边界

### 1.1 最终目标
- 仅显示 **GLFW 窗口**。
- Qt 进程不显示自身窗口，使用离屏渲染输出 QML 画面。
- Qt 与 GLFW 跨进程共享 GPU 纹理（“共享显存”）。
- GLFW 进程获取 Qt 的纹理并与自身内容统一渲染（可叠加/拼接）。

### 1.2 Demo 约束
- 平台先做 **Windows + OpenGL**（最容易做跨进程共享纹理句柄）。
- Qt 版本建议 **Qt 6.x**（QQuickRenderControl + RHI/OpenGL 路径）。
- GLFW 使用 OpenGL 4.x。
- 进程间同步先用简化方案（共享内存 + Win32 Event + 双缓冲/fence 简化策略）。

> 说明：Linux/macOS 方案可在后续扩展（EGLImage/DMABUF、IOSurface等），首版先保证 Windows 跑通。

---

## 2. 总体架构

### 2.1 进程划分
1. **producer_qt_offscreen**
   - 负责 QML 离屏渲染。
   - 将渲染结果输出到可共享的 OpenGL 纹理（WGL 共享句柄）。
   - 把共享句柄 + 元数据写入 IPC 通道，并通知 consumer。

2. **consumer_glfw**
   - 创建 GLFW 窗口与 OpenGL 上下文。
   - 从 IPC 读取共享纹理句柄，导入为本地可采样纹理对象。
   - 在同一窗口中渲染：背景/自有内容 + Qt 纹理融合。

### 2.2 数据流
1. Qt 渲染一帧到 FBO/纹理 A。
2. Qt 导出纹理共享句柄（首次或纹理重建时）。
3. Qt 更新共享内存中的 frame metadata（宽高、format、frameId、时间戳）。
4. Qt 触发“新帧事件”。
5. GLFW 收到事件后读取 metadata，采样共享纹理并绘制到自己的窗口。

---

## 3. IPC 与同步设计

### 3.1 IPC 通道
- **共享内存**（CreateFileMapping + MapViewOfFile）：
  - `SharedHeader`：magic/version/width/height/pixelFormat/frameId/flags/handleValid...
  - `SharedHandles`：纹理句柄（64bit）、可选第二缓冲句柄。
- **命名事件**：
  - `evtNewFrame`：Qt 每产出一帧 signal。
  - `evtConsumerReady`：GLFW 启动后通知可接收。
  - `evtShutdown`：任一进程退出广播。

### 3.2 同步策略（Demo）
- 首版采用 **双缓冲 + frameId 原子递增**，避免读写冲突概率。
- 若复杂场景可加 GL sync object + CPU 事件桥接（后续增强项）。

---

## 4. Qt 离屏渲染模块设计

### 4.1 模块拆分
- `QtOffscreenApp`
  - 应用入口，生命周期管理。
- `QmlOffscreenRenderer`
  - 基于 `QQuickRenderControl` + `QQuickWindow` + `QQmlEngine/QQmlComponent`。
  - 驱动 polish/sync/render。
- `SharedTexturePublisher`
  - 创建可共享 GL 纹理/FBO。
  - 维护 WGL 共享句柄导出。
  - 将句柄与 metadata 发布到 IPC。
- `IpcBridge`
  - 共享内存、事件、状态机。

### 4.2 渲染流程（每帧）
1. `beginFrame()`
2. `QQuickRenderControl::polishItems()`
3. `sync()`
4. `render()`
5. `glFlush`（确保命令提交）
6. 更新 metadata + SetEvent(newFrame)

### 4.3 QML 内容
- Demo 用简单动态场景：
  - 背景渐变、旋转矩形、文字、动画时间。
- 方便在 GLFW 端直观看到“Qt 内容在更新”。

---

## 5. GLFW 融合渲染模块设计

### 5.1 模块拆分
- `GlfwConsumerApp`
  - 窗口/上下文/主循环。
- `SharedTextureReceiver`
  - IPC 初始化、共享句柄读取、导入 GL 纹理。
- `Compositor`
  - 统一渲染管线：背景 + Qt 纹理 quad。
- `ShaderPrograms`
  - 基础纹理采样、alpha 混合、可选色彩校正。

### 5.2 渲染逻辑
1. 轮询/等待 `evtNewFrame`。
2. 若句柄变化则重建导入纹理对象。
3. 根据 metadata 设置采样区域和缩放（保持比例）。
4. 绘制 GLFW 自身内容（例如彩色三角形）+ Qt 纹理叠加。
5. `glfwSwapBuffers()`。

---

## 6. 工程结构规划

```text
qt_glfw_shared_demo/
  CMakeLists.txt
  cmake/
    FindGLFW.cmake (如需)
  common/
    include/
      ipc_protocol.h
      ipc_bridge.h
      gl_shared_texture.h
      log.h
    src/
      ipc_bridge_win.cpp
      gl_shared_texture_wgl.cpp
  producer_qt/
    CMakeLists.txt
    src/
      main.cpp
      qml_offscreen_renderer.h/.cpp
      shared_texture_publisher.h/.cpp
      qt_gl_context_helper.h/.cpp
    qml/
      Main.qml
  consumer_glfw/
    CMakeLists.txt
    src/
      main.cpp
      shared_texture_receiver.h/.cpp
      compositor.h/.cpp
      shaders.h/.cpp
  docs/
    plan.md
    protocol.md
    run_guide.md
```

---

## 7. 协议细节（初稿）

### 7.1 `SharedHeader` 字段
- `uint32_t magic = 0x5154474C`  // "QTGL"
- `uint32_t version = 1`
- `uint32_t width`
- `uint32_t height`
- `uint32_t internalFormat`  // GL_RGBA8
- `uint64_t sharedHandle`    // Win32 HANDLE value cast
- `uint64_t frameId`
- `uint32_t producerPid`
- `uint32_t flags`           // bit0:handleValid bit1:shutdown
- `uint64_t timestampNs`

### 7.2 兼容策略
- 消费端校验 magic/version，不匹配则拒绝渲染并打印错误。
- handle 变化时重建导入纹理。

---

## 8. 构建与运行计划

### 8.1 构建系统
- 使用 CMake（顶层 + 两个子工程）。
- 依赖：
  - Qt6: Core, Gui, Quick, Qml, OpenGL
  - GLFW3
  - OpenGL32, GDI32（Windows）

### 8.2 运行步骤
1. 先启动 `consumer_glfw`（创建窗口并等待 producer）。
2. 启动 `producer_qt`（离屏渲染并发布共享纹理）。
3. GLFW 窗口中看到 Qt 动态画面融合显示。

---

## 9. 验收标准

1. 不出现 Qt 可见窗口，仅 GLFW 窗口显示。
2. GLFW 中可实时看到 QML 动画（>30 FPS）。
3. 可稳定运行 5 分钟以上无崩溃。
4. Qt 进程退出后，GLFW 能检测并优雅降级（显示“源断开”）。
5. 再次启动 Qt，GLFW 可恢复显示（可选增强项，若时间允许）。

---

## 10. 风险与规避

1. **Qt 渲染后端不一致**  
   - 规避：强制 Qt 使用 OpenGL 后端（启动参数/环境变量）。

2. **跨进程共享纹理平台差异**  
   - 规避：首版锁定 Windows WGL；其他平台后续适配。

3. **同步撕裂或偶发黑帧**  
   - 规避：双缓冲 + frameId 校验；必要时增加 fence。

4. **句柄生命周期管理复杂**  
   - 规避：统一在 publisher/receiver 封装 RAII，显式 close。

---

## 11. 分阶段实施计划

### Phase A：最小闭环
- 跑通 Qt 离屏渲染到本地纹理。
- GLFW 进程窗口显示自身内容。
- IPC 打通（仅文本/metadata）。

### Phase B：共享纹理打通
- Qt 导出共享句柄，GLFW 导入并采样。
- 完成基本融合显示。

### Phase C：稳定性增强
- 双缓冲与重建逻辑。
- 退出/重连、错误处理、日志。

### Phase D：文档与可复现
- `run_guide.md`、常见问题、依赖版本说明。

---

## 12. 交付物清单（你确认后将生成）

1. 完整 C++/QML 源码（producer + consumer + common）。
2. CMake 工程（可直接构建）。
3. `protocol.md`（IPC 协议说明）。
4. `run_guide.md`（构建与运行步骤、故障排查）。
5. 默认演示场景（Qt 动画 + GLFW 融合）。

---

## 13. 需要你确认的选项

请确认以下参数，我再生成代码（默认值见括号）：

1. 目标平台锁定 Debian13
2. Qt 版本（默认：**Qt 6.5+**）
3. OpenGL 版本（默认：**4.1+**）
4. 融合方式（默认：**GLFW背景 + Qt纹理右上角叠加**）
5. 是否需要透明通道（alpha）保留？（默认：**是**）
6. 是否要加“Qt 源断开提示文字”？（默认：**是**）
