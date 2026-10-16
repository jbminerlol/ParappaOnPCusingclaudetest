#include "psyq/libgte.h"

#include <cmath>
#include <cstdlib>

#include "psx/memory.h"

namespace {

constexpr uint32_t RCOSSIN_TBL = 0x800581C0u;  // 4096 x {short sin, short cos} in the EXE
constexpr uint32_t RATAN_TBL = 0x8005C1C0u;    // 1025 x short

gte::State g_state;

int32_t tbl_sin(int32_t a) {
    return psx::ptr_as<const int16_t>(RCOSSIN_TBL)[(a & 0xFFF) * 2];
}

int32_t tbl_cos(int32_t a) {
    return psx::ptr_as<const int16_t>(RCOSSIN_TBL)[(a & 0xFFF) * 2 + 1];
}

int16_t clamp16(int32_t v) {
    return int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

}  // namespace

namespace gte {

State &state() {
    return g_state;
}

void set_projection(int h) {
    g_state.h = h;
}

int32_t sin12(int32_t a) {
    return a < 0 ? -tbl_sin(-a) : tbl_sin(a);
}

int32_t cos12(int32_t a) {
    return tbl_cos(std::abs(a));
}

void rot_trans(const MATRIX &m, const SVECTOR &v, int32_t out[3]) {
    for (int i = 0; i < 3; i++)
        out[i] = int32_t((int64_t(m.m[i][0]) * v.vx + int64_t(m.m[i][1]) * v.vy + int64_t(m.m[i][2]) * v.vz) >> 12) +
                 m.t[i];
}

bool project(const int32_t v[3], int32_t *sx, int32_t *sy) {
    if (v[2] <= 0) return false;
    *sx = int32_t(int64_t(v[0]) * g_state.h / v[2]) + g_state.ofx;
    *sy = int32_t(int64_t(v[1]) * g_state.h / v[2]) + g_state.ofy;
    return true;
}

void mul_matrix(const MATRIX &a, const MATRIX &b, MATRIX &out) {
    MATRIX r = out;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i][j] = clamp16(int32_t(
                (int64_t(a.m[i][0]) * b.m[0][j] + int64_t(a.m[i][1]) * b.m[1][j] + int64_t(a.m[i][2]) * b.m[2][j]) >> 12));
    out = r;
}

}  // namespace gte

extern "C" {

int psyq_rsin(int a) {
    return gte::sin12(a);
}

int psyq_rcos(int a) {
    return gte::cos12(a);
}

int psyq_ratan2(int32_t y, int32_t x) {
    const int16_t *tbl = psx::ptr_as<const int16_t>(RATAN_TBL);
    bool nx = x < 0, ny = y < 0;
    int64_t ax = std::llabs(int64_t(x)), ay = std::llabs(int64_t(y));
    if (ax == 0 && ay == 0) return 0;
    int32_t r;
    if (ax > ay) {
        r = tbl[(ay << 10) / ax];
    } else {
        r = 0x400 - tbl[(ax << 10) / ay];
    }
    if (nx) r = 0x800 - r;
    if (ny) r = -r;
    return r;
}

int32_t psyq_VectorNormal(const VECTOR *in, VECTOR *out) {
    int64_t sq = int64_t(in->vx) * in->vx + int64_t(in->vy) * in->vy + int64_t(in->vz) * in->vz;
    double len = std::sqrt(double(sq));
    if (len == 0) {
        out->vx = out->vy = out->vz = 0;
        return 0;
    }
    out->vx = int32_t(in->vx * 4096.0 / len);
    out->vy = int32_t(in->vy * 4096.0 / len);
    out->vz = int32_t(in->vz * 4096.0 / len);
    return int32_t(sq);
}

// Vertex morphing: dst[i] += src[i] * p >> 12 (SVECTOR arrays).
int psyq_gteMIMefunc(SVECTOR *dst, const SVECTOR *src, int n, int p) {
    for (int i = 0; i < n; i++) {
        dst[i].vx = int16_t(dst[i].vx + (src[i].vx * p >> 12));
        dst[i].vy = int16_t(dst[i].vy + (src[i].vy * p >> 12));
        dst[i].vz = int16_t(dst[i].vz + (src[i].vz * p >> 12));
    }
    return 0;
}

// m2 = m0 * m1 (rotation), m2.t = m0.rot * m1.t + m0.t
MATRIX *psyq_CompMatrix(const MATRIX *m0, const MATRIX *m1, MATRIX *m2) {
    MATRIX r;
    gte::mul_matrix(*m0, *m1, r);
    for (int i = 0; i < 3; i++)
        r.t[i] = int32_t((int64_t(m0->m[i][0]) * m1->t[0] + int64_t(m0->m[i][1]) * m1->t[1] +
                          int64_t(m0->m[i][2]) * m1->t[2]) >> 12) + m0->t[i];
    *m2 = r;
    return m2;
}

VECTOR *psyq_ApplyMatrix(const MATRIX *m, const SVECTOR *v, VECTOR *out) {
    int32_t r[3];
    for (int i = 0; i < 3; i++)
        r[i] = int32_t((int64_t(m->m[i][0]) * v->vx + int64_t(m->m[i][1]) * v->vy + int64_t(m->m[i][2]) * v->vz) >> 12);
    out->vx = r[0];
    out->vy = r[1];
    out->vz = r[2];
    return out;
}

MATRIX *psyq_ScaleMatrix(MATRIX *m, const VECTOR *s) {
    const int32_t sv[3] = {s->vx, s->vy, s->vz};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m->m[i][j] = int16_t(m->m[i][j] * sv[j] >> 12);
    return m;
}

MATRIX *psyq_TransMatrix(MATRIX *m, const VECTOR *v) {
    m->t[0] = v->vx;
    m->t[1] = v->vy;
    m->t[2] = v->vz;
    return m;
}

// m = R(axis, angle) * m, as libgte's RotMatrixX/Y/Z.
static MATRIX *rot_rows(MATRIX *m, int a_row, int b_row, int32_t c, int32_t s) {
    for (int j = 0; j < 3; j++) {
        int32_t a = m->m[a_row][j], b = m->m[b_row][j];
        m->m[a_row][j] = int16_t((c * a - s * b) >> 12);
        m->m[b_row][j] = int16_t((s * a + c * b) >> 12);
    }
    return m;
}

MATRIX *psyq_RotMatrixX(int r, MATRIX *m) {
    return rot_rows(m, 1, 2, gte::cos12(r), gte::sin12(r));
}

MATRIX *psyq_RotMatrixY(int r, MATRIX *m) {
    return rot_rows(m, 0, 2, gte::cos12(r), -gte::sin12(r));
}

MATRIX *psyq_RotMatrixZ(int r, MATRIX *m) {
    return rot_rows(m, 0, 1, gte::cos12(r), gte::sin12(r));
}

}  // extern "C"
