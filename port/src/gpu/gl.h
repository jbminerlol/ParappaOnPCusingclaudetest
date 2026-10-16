// Minimal OpenGL 3.3 core loader (functions resolved through SDL_GL_GetProcAddress).
#pragma once
#include <SDL3/SDL_opengl.h>

#define PSX_GL_FUNCS(X)                                                                                          \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer)                                   \
    X(PFNGLBUFFERDATAPROC, glBufferData) X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                             \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                   \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource)                           \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv)                           \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                 \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram)                             \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                 \
    X(PFNGLDELETESHADERPROC, glDeleteShader) X(PFNGLUSEPROGRAMPROC, glUseProgram)                               \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i)                     \
    X(PFNGLUNIFORM2IPROC, glUniform2i) X(PFNGLUNIFORM4IPROC, glUniform4i) X(PFNGLUNIFORM1FPROC, glUniform1f)    \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                                                          \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)               \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                                    \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBLENDCOLORPROC, glBlendColor) X(PFNGLACTIVETEXTUREPROC, glActiveTexture)

// Each function is a pointer named gl::fn_<name>; the GL names below map onto them (a few are also
// declared, but not exported, by the system GL header).
namespace gl {
#define PSX_GL_DECLARE(type, name) extern type fn_##name;
PSX_GL_FUNCS(PSX_GL_DECLARE)
#undef PSX_GL_DECLARE

// Resolves every function above; returns false (and logs) if one is missing.
bool load();
}  // namespace gl

#define glGenBuffers gl::fn_glGenBuffers
#define glBindBuffer gl::fn_glBindBuffer
#define glBufferData gl::fn_glBufferData
#define glBufferSubData gl::fn_glBufferSubData
#define glDeleteBuffers gl::fn_glDeleteBuffers
#define glGenVertexArrays gl::fn_glGenVertexArrays
#define glBindVertexArray gl::fn_glBindVertexArray
#define glEnableVertexAttribArray gl::fn_glEnableVertexAttribArray
#define glVertexAttribPointer gl::fn_glVertexAttribPointer
#define glVertexAttribIPointer gl::fn_glVertexAttribIPointer
#define glCreateShader gl::fn_glCreateShader
#define glShaderSource gl::fn_glShaderSource
#define glCompileShader gl::fn_glCompileShader
#define glGetShaderiv gl::fn_glGetShaderiv
#define glGetShaderInfoLog gl::fn_glGetShaderInfoLog
#define glCreateProgram gl::fn_glCreateProgram
#define glAttachShader gl::fn_glAttachShader
#define glLinkProgram gl::fn_glLinkProgram
#define glGetProgramiv gl::fn_glGetProgramiv
#define glGetProgramInfoLog gl::fn_glGetProgramInfoLog
#define glDeleteShader gl::fn_glDeleteShader
#define glUseProgram gl::fn_glUseProgram
#define glGetUniformLocation gl::fn_glGetUniformLocation
#define glUniform1i gl::fn_glUniform1i
#define glUniform2i gl::fn_glUniform2i
#define glUniform4i gl::fn_glUniform4i
#define glUniform1f gl::fn_glUniform1f
#define glUniform4f gl::fn_glUniform4f
#define glGenFramebuffers gl::fn_glGenFramebuffers
#define glBindFramebuffer gl::fn_glBindFramebuffer
#define glFramebufferTexture2D gl::fn_glFramebufferTexture2D
#define glCheckFramebufferStatus gl::fn_glCheckFramebufferStatus
#define glBlitFramebuffer gl::fn_glBlitFramebuffer
#define glBlendEquationSeparate gl::fn_glBlendEquationSeparate
#define glBlendFuncSeparate gl::fn_glBlendFuncSeparate
#define glBlendColor gl::fn_glBlendColor
#define glActiveTexture gl::fn_glActiveTexture
