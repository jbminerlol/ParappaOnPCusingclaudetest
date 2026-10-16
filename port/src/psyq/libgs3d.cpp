// libgs 3D: coordinate systems, the view (GsSetRefView2) and TMD model setup.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "psx/log.h"
#include "psx/memory.h"
#include "psyq/libgs.h"
#include "psyq/libgte.h"

namespace {

constexpr uint32_t GS_IDMATRIX = 0x80091838u;
MATRIX g_ws;  // world -> screen (GsWSMATRIX)

const MATRIX &identity() {
    return *psx::ptr_as<const MATRIX>(GS_IDMATRIX);
}

// a * b with translation: (a.rot * b.rot, a.rot * b.t + a.t)
MATRIX compose(const MATRIX &a, const MATRIX &b) {
    MATRIX r;
    gte::mul_matrix(a, b, r);
    for (int i = 0; i < 3; i++)
        r.t[i] = int32_t((int64_t(a.m[i][0]) * b.t[0] + int64_t(a.m[i][1]) * b.t[1] + int64_t(a.m[i][2]) * b.t[2]) >> 12) +
                 a.t[i];
    return r;
}

MATRIX local_to_world(const GsCOORDINATE2 *c) {
    MATRIX m = c->coord;
    for (const GsCOORDINATE2 *p = c->super; p && psx::addr_of(p) > 1; p = psx::mirror(p)->super)
        m = compose(psx::mirror(p)->coord, m);
    return m;
}

void set_view(const GsRVIEW2 *v, bool large) {
    (void)large;
    // Camera looks from vp towards vr; rz is the twist about the view axis (1/4096 turn units).
    double dx = double(v->vrx) - v->vpx, dy = double(v->vry) - v->vpy, dz = double(v->vrz) - v->vpz;
    double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len == 0) return;
    double fz[3] = {dx / len, dy / len, dz / len};
    // PS1 world and screen Y both point down: x = down x z, y = z x x. A camera at (0,0,-d) looking
    // at the origin then gets the identity rotation.
    double up[3] = {0, 1, 0};
    double fx[3] = {up[1] * fz[2] - up[2] * fz[1], up[2] * fz[0] - up[0] * fz[2], up[0] * fz[1] - up[1] * fz[0]};
    double lx = std::sqrt(fx[0] * fx[0] + fx[1] * fx[1] + fx[2] * fx[2]);
    if (lx < 1e-9) {
        fx[0] = 1;
        fx[1] = fx[2] = 0;
        lx = 1;
    }
    for (double &c : fx) c /= lx;
    double fy[3] = {fz[1] * fx[2] - fz[2] * fx[1], fz[2] * fx[0] - fz[0] * fx[2], fz[0] * fx[1] - fz[1] * fx[0]};
    double ang = v->rz / 4096.0 * 2 * 3.14159265358979;
    double c = std::cos(ang), s = std::sin(ang);
    double rx[3], ry[3];
    for (int i = 0; i < 3; i++) {
        rx[i] = c * fx[i] + s * fy[i];
        ry[i] = -s * fx[i] + c * fy[i];
    }
    MATRIX m{};
    for (int j = 0; j < 3; j++) {
        m.m[0][j] = int16_t(std::lround(rx[j] * 4096));
        m.m[1][j] = int16_t(std::lround(ry[j] * 4096));
        m.m[2][j] = int16_t(std::lround(fz[j] * 4096));
    }
    const int32_t vp[3] = {v->vpx, v->vpy, v->vpz};
    for (int i = 0; i < 3; i++)
        m.t[i] = -int32_t((int64_t(m.m[i][0]) * vp[0] + int64_t(m.m[i][1]) * vp[1] + int64_t(m.m[i][2]) * vp[2]) >> 12);
    // The reference view is relative to 'super': world -> screen = view * inverse(super) is not
    // needed by this game (super is always WORLD), so only warn otherwise.
    if (v->super && psx::addr_of(v->super) > 1) PSX_TODO_ONCE("GsSetRefView2 with a super coordinate");
    g_ws = m;
}

}  // namespace

namespace libgs3d {
const MATRIX &ws_matrix() { return g_ws; }
}  // namespace libgs3d

extern "C" {

int psyq_GsInitCoordinate2(GsCOORDINATE2 *super, GsCOORDINATE2 *c) {
    c = psx::mirror(c);
    c->coord = identity();
    c->super = super;
    c->flg = 0;
    if (super && psx::addr_of(super) > 1) super->sub = c;
    return 0;
}

// m2 = m1 * m2 (rotation and translation)
int psyq_GsMulCoord2(const MATRIX *m1, MATRIX *m2) {
    *m2 = compose(*m1, *m2);
    return 0;
}

int psyq_GsSetLsMatrix(const MATRIX *m) {
    gte::state().rot = *m;
    return 0;
}

int psyq_GsGetLw(GsCOORDINATE2 *c, MATRIX *out) {
    c = psx::mirror(c);
    *out = local_to_world(c);
    c->workm = *out;
    return 0;
}

int psyq_GsGetLs(GsCOORDINATE2 *c, MATRIX *out) {
    c = psx::mirror(c);
    MATRIX lw = local_to_world(c);
    c->workm = lw;
    *out = compose(g_ws, lw);
    return 0;
}

int psyq_GsSetRefView2(const GsRVIEW2 *v) {
    set_view(v, false);
    return 0;
}

int psyq_GsSetRefView2L(const GsRVIEW2 *v) {
    set_view(v, true);
    return 0;
}

// TMD: header {id, flags, nobj}; object table entries of 7 words with offsets relative to the table.
int psyq_GsMapModelingData(uint32_t *tmd) {
    uint32_t *hdr = tmd;  // called with &tmd[1] (flags word follows the ID)
    if (hdr[0] & 1) return 0;  // already mapped (FIXP)
    uint32_t nobj = hdr[1];
    uint32_t *objs = hdr + 2;
    uint32_t base = psx::addr_of(objs);
    for (uint32_t i = 0; i < nobj; i++) {
        uint32_t *o = objs + i * 7;
        o[0] += base;  // vertices
        o[2] += base;  // normals
        o[4] += base;  // primitives
    }
    hdr[0] |= 1;
    return 0;
}

int psyq_GsLinkObject4(uint32_t objtable, GsDOBJ2 *obj, int n) {
    obj->tmd = reinterpret_cast<u_long *>(objtable + uint32_t(n) * 28);
    return 0;
}

// Renders a TMD object with the current local-screen matrix (GsSetLsMatrix). Supports the
// unlit primitive types (flag bit 0 set), which are the only ones PaRappa's models use.
int psyq_GsSortObject4J(GsDOBJ2 *obj, GsOT *ot, int shift, uint32_t scratch) {
    (void)scratch;
    if (!obj || !obj->tmd || int32_t(obj->attribute) < 0) return 0;
    const uint32_t *o = reinterpret_cast<const uint32_t *>(obj->tmd);
    const SVECTOR *verts = psx::ptr_as<const SVECTOR>(o[0]);
    const uint32_t *prim = psx::ptr_as<const uint32_t>(o[4]);
    uint32_t nprim = o[5];
    if (!verts || !prim) return 0;
    const MATRIX &ls = gte::state().rot;
    uint32_t *&work = libgs::work_base();
    int ot_size = 1 << (ot->length & 31);
    uint32_t *ot_org = reinterpret_cast<uint32_t *>(ot->org);

    for (uint32_t p = 0; p < nprim; p++) {
        uint32_t hdr = prim[0];
        int ilen = (hdr >> 8) & 0xFF, flag = (hdr >> 16) & 0xFF, mode = hdr >> 24;
        const uint32_t *w = prim + 1;
        prim += 1 + ilen;
        if (!(flag & 1) || (mode & 0xE0) != 0x20) {
            PSX_TODO_ONCE("GsSortObject4J: TMD primitive mode %02X flag %02X not supported", mode, flag);
            continue;
        }
        bool gouraud = mode & 0x10, quad = mode & 0x08, textured = mode & 0x04;
        int n = quad ? 4 : 3;
        // Layout: [uv words][colour words][vertex index words].
        const uint32_t *uv = textured ? w : nullptr;
        const uint32_t *col = w + (textured ? n : 0);
        const uint32_t *vi = col + (gouraud ? n : 1);
        uint16_t idx[4];
        for (int k = 0; k < n; k++) idx[k] = uint16_t(vi[k / 2] >> ((k & 1) * 16));

        int32_t sx[4], sy[4], sz[4];
        bool visible = true;
        for (int k = 0; k < n && visible; k++) {
            int32_t v[3];
            gte::rot_trans(ls, verts[idx[k]], v);
            sz[k] = v[2];
            visible = gte::project(v, &sx[k], &sy[k]) && sx[k] > -1024 && sx[k] < 1024 && sy[k] > -1024 && sy[k] < 1024;
        }
        if (!visible) continue;
        if (!(flag & 2)) {  // single-sided: cull back faces (NCLIP <= 0)
            int64_t cross = int64_t(sx[1] - sx[0]) * (sy[2] - sy[0]) - int64_t(sy[1] - sy[0]) * (sx[2] - sx[0]);
            if (cross <= 0) continue;
        }
        int32_t z = 0;
        for (int k = 0; k < n; k++) z += sz[k];
        z /= n;
        int slot = std::clamp(int(z >> shift), 0, ot_size - 1);

        uint32_t *pk = work;
        int words = 1;
        for (int k = 0; k < n; k++) {
            uint32_t c = col[gouraud ? k : 0] & 0x00FFFFFFu;
            if (k == 0) pk[words++] = c | uint32_t(mode) << 24;
            else if (gouraud) pk[words++] = c;
            pk[words++] = uint32_t(uint16_t(sx[k])) | uint32_t(uint16_t(sy[k])) << 16;
            if (textured) pk[words++] = uv[k] & (k < 2 ? 0xFFFFFFFFu : 0x0000FFFFu);
        }
        uint32_t *entry = ot_org + slot;
        pk[0] = (*entry & 0x00FFFFFFu) | uint32_t(words - 1) << 24;
        *entry = psx::addr_of(pk) & 0x00FFFFFFu;
        work = pk + words;
    }
    return 0;
}

}  // extern "C"
