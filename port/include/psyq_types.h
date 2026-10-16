/* PsyQ SDK data types, laid out as in the PS1 libraries. Pointer fields are real pointers: the port
 * is a 32-bit build with PS1 RAM mapped at its original address. Shared by game code and runtime. */
#pragma once
#include <stdint.h>

#ifndef PSYQ_BASIC_TYPES
#define PSYQ_BASIC_TYPES
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef uint32_t u_long;
#endif

/* ---- libgte ---- */
typedef struct MATRIX { short m[3][3]; int32_t t[3]; } MATRIX;
typedef struct VECTOR { int32_t vx, vy, vz, pad; } VECTOR;
typedef struct SVECTOR { short vx, vy, vz, pad; } SVECTOR;
typedef struct CVECTOR { u_char r, g, b, cd; } CVECTOR;
typedef struct DVECTOR { short vx, vy; } DVECTOR;

/* ---- libgpu ---- */
typedef struct RECT { short x, y, w, h; } RECT;
typedef struct DR_ENV { u_long tag; u_long code[15]; } DR_ENV;
typedef struct DRAWENV {
    RECT clip; short ofs[2]; RECT tw; u_short tpage; u_char dtd, dfe, isbg, r0, g0, b0; DR_ENV dr_env;
} DRAWENV;
typedef struct DISPENV { RECT disp; RECT screen; u_char isinter, isrgb24, pad0, pad1; } DISPENV;
typedef struct P_TAG { unsigned addr : 24; unsigned len : 8; u_char r0, g0, b0, code; } P_TAG;
typedef struct POLY_F3 { u_long tag; u_char r0, g0, b0, code; short x0, y0, x1, y1, x2, y2; } POLY_F3;
typedef struct POLY_F4 { u_long tag; u_char r0, g0, b0, code; short x0, y0, x1, y1, x2, y2, x3, y3; } POLY_F4;
typedef struct POLY_FT3 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char u0, v0; u_short clut;
    short x1, y1; u_char u1, v1; u_short tpage; short x2, y2; u_char u2, v2; u_short pad1;
} POLY_FT3;
typedef struct POLY_FT4 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char u0, v0; u_short clut;
    short x1, y1; u_char u1, v1; u_short tpage; short x2, y2; u_char u2, v2; u_short pad1;
    short x3, y3; u_char u3, v3; u_short pad2;
} POLY_FT4;
typedef struct POLY_G3 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char r1, g1, b1, pad1; short x1, y1;
    u_char r2, g2, b2, pad2; short x2, y2;
} POLY_G3;
typedef struct POLY_G4 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char r1, g1, b1, pad1; short x1, y1;
    u_char r2, g2, b2, pad2; short x2, y2; u_char r3, g3, b3, pad3; short x3, y3;
} POLY_G4;
typedef struct POLY_GT3 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char u0, v0; u_short clut;
    u_char r1, g1, b1, p1; short x1, y1; u_char u1, v1; u_short tpage;
    u_char r2, g2, b2, p2; short x2, y2; u_char u2, v2; u_short pad2;
} POLY_GT3;
typedef struct POLY_GT4 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char u0, v0; u_short clut;
    u_char r1, g1, b1, p1; short x1, y1; u_char u1, v1; u_short tpage;
    u_char r2, g2, b2, p2; short x2, y2; u_char u2, v2; u_short pad2;
    u_char r3, g3, b3, p3; short x3, y3; u_char u3, v3; u_short pad3;
} POLY_GT4;
typedef struct LINE_F2 { u_long tag; u_char r0, g0, b0, code; short x0, y0, x1, y1; } LINE_F2;
typedef struct LINE_G2 { u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char r1, g1, b1, p1; short x1, y1; } LINE_G2;
typedef struct LINE_F3 { u_long tag; u_char r0, g0, b0, code; short x0, y0, x1, y1, x2, y2; u_long pad; } LINE_F3;
typedef struct LINE_G3 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char r1, g1, b1, p1; short x1, y1;
    u_char r2, g2, b2, p2; short x2, y2; u_long pad;
} LINE_G3;
typedef struct LINE_F4 { u_long tag; u_char r0, g0, b0, code; short x0, y0, x1, y1, x2, y2, x3, y3; u_long pad; } LINE_F4;
typedef struct LINE_G4 {
    u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char r1, g1, b1, p1; short x1, y1;
    u_char r2, g2, b2, p2; short x2, y2; u_char r3, g3, b3, p3; short x3, y3; u_long pad;
} LINE_G4;
typedef struct SPRT { u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char u0, v0; u_short clut; short w, h; } SPRT;
typedef struct SPRT_16 { u_long tag; u_char r0, g0, b0, code; short x0, y0; u_char u0, v0; u_short clut; } SPRT_16;
typedef SPRT_16 SPRT_8;
typedef struct TILE { u_long tag; u_char r0, g0, b0, code; short x0, y0, w, h; } TILE;
typedef struct TILE_16 { u_long tag; u_char r0, g0, b0, code; short x0, y0; } TILE_16;
typedef TILE_16 TILE_8;
typedef TILE_16 TILE_1;
typedef struct DR_MODE { u_long tag; u_long code[2]; } DR_MODE;
typedef struct DR_TWIN { u_long tag; u_long code[2]; } DR_TWIN;
typedef struct DR_AREA { u_long tag; u_long code[2]; } DR_AREA;
typedef struct DR_OFFSET { u_long tag; u_long code[2]; } DR_OFFSET;
typedef struct DR_PRIO { u_long tag; u_long code[2]; } DR_PRIO;
typedef struct DR_TPAGE { u_long tag; u_long code[1]; } DR_TPAGE;
typedef struct DR_MOVE { u_long tag; u_long code[5]; } DR_MOVE;
typedef struct DR_LOAD { u_long tag; u_long code[3]; u_long p[13]; } DR_LOAD;

/* ---- libgs ---- */
typedef struct GsOT_TAG { unsigned p : 24; unsigned num : 8; } GsOT_TAG;
typedef struct GsOT { u_long length; GsOT_TAG *org; u_long offset; u_long point; GsOT_TAG *tag; } GsOT;
typedef struct GsCOORD2PARAM { VECTOR scale; SVECTOR rotate; VECTOR trans; } GsCOORD2PARAM;
typedef struct GsCOORDINATE2 {
    u_long flg; MATRIX coord; MATRIX workm; GsCOORD2PARAM *param; struct GsCOORDINATE2 *super, *sub;
} GsCOORDINATE2;
typedef struct GsDOBJ2 { u_long attribute; GsCOORDINATE2 *coord2; u_long *tmd; u_long id; } GsDOBJ2;
typedef struct GsRVIEW2 { int32_t vpx, vpy, vpz, vrx, vry, vrz, rz; GsCOORDINATE2 *super; } GsRVIEW2;
typedef struct GsSPRITE {
    u_long attribute; short x, y; u_short w, h; u_short tpage; u_char u, v; short cx, cy;
    u_char r, g, b; short mx, my; short scalex, scaley; int32_t rotate;
} GsSPRITE;
typedef struct GsBOXF { u_long attribute; short x, y; u_short w, h; u_char r, g, b; } GsBOXF;
typedef struct GsLINE { u_long attribute; short x0, y0, x1, y1; u_char r, g, b; } GsLINE;
typedef struct GsIMAGE {
    u_long pmode; short px, py; u_short pw, ph; u_long *pixel; short cx, cy; u_short cw, ch; u_long *clut;
} GsIMAGE;
typedef struct GsCELL { u_char u, v; u_short cba; u_short flag; u_short tpage; } GsCELL;
typedef struct GsMAP { u_char cellw, cellh; u_short ncellw, ncellh; GsCELL *base; u_short *index; } GsMAP;
typedef struct GsBG {
    u_long attribute; short x, y; short w, h; short scrollx, scrolly; u_char r, g, b; GsMAP *map;
    short mx, my; short scalex, scaley; int32_t rotate;
} GsBG;

/* ---- libcd ---- */
typedef struct CdlLOC { u_char minute, second, sector, track; } CdlLOC;
typedef struct CdlFILE { CdlLOC pos; u_long size; char name[16]; } CdlFILE;

/* ---- libpress ---- */
typedef struct DECDCTENV { u_char iq_y[64]; u_char iq_c[64]; short dct[64]; } DECDCTENV;

/* ---- misc ---- */
typedef struct VERTC { short vx, vy, vz; u_char tu, tv; CVECTOR col; } VERTC;

#ifdef __cplusplus
static_assert(sizeof(MATRIX) == 32, "MATRIX");
static_assert(sizeof(DRAWENV) == 92, "DRAWENV");
static_assert(sizeof(DISPENV) == 20, "DISPENV");
static_assert(sizeof(GsOT) == 20, "GsOT");
static_assert(sizeof(GsCOORDINATE2) == 80, "GsCOORDINATE2");
static_assert(sizeof(GsSPRITE) == 36, "GsSPRITE");
static_assert(sizeof(GsIMAGE) == 28, "GsIMAGE");
static_assert(sizeof(GsBG) == 36, "GsBG");
static_assert(sizeof(GsMAP) == 16, "GsMAP");
static_assert(sizeof(CdlFILE) == 24, "CdlFILE");
#endif
