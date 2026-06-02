#pragma once

// ─── Shared vertex shader ─────────────────────────────────────────────────────
//  location 0 : vec2 aPos       (NDC position)
//  location 1 : vec2 aTexCoord  ([0,1] texture coordinate)
inline constexpr const char *VERT_QUAD = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord   = aTexCoord;
}
)GLSL";

// ─── Animated background ──────────────────────────────────────────────────────
//  Drawn on a full-screen quad to show GLFW's own animated content.
inline constexpr const char *FRAG_BACKGROUND = R"GLSL(
#version 410 core
in  vec2 vTexCoord;
out vec4 fragColor;
uniform float uTime;
void main() {
    vec2  uv   = vTexCoord;
    // Dark animated ripple pattern to make GLFW activity visible
    float wave = sin(uv.x * 9.0 + uTime * 1.2)
               * sin(uv.y * 7.0 - uTime * 0.9)
               * 0.5 + 0.5;
    vec3 base  = vec3(0.03, 0.07, 0.14);
    vec3 glow  = vec3(0.04, 0.22, 0.38);
    fragColor  = vec4(mix(base, glow, wave), 1.0);
}
)GLSL";

// ─── Qt overlay ───────────────────────────────────────────────────────────────
//  Drawn on the top-right corner quad.
//  • When Qt is connected: samples the shared texture.
//    uFlipY = 1  → pixel-copy path (glReadPixels, bottom-row first): flip Y.
//    uFlipY = 0  → DMA-BUF path  (GPU-native orientation): no flip.
//  • When Qt is disconnected: pulsing red "source lost" placeholder.
inline constexpr const char *FRAG_QT_OVERLAY = R"GLSL(
#version 410 core
in  vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform bool      uDisconnected;
uniform float     uTime;
uniform bool      uFlipY;
void main() {
    if (uDisconnected) {
        float pulse = 0.35 + 0.25 * sin(uTime * 4.0);
        fragColor = vec4(0.65, 0.05, 0.05, pulse);
    } else {
        vec2 coord = uFlipY
            ? vec2(vTexCoord.x, 1.0 - vTexCoord.y)  // pixel-copy: flip Y
            : vTexCoord;                               // DMA-BUF: no flip
        vec4 texel = texture(uTexture, coord);
        fragColor  = texel;
    }
}
)GLSL";
