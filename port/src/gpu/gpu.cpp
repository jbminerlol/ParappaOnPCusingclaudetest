// PS1 GPU on OpenGL 3.3.
//
// VRAM is an RGBA8 texture of (1024*S)x(512*S) pixels attached to a framebuffer. Every VRAM word
// is stored as its three 5-bit channels (scaled to 8 bits, so they decode exactly) plus the mask
// bit in alpha. Paletted and direct textures are decoded in the fragment shader from a copy of
// that texture (refreshed after drawing), so CLUT lookups, texture windows and STP semantics match
// the hardware. Drawing commands are batched until a state change or a VRAM transfer.
#include "gpu/gpu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "gpu/gl.h"
#include "psx/log.h"
#include "psx/memory.h"

namespace gpu {
namespace {

constexpr int VRAM_W = 1024, VRAM_H = 512;

// tpage field of a vertex: texpage bits 0-8 as in GP0(E1) plus primitive flags.
constexpr uint16_t V_TEXTURED = 1 << 9;
constexpr uint16_t V_RAW = 1 << 10;
constexpr uint16_t V_SEMI = 1 << 11;

struct Vertex {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
    uint16_t clut;
    uint16_t tpage;
};

struct BatchKey {
    int blend = -1;  // -1 opaque, else semi-transparency mode 0-3
    int clip[4] = {0, 0, 0, 0};
    int texwin[4] = {0, 0, 0, 0};
    bool set_mask = false;
    bool operator==(const BatchKey &o) const {
        return blend == o.blend && std::memcmp(clip, o.clip, sizeof(clip)) == 0 &&
               std::memcmp(texwin, o.texwin, sizeof(texwin)) == 0 && set_mask == o.set_mask;
    }
};

struct State {
    int scale = 1;
    GLuint vram_tex = 0, vram_fbo = 0;
    GLuint sample_tex = 0, sample_fbo = 0;
    GLuint upload_tex = 0;
    GLuint read_tex = 0, read_fbo = 0;
    GLuint draw_prog = 0, upload_prog = 0, present_prog = 0;
    GLuint vao = 0, vbo = 0, quad_vao = 0, quad_vbo = 0;
    bool sample_stale = true;

    // GP0 draw state.
    uint16_t texpage = 0;  // E1 bits 0-11
    int texwin[4] = {0, 0, 0, 0};  // mask x, mask y, offset x, offset y (pixels)
    int clip_x1 = 0, clip_y1 = 0, clip_x2 = 0, clip_y2 = 0;
    int off_x = 0, off_y = 0;
    bool set_mask = false, check_mask = false;

    std::vector<Vertex> batch;
    BatchKey key;

    std::vector<uint32_t> pending;  // partial command across gp0() calls
    DisplayMode display;
} g;

// ---------------------------------------------------------------- shaders

const char *const kDrawVS = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uint a_clut;
layout(location = 4) in uint a_tpage;
noperspective out vec3 v_color;
noperspective out vec2 v_uv;
flat out uint v_clut;
flat out uint v_tpage;
void main() {
    v_color = a_color.rgb;
    v_uv = a_uv;
    v_clut = a_clut;
    v_tpage = a_tpage;
    gl_Position = vec4(a_pos / vec2(1024.0, 512.0) * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *const kVramFetch = R"(
uniform sampler2D u_vram;
uniform int u_scale;
uint fetch16(ivec2 p) {
    p &= ivec2(1023, 511);
    vec4 c = texelFetch(u_vram, p * u_scale + u_scale / 2, 0);
    uvec4 q = uvec4(round(c * vec4(31.0, 31.0, 31.0, 1.0)));
    return q.r | (q.g << 5) | (q.b << 10) | (q.a << 15);
}
vec3 rgb15(uint w) {
    return vec3(float(w & 31u), float((w >> 5) & 31u), float((w >> 10) & 31u)) / 31.0;
}
)";

const char *const kDrawFS = R"(
noperspective in vec3 v_color;
noperspective in vec2 v_uv;
flat in uint v_clut;
flat in uint v_tpage;
uniform ivec4 u_texwin;
uniform int u_pass;  // 0: opaque batch, 1: semi batch opaque part, 2: semi batch blended part
uniform int u_set_mask;
out vec4 o_color;
void main() {
    bool textured = (v_tpage & 512u) != 0u;
    bool raw = (v_tpage & 1024u) != 0u;
    bool semi = (v_tpage & 2048u) != 0u;
    vec3 color = v_color;
    uint stp = 0u;
    if (textured) {
        ivec2 uv = ivec2(floor(v_uv)) & 255;
        uv = (uv & ~u_texwin.xy) | (u_texwin.zw & u_texwin.xy);
        ivec2 base = ivec2(int(v_tpage & 15u) * 64, int((v_tpage >> 4) & 1u) * 256);
        ivec2 clut = ivec2(int(v_clut & 63u) * 16, int(v_clut >> 6));
        uint depth = (v_tpage >> 7) & 3u;
        uint texel;
        if (depth == 0u) {
            uint w = fetch16(base + ivec2(uv.x >> 2, uv.y));
            texel = fetch16(clut + ivec2(int((w >> uint((uv.x & 3) * 4)) & 15u), 0));
        } else if (depth == 1u) {
            uint w = fetch16(base + ivec2(uv.x >> 1, uv.y));
            texel = fetch16(clut + ivec2(int((w >> uint((uv.x & 1) * 8)) & 255u), 0));
        } else {
            texel = fetch16(base + uv);
        }
        if (texel == 0u) discard;
        stp = texel >> 15;
        vec3 t = rgb15(texel);
        color = raw ? t : min(t * v_color * (255.0 / 128.0), vec3(1.0));
    }
    bool blended = semi && (!textured || stp != 0u);
    if (u_pass == 1 && blended) discard;
    if (u_pass == 2 && !blended) discard;
    color = floor(color * 31.0 + 0.5) / 31.0;
    o_color = vec4(color, (stp != 0u || u_set_mask != 0) ? 1.0 : 0.0);
}
)";

const char *const kQuadVS = R"(#version 330 core
layout(location = 0) in vec2 a_pos;  // 0..1
uniform vec4 u_dst;                  // NDC rectangle x0, y0, x1, y1
out vec2 v_t;
void main() {
    v_t = a_pos;
    gl_Position = vec4(mix(u_dst.xy, u_dst.zw, a_pos), 0.0, 1.0);
}
)";

const char *const kUploadFS = R"(#version 330 core
uniform usampler2D u_src;  // R16UI, VRAM layout
uniform int u_scale;
out vec4 o_color;
void main() {
    uint w = texelFetch(u_src, ivec2(gl_FragCoord.xy) / u_scale, 0).r;
    o_color = vec4(vec3(float(w & 31u), float((w >> 5) & 31u), float((w >> 10) & 31u)) / 31.0,
                   float(w >> 15));
}
)";

const char *const kPresentFS = R"(
in vec2 v_t;
uniform ivec4 u_area;  // x, y, w, h in VRAM pixels
uniform int u_rgb24;
out vec4 o_color;
void main() {
    vec2 p = vec2(u_area.xy) + v_t * vec2(u_area.zw);
    if (u_rgb24 == 0) {
        o_color = vec4(texture(u_vram, p / vec2(1024.0, 512.0)).rgb, 1.0);
        return;
    }
    ivec2 ip = ivec2(floor(p));
    int byte = (ip.x - u_area.x) * 3;
    ivec2 row = ivec2(u_area.x, ip.y);
    uint w0 = fetch16(row + ivec2(byte >> 1, 0));
    uint w1 = fetch16(row + ivec2((byte >> 1) + 1, 0));
    uint bytes = w0 | (w1 << 16);
    uint shift = uint(byte & 1) * 8u;
    o_color = vec4(float((bytes >> shift) & 255u), float((bytes >> (shift + 8u)) & 255u),
                   float((bytes >> (shift + 16u)) & 255u), 255.0) / 255.0;
}
)";

GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        psx::fatal("Shader compile error:\n%s", log);
    }
    return s;
}

GLuint program(const std::string &vs, const std::string &fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs.c_str()), f = compile(GL_FRAGMENT_SHADER, fs.c_str());
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        psx::fatal("Shader link error:\n%s", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

GLuint make_tex(GLint internal, int w, int h, GLenum format, GLenum type) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint make_fbo(GLuint tex) {
    GLuint f;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) psx::fatal("Incomplete framebuffer");
    return f;
}

void bind_vram_target() {
    glBindFramebuffer(GL_FRAMEBUFFER, g.vram_fbo);
    glViewport(0, 0, VRAM_W * g.scale, VRAM_H * g.scale);
}

// Copies VRAM into the texture the shaders sample from.
void refresh_sample() {
    if (!g.sample_stale) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g.vram_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.sample_fbo);
    int w = VRAM_W * g.scale, h = VRAM_H * g.scale;
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g.sample_stale = false;
}

void draw_quad() {
    glBindVertexArray(g.quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// VRAM rectangle (in VRAM pixels) -> NDC of the VRAM framebuffer.
void set_dst(GLuint prog, int x, int y, int w, int h) {
    glUniform4f(glGetUniformLocation(prog, "u_dst"), x / 512.0f - 1.0f, y / 256.0f - 1.0f, (x + w) / 512.0f - 1.0f,
                (y + h) / 256.0f - 1.0f);
}

void set_blend(int mode) {
    if (mode < 0) {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    // RGB per the PS1 modes; alpha (mask bit) always comes from the source.
    switch (mode) {
    case 0:  // B/2 + F/2
        glBlendColor(0, 0, 0, 0.5f);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA, GL_ONE, GL_ZERO);
        break;
    case 1:  // B + F
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        break;
    case 2:  // B - F
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        break;
    default:  // B + F/4
        glBlendColor(0, 0, 0, 0.25f);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        break;
    }
}

void flush() {
    if (g.batch.empty()) return;
    refresh_sample();
    bind_vram_target();
    const BatchKey &k = g.key;
    glEnable(GL_SCISSOR_TEST);
    glScissor(k.clip[0] * g.scale, k.clip[1] * g.scale, (k.clip[2] - k.clip[0] + 1) * g.scale,
              (k.clip[3] - k.clip[1] + 1) * g.scale);
    glUseProgram(g.draw_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.sample_tex);
    glUniform1i(glGetUniformLocation(g.draw_prog, "u_vram"), 0);
    glUniform1i(glGetUniformLocation(g.draw_prog, "u_scale"), g.scale);
    glUniform4i(glGetUniformLocation(g.draw_prog, "u_texwin"), k.texwin[0], k.texwin[1], k.texwin[2], k.texwin[3]);
    glUniform1i(glGetUniformLocation(g.draw_prog, "u_set_mask"), k.set_mask);
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, g.batch.size() * sizeof(Vertex), g.batch.data(), GL_STREAM_DRAW);
    GLint pass_loc = glGetUniformLocation(g.draw_prog, "u_pass");
    if (k.blend < 0) {
        set_blend(-1);
        glUniform1i(pass_loc, 0);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(g.batch.size()));
    } else {
        set_blend(-1);
        glUniform1i(pass_loc, 1);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(g.batch.size()));
        set_blend(k.blend);
        glUniform1i(pass_loc, 2);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(g.batch.size()));
        set_blend(-1);
    }
    g.batch.clear();
    g.sample_stale = true;
}

// ---------------------------------------------------------------- primitives

struct PrimVertex {
    int x, y;
    uint8_t r, g, b;
    int u, v;
};

void begin_prim(bool semi) {
    BatchKey k;
    k.blend = semi ? (g.texpage >> 5) & 3 : -1;
    k.clip[0] = g.clip_x1;
    k.clip[1] = g.clip_y1;
    k.clip[2] = g.clip_x2;
    k.clip[3] = g.clip_y2;
    std::memcpy(k.texwin, g.texwin, sizeof(k.texwin));
    k.set_mask = g.set_mask;
    if (!(k == g.key)) {
        flush();
        g.key = k;
    }
}

void push(const PrimVertex &p, uint16_t clut, uint16_t tpage) {
    Vertex v;
    v.x = float(p.x);
    v.y = float(p.y);
    v.r = p.r;
    v.g = p.g;
    v.b = p.b;
    v.a = 255;
    v.u = float(p.u);
    v.v = float(p.v);
    v.clut = clut;
    v.tpage = tpage;
    g.batch.push_back(v);
}

int sext11(uint32_t v) {
    return int32_t(v << 21) >> 21;
}

void polygon(const uint32_t *w, uint32_t cmd) {
    bool gouraud = cmd & 0x10, quad = cmd & 0x08, textured = cmd & 0x04, semi = cmd & 0x02, raw = cmd & 0x01;
    int n = quad ? 4 : 3;
    PrimVertex pv[4];
    uint16_t clut = 0, tpage = g.texpage & 0x1FF;
    int i = 0;
    uint32_t color = w[i++];
    for (int k = 0; k < n; k++) {
        if (gouraud && k > 0) color = w[i++];
        uint32_t xy = w[i++];
        pv[k].x = sext11(xy) + g.off_x;
        pv[k].y = sext11(xy >> 16) + g.off_y;
        pv[k].r = color & 0xFF;
        pv[k].g = (color >> 8) & 0xFF;
        pv[k].b = (color >> 16) & 0xFF;
        pv[k].u = pv[k].v = 0;
        if (textured) {
            uint32_t uv = w[i++];
            pv[k].u = uv & 0xFF;
            pv[k].v = (uv >> 8) & 0xFF;
            if (k == 0) clut = uv >> 16;
            if (k == 1) {
                tpage = (uv >> 16) & 0x1FF;
                g.texpage = uint16_t((g.texpage & ~0x1FF) | tpage);
            }
        }
    }
    uint16_t flags = tpage | (textured ? V_TEXTURED : 0) | (raw ? V_RAW : 0) | (semi ? V_SEMI : 0);
    if ((g.texpage >> 11) & 1) flags &= ~V_TEXTURED;  // texture disable bit
    begin_prim(semi);
    auto tri = [&](int a, int b, int c) {
        int xs[3] = {pv[a].x, pv[b].x, pv[c].x}, ys[3] = {pv[a].y, pv[b].y, pv[c].y};
        int w_ = *std::max_element(xs, xs + 3) - *std::min_element(xs, xs + 3);
        int h_ = *std::max_element(ys, ys + 3) - *std::min_element(ys, ys + 3);
        if (w_ >= 1024 || h_ >= 512) return;  // hardware rejects oversized triangles
        push(pv[a], clut, flags);
        push(pv[b], clut, flags);
        push(pv[c], clut, flags);
    };
    tri(0, 1, 2);
    if (quad) tri(1, 2, 3);
}

void rect(const uint32_t *w, uint32_t cmd) {
    bool textured = cmd & 0x04, semi = cmd & 0x02, raw = cmd & 0x01;
    int i = 0;
    uint32_t color = w[i++];
    uint32_t xy = w[i++];
    int u = 0, v = 0;
    uint16_t clut = 0;
    if (textured) {
        uint32_t uv = w[i++];
        u = uv & 0xFF;
        v = (uv >> 8) & 0xFF;
        clut = uv >> 16;
    }
    int width, height;
    switch ((cmd >> 3) & 3) {
    case 0: width = w[i] & 0x3FF; height = (w[i] >> 16) & 0x1FF; break;
    case 1: width = height = 1; break;
    case 2: width = height = 8; break;
    default: width = height = 16; break;
    }
    if (width == 0 || height == 0) return;
    int x = sext11(xy) + g.off_x, y = sext11(xy >> 16) + g.off_y;
    uint16_t flags = (g.texpage & 0x1FF) | (textured ? V_TEXTURED : 0) | (raw ? V_RAW : 0) | (semi ? V_SEMI : 0);
    if ((g.texpage >> 11) & 1) flags &= ~V_TEXTURED;
    PrimVertex p[4];
    for (int k = 0; k < 4; k++) {
        p[k].r = color & 0xFF;
        p[k].g = (color >> 8) & 0xFF;
        p[k].b = (color >> 16) & 0xFF;
        p[k].x = x + ((k & 1) ? width : 0);
        p[k].y = y + ((k & 2) ? height : 0);
        p[k].u = u + ((k & 1) ? width : 0);
        p[k].v = v + ((k & 2) ? height : 0);
    }
    begin_prim(semi);
    for (int k : {0, 1, 2, 1, 2, 3}) push(p[k], clut, flags);
}

void line_segment(const PrimVertex &a, const PrimVertex &b, bool semi) {
    // A one-pixel-wide quad around the segment, widened along the minor axis.
    float dx = float(b.x - a.x), dy = float(b.y - a.y);
    bool horizontal = std::abs(dx) >= std::abs(dy);
    PrimVertex q[4] = {a, a, b, b};
    float ox = horizontal ? 0.0f : 0.5f, oy = horizontal ? 0.5f : 0.0f;
    begin_prim(semi);
    uint16_t flags = semi ? V_SEMI : 0;
    Vertex v[4];
    for (int k = 0; k < 4; k++) {
        float s = (k & 1) ? 1.0f : -1.0f;
        v[k] = Vertex{float(q[k].x) + 0.5f + s * ox, float(q[k].y) + 0.5f + s * oy, q[k].r, q[k].g, q[k].b, 255,
                      0, 0, 0, flags};
    }
    for (int k : {0, 1, 2, 1, 2, 3}) g.batch.push_back(v[k]);
}

// Number of words a GP0 command needs (0 = variable length handled by the caller).
int command_words(uint32_t cmd) {
    switch (cmd >> 5) {
    case 1: {  // polygon
        int n = (cmd & 0x08) ? 4 : 3;
        int per = 1 + ((cmd & 0x04) ? 1 : 0);
        return 1 + n * per + ((cmd & 0x10) ? n - 1 : 0);
    }
    case 2:  // line (polylines are variable)
        if (cmd & 0x08) return 0;
        return (cmd & 0x10) ? 4 : 3;
    case 3: {  // rectangle
        int n = 2 + ((cmd & 0x04) ? 1 : 0);
        return n + (((cmd >> 3) & 3) == 0 ? 1 : 0);
    }
    case 4: return 4;  // VRAM -> VRAM
    case 5: return 3;  // CPU -> VRAM (+ data)
    case 6: return 3;  // VRAM -> CPU
    default:
        return cmd == 0x02 ? 3 : 1;
    }
}

void set_draw_mode(uint32_t w) {
    g.texpage = uint16_t(w & 0xFFF);
}

// Executes one complete command; returns words consumed (0 if more words are needed).
int execute(const uint32_t *w, int avail) {
    uint32_t cmd = w[0] >> 24;
    if ((cmd >> 5) == 2 && (cmd & 0x08)) {  // polyline: until the 0x5xxx5xxx terminator
        bool gouraud = cmd & 0x10;
        int i = 1;
        std::vector<PrimVertex> pts;
        uint32_t color = w[0];
        while (true) {
            if (gouraud && !pts.empty()) {
                if (i >= avail) return 0;
                if ((w[i] & 0xF000F000) == 0x50005000) return i + 1;
                color = w[i++];
            }
            if (i >= avail) return 0;
            if (!pts.empty() && (w[i] & 0xF000F000) == 0x50005000) break;
            PrimVertex p{sext11(w[i]) + g.off_x, sext11(w[i] >> 16) + g.off_y, uint8_t(color), uint8_t(color >> 8),
                         uint8_t(color >> 16), 0, 0};
            if (!pts.empty()) line_segment(pts.back(), p, cmd & 0x02);
            pts.push_back(p);
            i++;
        }
        return i + 1;
    }
    int n = command_words(cmd);
    if (avail < n) return 0;
    switch (cmd >> 5) {
    case 1:
        polygon(w, cmd);
        return n;
    case 2: {
        uint32_t c1 = (cmd & 0x10) ? w[2] : w[0];
        uint32_t xy1 = (cmd & 0x10) ? w[3] : w[2];
        PrimVertex a{sext11(w[1]) + g.off_x, sext11(w[1] >> 16) + g.off_y, uint8_t(w[0]), uint8_t(w[0] >> 8),
                     uint8_t(w[0] >> 16), 0, 0};
        PrimVertex b{sext11(xy1) + g.off_x, sext11(xy1 >> 16) + g.off_y, uint8_t(c1), uint8_t(c1 >> 8),
                     uint8_t(c1 >> 16), 0, 0};
        line_segment(a, b, cmd & 0x02);
        return n;
    }
    case 3:
        rect(w, cmd);
        return n;
    case 4: {
        Rect r{int(w[1] & 0x3FF), int((w[1] >> 16) & 0x1FF), int(((w[3] - 1) & 0x3FF) + 1),
               int((((w[3] >> 16) - 1) & 0x1FF) + 1)};
        move_image(r, w[2] & 0x3FF, (w[2] >> 16) & 0x1FF);
        return n;
    }
    case 5: {
        int rw = int(((w[2] - 1) & 0x3FF) + 1), rh = int((((w[2] >> 16) - 1) & 0x1FF) + 1);
        int words = (rw * rh + 1) / 2;
        if (avail < 3 + words) return 0;
        load_image(Rect{int(w[1] & 0x3FF), int((w[1] >> 16) & 0x1FF), rw, rh},
                   reinterpret_cast<const uint16_t *>(w + 3));
        return 3 + words;
    }
    case 6:
        PSX_TODO_ONCE("GP0 VRAM->CPU transfer through the command stream is not supported");
        return n;
    default:
        break;
    }
    switch (cmd) {
    case 0x02: {  // fill rectangle (ignores clip, offset, mask)
        Rect r{int(w[1] & 0x3F0), int((w[1] >> 16) & 0x1FF), int(((w[2] & 0x3FF) + 15) & ~15),
               int((w[2] >> 16) & 0x1FF)};
        clear_image(r, uint8_t(w[0]), uint8_t(w[0] >> 8), uint8_t(w[0] >> 16));
        break;
    }
    case 0xE1: set_draw_mode(w[0]); break;
    case 0xE2:
        g.texwin[0] = (w[0] & 31) * 8;
        g.texwin[1] = ((w[0] >> 5) & 31) * 8;
        g.texwin[2] = ((w[0] >> 10) & 31) * 8;
        g.texwin[3] = ((w[0] >> 15) & 31) * 8;
        break;
    case 0xE3:
        g.clip_x1 = w[0] & 0x3FF;
        g.clip_y1 = (w[0] >> 10) & 0x1FF;
        break;
    case 0xE4:
        g.clip_x2 = w[0] & 0x3FF;
        g.clip_y2 = (w[0] >> 10) & 0x1FF;
        break;
    case 0xE5:
        g.off_x = sext11(w[0]);
        g.off_y = sext11(w[0] >> 11);
        break;
    case 0xE6:
        g.set_mask = w[0] & 1;
        g.check_mask = w[0] & 2;
        if (g.check_mask) PSX_TODO_ONCE("GPU mask-bit test is not implemented");
        break;
    default:
        break;  // nop, cache clear, IRQ
    }
    return n;
}

}  // namespace

// ---------------------------------------------------------------- public API

bool init(int scale) {
    if (!gl::load()) return false;
    g.scale = std::max(1, scale);
    int w = VRAM_W * g.scale, h = VRAM_H * g.scale;
    g.vram_tex = make_tex(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE);
    g.vram_fbo = make_fbo(g.vram_tex);
    g.sample_tex = make_tex(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE);
    g.sample_fbo = make_fbo(g.sample_tex);
    g.read_tex = make_tex(GL_RGBA8, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_BYTE);
    g.read_fbo = make_fbo(g.read_tex);
    g.upload_tex = make_tex(GL_R16UI, VRAM_W, VRAM_H, GL_RED_INTEGER, GL_UNSIGNED_SHORT);

    const std::string header = "#version 330 core\n";
    g.draw_prog = program(kDrawVS, header + kVramFetch + kDrawFS);
    g.upload_prog = program(kQuadVS, kUploadFS);
    g.present_prog = program(kQuadVS, header + kVramFetch + kPresentFS);

    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, r));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), (void *)offsetof(Vertex, clut));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), (void *)offsetof(Vertex, tpage));

    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenVertexArrays(1, &g.quad_vao);
    glBindVertexArray(g.quad_vao);
    glGenBuffers(1, &g.quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    reset();
    clear_image(Rect{0, 0, VRAM_W, VRAM_H}, 0, 0, 0);
    LOGI("GPU: OpenGL %s, VRAM scale %dx", reinterpret_cast<const char *>(glGetString(GL_VERSION)), g.scale);
    return true;
}

void reset() {
    flush();
    g.texpage = 0;
    std::memset(g.texwin, 0, sizeof(g.texwin));
    g.clip_x1 = g.clip_y1 = 0;
    g.clip_x2 = VRAM_W - 1;
    g.clip_y2 = VRAM_H - 1;
    g.off_x = g.off_y = 0;
    g.set_mask = g.check_mask = false;
    g.pending.clear();
}

void gp0(const uint32_t *words, int count) {
    // Finish a command that was split across calls.
    if (!g.pending.empty()) {
        g.pending.insert(g.pending.end(), words, words + count);
        std::vector<uint32_t> buf;
        buf.swap(g.pending);
        words = buf.data();
        count = int(buf.size());
        int pos = 0;
        while (pos < count) {
            int used = execute(words + pos, count - pos);
            if (used == 0) {
                g.pending.assign(words + pos, words + count);
                return;
            }
            pos += used;
        }
        return;
    }
    int pos = 0;
    while (pos < count) {
        int used = execute(words + pos, count - pos);
        if (used == 0) {
            g.pending.assign(words + pos, words + count);
            return;
        }
        pos += used;
    }
}

void draw_otag(uint32_t addr) {
    // Each packet: header (next pointer in bits 0-23, word count in bits 24-31) + GP0 words.
    for (int guard = 0; guard < 1 << 20; guard++) {
        const uint32_t *p = psx::ptr_as<const uint32_t>(addr);
        if (!p) {
            LOGW("draw_otag: bad packet address %08X", addr);
            return;
        }
        uint32_t header = p[0];
        int words = header >> 24;
        if (words) gp0(p + 1, words);
        uint32_t next = header & 0xFFFFFF;
        if (next == 0xFFFFFF) break;
        addr = 0x80000000u | next;
    }
    g.pending.clear();
}

void load_image(const Rect &r, const uint16_t *pixels) {
    flush();
    // Stage the words in the R16UI upload texture, then convert into the VRAM framebuffer.
    glBindTexture(GL_TEXTURE_2D, g.upload_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if (r.x + r.w <= VRAM_W && r.y + r.h <= VRAM_H) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pixels);
    } else {
        // VRAM wraps at the edges: upload row by row, splitting rows that cross x=1024.
        for (int y = 0; y < r.h; y++) {
            const uint16_t *row = pixels + size_t(y) * r.w;
            int vy = (r.y + y) & (VRAM_H - 1);
            int first = std::min(r.w, VRAM_W - r.x);
            glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, vy, first, 1, GL_RED_INTEGER, GL_UNSIGNED_SHORT, row);
            if (first < r.w)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, vy, r.w - first, 1, GL_RED_INTEGER, GL_UNSIGNED_SHORT, row + first);
        }
    }
    bind_vram_target();
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glUseProgram(g.upload_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.upload_tex);
    glUniform1i(glGetUniformLocation(g.upload_prog, "u_src"), 0);
    glUniform1i(glGetUniformLocation(g.upload_prog, "u_scale"), g.scale);
    auto blit_part = [&](int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) return;
        glScissor(x * g.scale, y * g.scale, w * g.scale, h * g.scale);
        set_dst(g.upload_prog, x, y, w, h);
        draw_quad();
    };
    int w0 = std::min(r.w, VRAM_W - r.x), h0 = std::min(r.h, VRAM_H - r.y);
    blit_part(r.x, r.y, w0, h0);
    blit_part(0, r.y, r.w - w0, h0);
    blit_part(r.x, 0, w0, r.h - h0);
    blit_part(0, 0, r.w - w0, r.h - h0);
    g.sample_stale = true;
}

void store_image(const Rect &r, uint16_t *pixels) {
    flush();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g.vram_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.read_fbo);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(r.x * g.scale, r.y * g.scale, (r.x + r.w) * g.scale, (r.y + r.h) * g.scale, r.x, r.y, r.x + r.w,
                      r.y + r.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g.read_fbo);
    std::vector<uint8_t> rgba(size_t(r.w) * r.h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    for (size_t i = 0; i < size_t(r.w) * r.h; i++) {
        const uint8_t *c = &rgba[i * 4];
        auto five = [](uint8_t v) { return uint16_t((v * 31 + 127) / 255); };
        pixels[i] = uint16_t(five(c[0]) | (five(c[1]) << 5) | (five(c[2]) << 10) | ((c[3] >= 128) << 15));
    }
}

void move_image(const Rect &r, int x, int y) {
    flush();
    refresh_sample();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g.sample_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.vram_fbo);
    glDisable(GL_SCISSOR_TEST);
    int s = g.scale;
    glBlitFramebuffer(r.x * s, r.y * s, (r.x + r.w) * s, (r.y + r.h) * s, x * s, y * s, (x + r.w) * s, (y + r.h) * s,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g.sample_stale = true;
}

void clear_image(const Rect &r, uint8_t red, uint8_t green, uint8_t blue) {
    flush();
    bind_vram_target();
    glEnable(GL_SCISSOR_TEST);
    glScissor(r.x * g.scale, r.y * g.scale, r.w * g.scale, r.h * g.scale);
    auto q = [](uint8_t v) { return float(v >> 3) / 31.0f; };
    glClearColor(q(red), q(green), q(blue), 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    g.sample_stale = true;
}

void set_display(const DisplayMode &mode) {
    g.display = mode;
}

const DisplayMode &display() {
    return g.display;
}

// Draws the display area into the viewport of the bound framebuffer.
static void draw_display() {
    glUseProgram(g.present_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.vram_tex);
    glUniform1i(glGetUniformLocation(g.present_prog, "u_vram"), 0);
    glUniform1i(glGetUniformLocation(g.present_prog, "u_scale"), g.scale);
    glUniform4i(glGetUniformLocation(g.present_prog, "u_area"), g.display.x, g.display.y, g.display.w, g.display.h);
    glUniform1i(glGetUniformLocation(g.present_prog, "u_rgb24"), g.display.rgb24);
    // VRAM row 0 is the top of the picture; flip into window coordinates.
    glUniform4f(glGetUniformLocation(g.present_prog, "u_dst"), -1, 1, 1, -1);
    draw_quad();
}

void present(int window_w, int window_h) {
    flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, window_w, window_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!g.display.enabled) return;
    // 4:3 letterbox.
    int vw = window_w, vh = window_w * 3 / 4;
    if (vh > window_h) {
        vh = window_h;
        vw = window_h * 4 / 3;
    }
    glViewport((window_w - vw) / 2, (window_h - vh) / 2, vw, vh);
    draw_display();
}

bool capture(std::vector<uint8_t> &rgb, int *out_w, int *out_h) {
    if (!g.display.enabled) return false;
    flush();
    static GLuint fbo, tex;
    static int fw, fh;
    int h = g.display.h * g.scale, w = h * 4 / 3;
    if (w != fw || h != fh) {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        if (!fbo) glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        fw = w;
        fh = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, w, h);
    draw_display();
    rgb.resize(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    *out_w = w;
    *out_h = h;
    return true;
}

}  // namespace gpu
