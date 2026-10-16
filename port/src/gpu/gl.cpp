#include "gpu/gl.h"

#include <SDL3/SDL.h>

#include "psx/log.h"

namespace gl {

#define PSX_GL_DEFINE(type, name) type fn_##name;
PSX_GL_FUNCS(PSX_GL_DEFINE)
#undef PSX_GL_DEFINE

bool load() {
    bool ok = true;
#define PSX_GL_LOAD(type, name)                                              \
    fn_##name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));        \
    if (!fn_##name) {                                                        \
        LOGE("OpenGL function %s is missing", #name);                        \
        ok = false;                                                          \
    }
    PSX_GL_FUNCS(PSX_GL_LOAD)
#undef PSX_GL_LOAD
    return ok;
}

}  // namespace gl
