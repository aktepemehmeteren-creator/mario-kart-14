/*
 * MARIO KART - PSP EDITION
 * PSP icin sade kontrol ve performans ayarlari
 * Kontroller:
 *   Analog / D-pad : direksiyon
 *   X              : gaz
 *   Kare           : fren
 *   L / R          : drift (birakinca mini turbo)
 *   Daire          : eldeki ozel gucu kullan
 *   Ucgen          : kamera acisi degistir
 *   START          : duraklat / devam et
 *   SELECT         : bitis ekraninda yeniden baslat
 *
 *   Ozel gucler: Mantar, Turbo Yildizi, Yesil/Kirmizi Kabuk, Muz, Yildirim, Kalkan
 *   Gelismis: PNG texture cache/atlas/UV, dinamik sky, bulut, AI savunma-saldiri, kupa, kariyer, ghost/save
 *
 *   Menu: YUKARI/ASAGI ile mod sec, X ile baslat
 *   Modlar: TEK KISILIK / TIME TRIAL + GHOST / CHAMPIONSHIP / CAREER
 *   Kayit: ms0:/PSP/SAVEDATA/MKPSP/  (en iyi tur, en iyi sure, galibiyet, ghost)

 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psppower.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

PSP_MODULE_INFO("MarioKartPSP", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

/* ---------- HOME tusu ---------- */
static int exit_callback(int a1, int a2, void *c) { (void)a1; (void)a2; (void)c; sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int th = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

/* ---------- Sabitler ---------- */
#define W 480
#define H 272
#define BUFW 512
#define PI 3.14159265f
#define DT (1.0f / 60.0f)
#define M 320            /* PSP icin yeterli pist ornek sayisi */
#define HW 11.0f         /* yol yarim genislik */
#define VMAX 52.0f
#define LAPS 3
#define NK 6             /* oyuncu + 5 bot */
#define MAXV 56000
#define NTRACKS 16           /* Mario Kart 64 tam pist seti */
#define ASSET_ROOT "ms0:/PSP/GAME/MarioKartPSP/"

static unsigned int __attribute__((aligned(16))) list[262144];

#define RGB(r,g,b) (0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define RGBA(r,g,b,a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float wrapA(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static unsigned scol(unsigned c, float f) {
    int r = (int)((c & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)(((c >> 16) & 255) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000u) | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}
static unsigned mixc(unsigned a, unsigned b, float t) {
    t = clampf(t, 0, 1);
    int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255;
    int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255;
    return RGB((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t), (int)(ab + (bb - ab) * t));
}
static unsigned rngState = 12345;
static float rnd(void) { rngState = rngState * 1664525u + 1013904223u; return ((rngState >> 8) & 0xFFFF) / 65536.0f; }

/* ---------- 2D cizim (HUD) ---------- */
typedef struct { unsigned int c; short x, y, z; } V2;
#define VF2 (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)

static short cs(float v) {
    if (v != v) v = 0;
    if (v > 30000) v = 30000;
    if (v < -30000) v = -30000;
    return (short)v;
}
static void v2set(V2 *v, float x, float y, unsigned c) { v->c = c; v->x = cs(x); v->y = cs(y); v->z = 0; }
static void rect(float x, float y, float w, float h, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(2 * sizeof(V2));
    v2set(&v[0], x, y, c); v2set(&v[1], x + w, y + h, c);
    sceGuDrawArray(GU_SPRITES, VF2, 2, 0, v);
}
static void vgrad(float x, float y, float w, float h, unsigned top, unsigned bot) {
    V2 *v = (V2 *)sceGuGetMemory(4 * sizeof(V2));
    v2set(&v[0], x, y, top); v2set(&v[1], x + w, y, top);
    v2set(&v[2], x, y + h, bot); v2set(&v[3], x + w, y + h, bot);
    sceGuDrawArray(GU_TRIANGLE_STRIP, VF2, 4, 0, v);
}
static void trap(float xl1, float xr1, float y1, float xl2, float xr2, float y2, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(4 * sizeof(V2));
    v2set(&v[0], xl1, y1, c); v2set(&v[1], xr1, y1, c);
    v2set(&v[2], xl2, y2, c); v2set(&v[3], xr2, y2, c);
    sceGuDrawArray(GU_TRIANGLE_STRIP, VF2, 4, 0, v);
}
static void tri2(float x1, float y1, float x2, float y2, float x3, float y3, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(3 * sizeof(V2));
    v2set(&v[0], x1, y1, c); v2set(&v[1], x2, y2, c); v2set(&v[2], x3, y3, c);
    sceGuDrawArray(GU_TRIANGLES, VF2, 3, 0, v);
}

/* ---------- 5x7 piksel font (A-Z, 0-9, bazi isaretler) ---------- */
/* her harf 7 satir, her satir 5 bit (en soldaki bit = 0x10) */
typedef struct { char c; unsigned char r[7]; } Glyph5;
static const Glyph5 font5[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
};
#define FONT5_COUNT ((int)(sizeof(font5) / sizeof(font5[0])))

static const unsigned char *glyphFor(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);   /* kucuk harf -> buyuk harf */
    for (int i = 0; i < FONT5_COUNT; i++) if (font5[i].c == ch) return font5[i].r;
    return NULL;
}

/* v == NULL ise sadece dikdortgen sayar, doluysa vertex yazar */
static void emitR(V2 *v, int *n, int x, int y, int w, int h, unsigned c) {
    if (v) { v2set(&v[2 * (*n)], x, y, c); v2set(&v[2 * (*n) + 1], x + w, y + h, c); }
    (*n)++;
}
static void glyphEmit(V2 *v, int *n, const unsigned char *r, int x, int y, int w, int h, unsigned c) {
    for (int row = 0; row < 7; row++) {
        int y0 = y + row * h / 7, y1 = y + (row + 1) * h / 7;
        if (y1 <= y0) y1 = y0 + 1;
        int col = 0;
        while (col < 5) {
            if (r[row] & (0x10 >> col)) {
                int c0 = col;
                while (col < 5 && (r[row] & (0x10 >> col))) col++;
                int x0 = x + c0 * w / 5, x1 = x + col * w / 5;
                if (x1 <= x0) x1 = x0 + 1;
                emitR(v, n, x0, y0, x1 - x0, y1 - y0, c);
            } else col++;
        }
    }
}

/* Title font is an offline-rasterized Titillium Web Bold atlas; the old pixel font remains as fallback. */
static int drawTitilliumText(int x, int y, int w, int h, int t, const char *s, unsigned c);

/* Yazi tek seferde cizilir (tek GU cagrisi): hizli ve PSP dostu */
static void text(int x, int y, int w, int h, int t, const char *s, unsigned c) {
    if (drawTitilliumText(x, y, w, h, t, s, c)) return;
    unsigned sh = RGBA(0, 0, 0, 200);
    int total = 0;
    V2 *v = NULL;
    for (int pass = 0; pass < 2; pass++) {
        int n = 0, cx = x;
        if (pass == 1) {
            if (total == 0) return;
            v = (V2 *)sceGuGetMemory(2 * total * sizeof(V2));
        }
        for (const char *p = s; *p; p++) {
            char ch = *p;
            if (ch == ' ') { cx += w; continue; }
            if (ch == '.') {
                emitR(v, &n, cx + 1, y + h - t + 1, t, t, sh); emitR(v, &n, cx, y + h - t, t, t, c);
                cx += t + 3; continue;
            }
            if (ch == ':') {
                emitR(v, &n, cx + 1, y + h / 3 + 1, t, t, sh); emitR(v, &n, cx, y + h / 3, t, t, c);
                emitR(v, &n, cx + 1, y + 2 * h / 3 - t + 1, t, t, sh); emitR(v, &n, cx, y + 2 * h / 3 - t, t, t, c);
                cx += t + 3; continue;
            }
            const unsigned char *g = glyphFor(ch);
            if (g) {
                glyphEmit(v, &n, g, cx + 1, y + 1, w, h, sh);
                glyphEmit(v, &n, g, cx, y, w, h, c);
            }
            cx += (ch == '/') ? w : (w + t + 3);
        }
        if (pass == 0) total = n;
        else sceGuDrawArray(GU_SPRITES, VF2, 2 * total, 0, v);
    }
}
static int textWidth(const char *s, int w, int t) {
    int x = 0;
    for (; *s; s++) {
        if (*s == ' ' || *s == '/') x += w;
        else if (*s == '.' || *s == ':') x += t + 3;
        else x += w + t + 3;
    }
    return x;
}

/* ---------- 3D mesh deposu ---------- */
typedef struct { float u, v; unsigned c; float x, y, z; } Vtx;
#define VF3 (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#include "char_models.h"
#include "kart_models.h"
typedef struct { float x, y, z; } P3;

static Vtx __attribute__((aligned(16))) mesh[MAXV];
static int nv = 0;
static P3 mk(float x, float y, float z) { P3 p; p.x = x; p.y = y; p.z = z; return p; }
static unsigned pvAlpha = 255;   /* ghost kart icin yari saydam */
static void pv(P3 p, unsigned c) {
    if (pvAlpha != 255) c = (c & 0x00FFFFFFu) | (pvAlpha << 24);
    if (nv < MAXV) { mesh[nv].u = 0; mesh[nv].v = 0; mesh[nv].c = c; mesh[nv].x = p.x; mesh[nv].y = p.y; mesh[nv].z = p.z; nv++; }
}
static void tri3(P3 a, P3 b, P3 c, unsigned col) {
    if (nv + 3 > MAXV) return;
    pv(a, col); pv(b, col); pv(c, col);
}
static void quad3(P3 a, P3 b, P3 c, P3 d, unsigned col) { tri3(a, b, c, col); tri3(a, c, d, col); }
/* dokulu dortgen: a=(u0,v0) b=(u1,v0) c=(u1,v1) d=(u0,v1) */
static void tquad(P3 a, P3 b, P3 c, P3 d, unsigned col, float u0, float v0, float u1, float v1) {
    int s0 = nv;
    quad3(a, b, c, d, col);
    if (nv - s0 == 6) {
        mesh[s0 + 0].u = u0; mesh[s0 + 0].v = v0;
        mesh[s0 + 1].u = u1; mesh[s0 + 1].v = v0;
        mesh[s0 + 2].u = u1; mesh[s0 + 2].v = v1;
        mesh[s0 + 3].u = u0; mesh[s0 + 3].v = v0;
        mesh[s0 + 4].u = u1; mesh[s0 + 4].v = v1;
        mesh[s0 + 5].u = u0; mesh[s0 + 5].v = v1;
    }
}

/* yone gore donmus kutu (f = ileri yonu xz) */
static void boxO(float cx, float cy, float cz, float fx, float fz, float hx, float hy, float hz, unsigned c) {
    float rx = -fz, rz = fx;
#define BP(sx, sy, sz) mk(cx + fx * hx * (sx) + rx * hz * (sz), cy + hy * (sy), cz + fz * hx * (sx) + rz * hz * (sz))
    quad3(BP(-1, 1, -1), BP(1, 1, -1), BP(1, 1, 1), BP(-1, 1, 1), scol(c, 1.00f));
    quad3(BP(1, -1, -1), BP(1, 1, -1), BP(1, 1, 1), BP(1, -1, 1), scol(c, 0.82f));
    quad3(BP(-1, -1, -1), BP(-1, 1, -1), BP(-1, 1, 1), BP(-1, -1, 1), scol(c, 0.60f));
    quad3(BP(-1, -1, 1), BP(1, -1, 1), BP(1, 1, 1), BP(-1, 1, 1), scol(c, 0.90f));
    quad3(BP(-1, -1, -1), BP(1, -1, -1), BP(1, 1, -1), BP(-1, 1, -1), scol(c, 0.70f));
#undef BP
}
static void box3(float cx, float cy, float cz, float hx, float hy, float hz, unsigned c) {
    boxO(cx, cy, cz, 1, 0, hx, hy, hz, c);
}
static void cone3(float cx, float cy, float cz, float r, float h, int sides, float rot, unsigned c) {
    P3 apex = mk(cx, cy + h, cz);
    for (int i = 0; i < sides; i++) {
        float a0 = rot + i * 2 * PI / sides, a1 = rot + (i + 1) * 2 * PI / sides;
        float am = (a0 + a1) * 0.5f;
        float sh = 0.62f + 0.38f * (0.5f + 0.5f * cosf(am - 0.8f));
        tri3(mk(cx + cosf(a0) * r, cy, cz + sinf(a0) * r), mk(cx + cosf(a1) * r, cy, cz + sinf(a1) * r), apex, scol(c, sh));
    }
}

/* ---------- Karakterler, pistler, temalar ---------- */
static const unsigned kBody[NK] = {
    RGB(225, 35, 35),   /* Mario  - kirmizi */
    RGB(35, 165, 70),   /* Luigi  - yesil */
    RGB(35, 200, 90),   /* Yoshi  - yesil */
    RGB(245, 205, 25),  /* Wario  - sari */
    RGB(80, 110, 230),  /* Toad   - mavi */
    RGB(245, 100, 170)  /* Peach  - pembe */
};
static const unsigned kTrim[NK] = { RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(40, 40, 40), RGB(255, 255, 255), RGB(255, 255, 255) };
static const char *charNames[NK] = { "MARIO", "LUIGI", "YOSHI", "WARIO", "TOAD", "PEACH" };
static const char *charClass[NK] = { "MEDIUM", "MEDIUM", "LIGHT", "HEAVY", "LIGHT", "MEDIUM" };
static const char *charTagline[NK] = {
    "ALL-ROUNDER", "SPEEDSTER", "TRICKY", "POWERHOUSE", "AGILE", "HANDLING"
};
/* MK tarzi statlar: hiz / ivme / direksiyon (agir = hizli ama yavas doner) */
static const float cSpd[NK] = { 1.00f, 1.06f, 0.94f, 1.12f, 0.90f, 0.98f };
static const float cAcc[NK] = { 1.00f, 0.94f, 1.14f, 0.82f, 1.20f, 1.06f };
static const float cTrn[NK] = { 1.00f, 0.96f, 1.10f, 0.84f, 1.18f, 1.08f };
/* 3D model: 0=Mario mesh, 1=Luigi mesh, 2=Yoshi mesh */
/* Source assets include true Mario, Luigi and Yoshi meshes. Wario/Toad/Peach
   use dedicated recolored material variants on the closest available base mesh. */
static const int charMeshId[NK] = { 0, 1, 2, 0, 1, 0 };
/* olcek / oturma yuksekligi: Wario buyuk, Toad kucuk, Peach zarif */
static const float charScale[NK]  = { 0.50f, 0.54f, 0.48f, 0.64f, 0.35f, 0.51f };
static const float charSeatY[NK]  = { 0.72f, 0.74f, 0.70f, 0.78f, 0.60f, 0.74f };
static const float charLeanX[NK]  = { 0.18f, 0.16f, 0.20f, 0.12f, 0.22f, 0.17f };

static int trackSel = 0, charSel = 0;
static int kChar[NK];            /* kart slotu -> karakter (0 = oyuncu) */
static int ghostBestChar = 0;    /* ghost kaydini yapan karakter */

typedef struct { const char *name; unsigned g1, g2, sky, haze, mtn, cap, leaf; float skip; } Theme;
/* Mario Kart 64 — 16 pist temasi (renk / gokyuzu) */
static const Theme themes[NTRACKS] = {
    /* Mushroom Cup */
    { "LUIGI RACEWAY",     RGB(34, 150, 48),   RGB(28, 138, 42),   RGB(70, 140, 230), RGB(180, 220, 255), RGB(90, 110, 160),  RGB(240, 245, 255), RGB(20, 120, 45),  0.25f },
    { "MOO MOO FARM",      RGB(50, 170, 55),   RGB(40, 150, 45),   RGB(90, 160, 235), RGB(190, 230, 255), RGB(100, 130, 90),  RGB(245, 250, 255), RGB(30, 140, 40),  0.30f },
    { "KOOPA BEACH",       RGB(210, 190, 120), RGB(200, 175, 100), RGB(40, 160, 230), RGB(160, 220, 250), RGB(180, 160, 90),  RGB(250, 240, 200), RGB(40, 150, 80),  0.55f },
    { "KALIMARI DESERT",   RGB(214, 184, 112), RGB(204, 172, 100), RGB(70, 140, 225), RGB(240, 214, 168), RGB(176, 120, 78),  RGB(235, 200, 150), RGB(110, 140, 60), 0.85f },
    /* Flower Cup */
    { "TOADS TURNPIKE",    RGB(70, 70, 78),    RGB(55, 55, 62),    RGB(60, 90, 150),  RGB(140, 160, 190), RGB(80, 80, 100),   RGB(200, 210, 230), RGB(50, 80, 50),   0.40f },
    { "FRAPPE SNOWLAND",   RGB(236, 242, 250), RGB(224, 232, 246), RGB(140, 170, 220), RGB(210, 225, 245), RGB(150, 165, 190), RGB(250, 252, 255), RGB(40, 100, 80),  0.35f },
    { "CHOCO MOUNTAIN",    RGB(120, 70, 40),   RGB(100, 55, 30),   RGB(90, 130, 200), RGB(200, 180, 140), RGB(90, 60, 40),    RGB(220, 190, 150), RGB(50, 90, 40),   0.50f },
    { "MARIO RACEWAY",     RGB(34, 150, 48),   RGB(28, 138, 42),   RGB(30, 100, 225), RGB(175, 218, 255), RGB(98, 108, 170),  RGB(240, 245, 255), RGB(20, 120, 45),  0.28f },
    /* Star Cup */
    { "WARIO STADIUM",     RGB(50, 55, 50),    RGB(40, 45, 40),    RGB(40, 50, 70),   RGB(90, 100, 120),  RGB(60, 65, 55),    RGB(160, 160, 150), RGB(40, 70, 40),   0.45f },
    { "SHERBET LAND",      RGB(200, 230, 245), RGB(180, 215, 235), RGB(100, 140, 205), RGB(206, 224, 244), RGB(130, 150, 190), RGB(250, 252, 255), RGB(40, 100, 80),  0.40f },
    { "ROYAL RACEWAY",     RGB(40, 160, 70),   RGB(30, 140, 55),   RGB(80, 150, 235), RGB(190, 220, 255), RGB(100, 120, 90),  RGB(245, 250, 255), RGB(25, 130, 50),  0.30f },
    { "BOWSERS CASTLE",    RGB(60, 40, 40),    RGB(45, 30, 30),    RGB(40, 20, 30),   RGB(90, 40, 40),    RGB(50, 30, 35),    RGB(140, 80, 70),   RGB(80, 30, 30),   0.55f },
    /* Special Cup */
    { "DK JUNGLE",         RGB(20, 100, 40),   RGB(15, 80, 30),    RGB(30, 90, 160),  RGB(100, 160, 120), RGB(40, 70, 40),    RGB(180, 200, 160), RGB(15, 90, 35),   0.45f },
    { "YOSHI VALLEY",      RGB(50, 160, 60),   RGB(40, 140, 50),   RGB(70, 140, 220), RGB(170, 210, 250), RGB(80, 110, 70),   RGB(235, 245, 255), RGB(30, 130, 45),  0.35f },
    { "BANSHEE BOARDWALK", RGB(40, 40, 55),    RGB(30, 30, 45),    RGB(20, 20, 45),   RGB(50, 50, 80),    RGB(35, 35, 50),    RGB(100, 100, 130), RGB(30, 40, 40),   0.50f },
    { "RAINBOW ROAD",      RGB(30, 20, 60),    RGB(20, 15, 50),    RGB(10, 5, 40),    RGB(60, 30, 100),   RGB(40, 25, 70),    RGB(140, 100, 200), RGB(40, 20, 80),   0.20f },
};
static unsigned HAZE = RGB(175, 218, 255);
static unsigned SKYTOP = RGB(30, 100, 225);

/* ---------- Pist ---------- */
static float tcx[M], tcz[M], tfx[M], tfz[M], tsd[M], tth[M];
static float TL = 0;
static float mapMinX, mapMaxX, mapMinZ, mapMaxZ;

static float ctrl[32][2];
static int nCtrl = 12;
static int mirrorMode = 0;
static float trackSC = 1.3f;

/* Mario Kart 64 pist iskeletleri: her pist icin kontrol noktalari (x,z) */
typedef struct { int n; float sc; float pts[16][2]; } TrackLayout;
static const TrackLayout mk64Tracks[NTRACKS] = {
    /* 0 Luigi Raceway — genis oval + hafif sikan */
    { 12, 1.35f, {{0,0},{180,20},{320,80},{380,200},{320,320},{160,360},{40,300},{-80,340},{-200,280},{-280,160},{-220,40},{-80,10}} },
    /* 1 Moo Moo Farm — dalgali ciftler */
    { 14, 1.20f, {{0,0},{140,40},{260,20},{340,100},{380,220},{300,320},{180,360},{60,340},{-60,360},{-160,300},{-240,180},{-200,60},{-100,20},{40,10}} },
    /* 2 Koopa Troopa Beach — uzun sahil halka */
    { 12, 1.40f, {{0,0},{200,30},{360,100},{420,220},{340,340},{160,380},{-20,360},{-180,300},{-280,180},{-260,60},{-140,10},{40,5}} },
    /* 3 Kalimari Desert — uzun oval (tren yolu hissi) */
    { 12, 1.50f, {{0,0},{220,10},{400,60},{460,180},{420,320},{220,380},{20,360},{-180,320},{-320,200},{-300,60},{-160,10},{20,5}} },
    /* 4 Toad's Turnpike — sik sehir virajlari */
    { 14, 1.10f, {{0,0},{120,30},{200,10},{280,80},{260,180},{320,240},{240,320},{100,340},{-40,300},{-120,220},{-80,120},{-160,60},{-80,10},{40,5}} },
    /* 5 Frappe Snowland — karli dolambac */
    { 14, 1.25f, {{0,0},{150,50},{280,30},{360,120},{340,240},{240,320},{100,360},{-40,340},{-160,280},{-220,160},{-180,40},{-60,10},{40,15},{80,5}} },
    /* 6 Choco Mountain — sac tokasi / serit */
    { 14, 1.15f, {{0,0},{100,60},{180,40},{280,100},{320,200},{260,300},{140,360},{20,340},{-100,280},{-160,160},{-120,40},{20,10},{80,30},{40,5}} },
    /* 7 Mario Raceway — klasik devre */
    { 12, 1.30f, {{0,0},{160,0},{300,60},{360,180},{300,300},{160,330},{60,260},{-40,330},{-180,300},{-260,180},{-200,60},{-100,40}} },
    /* 8 Wario Stadium — stadium halka + ic bukuntu */
    { 14, 1.20f, {{0,0},{160,40},{300,20},{380,120},{360,240},{280,340},{120,380},{-40,360},{-180,300},{-260,180},{-240,60},{-120,10},{20,20},{60,5}} },
    /* 9 Sherbet Land — buzlu 8/halka */
    { 12, 1.25f, {{0,0},{140,40},{280,20},{360,120},{340,260},{200,340},{40,360},{-120,300},{-220,180},{-200,50},{-80,10},{40,5}} },
    /* 10 Royal Raceway — uzun zarif */
    { 14, 1.45f, {{0,0},{180,20},{340,50},{420,150},{400,280},{280,360},{100,390},{-80,360},{-220,280},{-300,160},{-260,40},{-120,10},{20,5},{60,0}} },
    /* 11 Bowser's Castle — acili kalesi */
    { 14, 1.15f, {{0,0},{140,20},{240,0},{320,80},{300,180},{360,260},{260,340},{100,360},{-40,320},{-140,240},{-180,120},{-120,20},{20,10},{60,5}} },
    /* 12 DK's Jungle Parkway — orman dolambac */
    { 14, 1.30f, {{0,0},{120,50},{240,20},{340,80},{380,200},{300,320},{160,370},{20,350},{-120,300},{-220,200},{-240,70},{-140,10},{-20,20},{40,5}} },
    /* 13 Yoshi Valley — labirentimsi */
    { 16, 1.20f, {{0,0},{100,40},{200,10},{280,70},{260,160},{340,220},{280,300},{160,350},{40,340},{-80,300},{-160,220},{-120,120},{-180,40},{-80,10},{20,20},{50,5}} },
    /* 14 Banshee Boardwalk — iskele uzun halka */
    { 12, 1.35f, {{0,0},{200,20},{360,80},{400,200},{340,320},{160,370},{-20,350},{-180,280},{-280,160},{-260,40},{-120,10},{40,5}} },
    /* 15 Rainbow Road — uzun uzay seriti */
    { 16, 1.60f, {{0,0},{180,30},{340,10},{480,80},{540,200},{480,340},{340,420},{160,450},{-20,420},{-180,340},{-300,220},{-320,80},{-200,10},{-40,20},{60,10},{100,0}} },
};

static void catmull(float p0, float p1, float p2, float p3, float t, float *o) {
    float t2 = t * t, t3 = t2 * t;
    *o = 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

#define DENSE_MAX 512
static void buildTrackPath(void) {
    static float dx[DENSE_MAX], dz[DENSE_MAX], cum[DENSE_MAX + 1];
    int tid = trackSel;
    if (tid < 0 || tid >= NTRACKS) tid = 0;
    const TrackLayout *tl = &mk64Tracks[tid];
    nCtrl = tl->n;
    trackSC = tl->sc;
    for (int i = 0; i < nCtrl; i++) {
        ctrl[i][0] = tl->pts[i][0];
        ctrl[i][1] = tl->pts[i][1];
        if (mirrorMode) ctrl[i][0] = -ctrl[i][0];
    }
    const int per = DENSE_MAX / nCtrl;
    const int KD = nCtrl * per;
    const float SC = trackSC;
    int k = 0;
    for (int i = 0; i < nCtrl; i++) {
        const float *p0 = ctrl[(i + nCtrl - 1) % nCtrl], *p1 = ctrl[i], *p2 = ctrl[(i + 1) % nCtrl], *p3 = ctrl[(i + 2) % nCtrl];
        for (int j = 0; j < per; j++) {
            float t = (float)j / per, x, z;
            catmull(p0[0], p1[0], p2[0], p3[0], t, &x);
            catmull(p0[1], p1[1], p2[1], p3[1], t, &z);
            dx[k] = x * SC; dz[k] = z * SC; k++;
        }
    }
    cum[0] = 0;
    for (int i = 0; i < KD; i++) {
        int j = (i + 1) % KD;
        cum[i + 1] = cum[i] + sqrtf((dx[j] - dx[i]) * (dx[j] - dx[i]) + (dz[j] - dz[i]) * (dz[j] - dz[i]));
    }
    TL = cum[KD];
    int ptr = 0;
    for (int m = 0; m < M; m++) {
        float target = TL * m / M;
        while (ptr < KD - 1 && cum[ptr + 1] < target) ptr++;
        int j = (ptr + 1) % KD;
        float seglen = cum[ptr + 1] - cum[ptr];
        float u = seglen > 0.0001f ? (target - cum[ptr]) / seglen : 0;
        tcx[m] = dx[ptr] + (dx[j] - dx[ptr]) * u;
        tcz[m] = dz[ptr] + (dz[j] - dz[ptr]) * u;
        tsd[m] = target;
    }
    mapMinX = mapMinZ = 1e9f; mapMaxX = mapMaxZ = -1e9f;
    for (int m = 0; m < M; m++) {
        int a = (m + M - 1) % M, b = (m + 1) % M;
        float fx = tcx[b] - tcx[a], fz = tcz[b] - tcz[a];
        float l = sqrtf(fx * fx + fz * fz);
        if (l < 0.0001f) l = 1;
        tfx[m] = fx / l; tfz[m] = fz / l; tth[m] = atan2f(tfz[m], tfx[m]);
        if (tcx[m] < mapMinX) mapMinX = tcx[m];
        if (tcx[m] > mapMaxX) mapMaxX = tcx[m];
        if (tcz[m] < mapMinZ) mapMinZ = tcz[m];
        if (tcz[m] > mapMaxZ) mapMaxZ = tcz[m];
    }
}

static int nFlat = 0, nScene = 0;
static int toadCapStart, toadCapCount, toadSpotStart, toadSpotCount, peachCrownStart, peachCrownCount;
static int warioMustacheStart, warioMustacheCount, warioNoseStart, warioNoseCount;
static int peachHairStart, peachHairCount, warioBadgeStart, warioBadgeCount;
/* Accessory builder appears before the mesh helper definition. */
static void sphereMesh(float r, float sy, int seg, int rings, unsigned top, unsigned bot, unsigned band);
static void buildCharacterAccessoryMeshes(void) {
    /* Low-poly silhouettes are built once, never allocated in the frame loop. */
    toadCapStart = nv;
    sphereMesh(0.42f, 0.43f, 10, 5, RGB(220, 35, 45), RGB(245, 245, 235), RGB(245, 245, 235));
    toadCapCount = nv - toadCapStart;
    /* White circles on red cap (the old version accidentally used red spots). */
    toadSpotStart = nv;
    sphereMesh(0.082f, 0.75f, 6, 3, RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255));
    toadSpotCount = nv - toadSpotStart;

    /* Wario's broad dark moustache and large nose separate his silhouette
       from Mario even though both use the available Mario base mesh. */
    warioMustacheStart = nv;
    sphereMesh(0.155f, 0.55f, 8, 4, RGB(32, 25, 28), RGB(18, 16, 20), RGB(26, 22, 25));
    warioMustacheCount = nv - warioMustacheStart;
    warioNoseStart = nv;
    sphereMesh(0.12f, 0.90f, 8, 5, RGB(250, 185, 142), RGB(218, 133, 102), RGB(239, 164, 125));
    warioNoseCount = nv - warioNoseStart;
    warioBadgeStart = nv;
    sphereMesh(0.105f, 0.28f, 8, 4, RGB(255, 225, 70), RGB(245, 190, 30), RGB(255, 235, 90));
    warioBadgeCount = nv - warioBadgeStart;

    /* Peach gets blonde side curls beneath the crown. */
    peachHairStart = nv;
    sphereMesh(0.19f, 0.90f, 8, 5, RGB(255, 220, 100), RGB(220, 150, 40), RGB(245, 190, 65));
    peachHairCount = nv - peachHairStart;

    peachCrownStart = nv;
    box3(0, 0, 0, 0.26f, 0.055f, 0.30f, RGB(255, 205, 40));
    cone3(-0.16f, 0.03f, -0.18f, 0.09f, 0.22f, 5, 0.0f, RGB(255, 210, 40));
    cone3(-0.16f, 0.03f,  0.00f, 0.09f, 0.26f, 5, 0.0f, RGB(255, 210, 40));
    cone3(-0.16f, 0.03f,  0.18f, 0.09f, 0.22f, 5, 0.0f, RGB(255, 210, 40));
    box3(-0.17f, 0.13f, 0, 0.025f, 0.025f, 0.045f, RGB(255, 80, 145));
    peachCrownCount = nv - peachCrownStart;
}
static int kartStart[NK], kartCount = 0;
static int boxStart, boxCount, coneStart, coneCount, flameStart, flameCount, shadowStart, shadowCount;
static int sparkStart, sparkCount;
static int groundEnd, roadStart, roadEnd;
static int shellGStart, shellGCount, shellRStart, shellRCount, banStart, banCount, ghostStart, ghostCount3;
static unsigned char padMark[M];

static void buildKartMesh(unsigned body, unsigned trim) {
    unsigned dark = RGB(30, 30, 34), grey = RGB(150, 150, 158);
    box3(0, 0.55f, 0, 1.55f, 0.28f, 0.75f, body);
    box3(1.9f, 0.45f, 0, 0.5f, 0.2f, 0.5f, body);
    box3(2.35f, 0.3f, 0, 0.16f, 0.07f, 1.05f, trim);
    box3(-1.75f, 1.35f, 0, 0.25f, 0.07f, 1.0f, trim);
    box3(-1.6f, 1.0f, 0.55f, 0.06f, 0.32f, 0.06f, dark);
    box3(-1.6f, 1.0f, -0.55f, 0.06f, 0.32f, 0.06f, dark);
    box3(-1.3f, 0.95f, 0, 0.3f, 0.25f, 0.5f, grey);
    for (int i = 0; i < 2; i++) {
        float s = i ? 1.0f : -1.0f;
        box3(1.25f, 0.42f, s * 1.0f, 0.42f, 0.42f, 0.22f, dark);
        box3(1.25f, 0.42f, s * 1.23f, 0.2f, 0.2f, 0.02f, grey);
        box3(-1.2f, 0.5f, s * 1.05f, 0.5f, 0.5f, 0.28f, dark);
        box3(-1.2f, 0.5f, s * 1.34f, 0.24f, 0.24f, 0.02f, grey);
    }
    /* Gercek 3D karakter modeli ayri ciziliyor. */
}

static void buildSparkMesh(void) {
    sparkStart = nv;
    /* daha dolgun yildiz / kivilcim: ust+alt + capraz kollar */
    P3 a = mk(0, 1.0f, 0);
    P3 b = mk(-0.35f, -0.15f, 0);
    P3 c = mk(0.35f, -0.15f, 0);
    P3 d = mk(0, 0, 0.48f);
    P3 e = mk(0, 0, -0.48f);
    P3 f = mk(0, -0.55f, 0);
    unsigned q = RGB(255, 230, 80);
    unsigned q2 = RGB(255, 160, 40);
    tri3(a, b, d, q); tri3(a, d, c, q);
    tri3(a, c, e, q); tri3(a, e, b, q);
    tri3(f, b, d, q2); tri3(f, d, c, q2);
    tri3(f, c, e, q2); tri3(f, e, b, q2);
    sparkCount = nv - sparkStart;
}

/* kabuk: ust yari renkli, alt yari krem, ortada beyaz bant */
static void sphereMesh(float r, float sy, int seg, int rings, unsigned top, unsigned bot, unsigned band) {
    for (int j = 0; j < rings; j++) {
        float p0 = -PI * 0.5f + PI * j / rings, p1 = -PI * 0.5f + PI * (j + 1) / rings;
        float pm = (p0 + p1) * 0.5f;
        unsigned base = pm > 0.25f ? top : (pm < -0.25f ? bot : band);
        for (int i = 0; i < seg; i++) {
            float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg, am = (a0 + a1) * 0.5f;
            float sh = (0.70f + 0.30f * (0.5f + 0.5f * cosf(am - 0.8f))) * (0.85f + 0.25f * sinf(pm));
            P3 q00 = mk(cosf(a0) * cosf(p0) * r, sinf(p0) * r * sy, sinf(a0) * cosf(p0) * r);
            P3 q10 = mk(cosf(a1) * cosf(p0) * r, sinf(p0) * r * sy, sinf(a1) * cosf(p0) * r);
            P3 q11 = mk(cosf(a1) * cosf(p1) * r, sinf(p1) * r * sy, sinf(a1) * cosf(p1) * r);
            P3 q01 = mk(cosf(a0) * cosf(p1) * r, sinf(p1) * r * sy, sinf(a0) * cosf(p1) * r);
            quad3(q00, q10, q11, q01, scol(base, sh));
        }
    }
}

/* muz: yay seklinde kivrilan, uclari kahverengi koni gibi daralan tup */
static void bananaMesh(void) {
    enum { N = 8, S = 6 };
    const float R = 1.5f;
    P3 ring[N + 1][S];
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N, a = -0.95f + 1.9f * t;
        float cx = sinf(a) * R, cy = (1.0f - cosf(a)) * R + 0.15f;
        float rad = 0.08f + 0.26f * sinf(PI * t);
        float nx = -sinf(a), ny = cosf(a);
        for (int q = 0; q < S; q++) {
            float ph = q * 2 * PI / S;
            ring[i][q] = mk(cx + nx * rad * cosf(ph), cy + ny * rad * cosf(ph), rad * sinf(ph));
        }
    }
    for (int i = 0; i < N; i++) for (int q = 0; q < S; q++) {
        int q2 = (q + 1) % S;
        float ph = (q + 0.5f) * 2 * PI / S;
        unsigned c = (i == 0 || i == N - 1) ? RGB(110, 76, 30) : scol(RGB(255, 214, 40), 0.72f + 0.28f * (0.5f + 0.5f * cosf(ph - 0.6f)));
        quad3(ring[i][q], ring[i + 1][q], ring[i + 1][q2], ring[i][q2], c);
    }
}

static void buildMesh(void) {
    nv = 0;
    const Theme *th = &themes[trackSel];
    HAZE = th->haze; SKYTOP = th->sky;
    unsigned G1 = th->g1, G2 = th->g2;
    /* --- duz katmanlar (derinlik testsiz, sirayla) --- */
    /* zemin (dokulu) */
    const int GN = 24; const float GS = 125.0f;
    for (int i = 0; i < GN; i++) for (int j = 0; j < GN; j++) {
        float x0 = -1500 + i * GS, z0 = -1500 + j * GS;
        tquad(mk(x0, 0, z0), mk(x0 + GS, 0, z0), mk(x0 + GS, 0, z0 + GS), mk(x0, 0, z0 + GS), ((i + j) & 1) ? G1 : G2,
              x0 / 24.0f, z0 / 24.0f, (x0 + GS) / 24.0f, (z0 + GS) / 24.0f);
    }
    groundEnd = nv;
    /* cim seritleri (yolun yani) */
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 4) & 1) ? scol(G1, 1.18f) : scol(G1, 1.08f);
        float a = HW + 1.8f, b = HW + 14.0f;
        quad3(mk(tcx[k] + rx0 * a, 0.0f, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0.0f, tcz[k] + rz0 * b),
              mk(tcx[k2] + rx1 * b, 0.0f, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0.0f, tcz[k2] + rz1 * a), c);
        quad3(mk(tcx[k] - rx0 * a, 0.0f, tcz[k] - rz0 * a), mk(tcx[k] - rx0 * b, 0.0f, tcz[k] - rz0 * b),
              mk(tcx[k2] - rx1 * b, 0.0f, tcz[k2] - rz1 * b), mk(tcx[k2] - rx1 * a, 0.0f, tcz[k2] - rz1 * a), c);
    }
    /* rumble */
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 2) & 1) ? RGB(225, 40, 40) : RGB(245, 245, 245);
        float a = HW, b = HW + 1.8f;
        quad3(mk(tcx[k] + rx0 * a, 0, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0, tcz[k] + rz0 * b),
              mk(tcx[k2] + rx1 * b, 0, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0, tcz[k2] + rz1 * a), c);
        quad3(mk(tcx[k] - rx0 * a, 0, tcz[k] - rz0 * a), mk(tcx[k] - rx0 * b, 0, tcz[k] - rz0 * b),
              mk(tcx[k2] - rx1 * b, 0, tcz[k2] - rz1 * b), mk(tcx[k2] - rx1 * a, 0, tcz[k2] - rz1 * a), c);
    }
    /* yol (dokulu asfalt) */
    roadStart = nv;
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 3) & 1) ? RGB(96, 96, 102) : RGB(106, 106, 112);
        float v0 = tsd[k] / 8.0f, v1 = (k2 == 0 ? TL : tsd[k2]) / 8.0f;
        tquad(mk(tcx[k] - rx0 * HW, 0, tcz[k] - rz0 * HW), mk(tcx[k] + rx0 * HW, 0, tcz[k] + rz0 * HW),
              mk(tcx[k2] + rx1 * HW, 0, tcz[k2] + rz1 * HW), mk(tcx[k2] - rx1 * HW, 0, tcz[k2] - rz1 * HW), c,
              0.0f, v0, 2.0f, v1);
    }
    roadEnd = nv;
    /* turbo seritleri */
    memset(padMark, 0, sizeof(padMark));
    for (int q = 1; q < 10; q++) {
        int b = M * q / 10 + 7;
        for (int j = 0; j < 7; j++) {
            int k = (b + j) % M, k2 = (k + 1) % M;
            padMark[k] = 1;
            float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
            float w = HW * 0.4f;
            unsigned c = (j & 1) ? RGB(255, 215, 30) : RGB(255, 120, 20);
            quad3(mk(tcx[k] - rx0 * w, 0, tcz[k] - rz0 * w), mk(tcx[k] + rx0 * w, 0, tcz[k] + rz0 * w),
                  mk(tcx[k2] + rx1 * w, 0, tcz[k2] + rz1 * w), mk(tcx[k2] - rx1 * w, 0, tcz[k2] - rz1 * w), c);
        }
    }
    /* seritler */
    for (int k = 0; k < M; k++) {
        if ((k % 4) > 1) continue;
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        for (int ln = -1; ln <= 1; ln += 2) {
            float o = ln * HW / 3.0f, w = 0.22f;
            quad3(mk(tcx[k] + rx0 * (o - w), 0, tcz[k] + rz0 * (o - w)), mk(tcx[k] + rx0 * (o + w), 0, tcz[k] + rz0 * (o + w)),
                  mk(tcx[k2] + rx1 * (o + w), 0, tcz[k2] + rz1 * (o + w)), mk(tcx[k2] + rx1 * (o - w), 0, tcz[k2] + rz1 * (o - w)), RGB(240, 240, 240));
        }
    }
    /* baslangic cizgisi */
    for (int row = 0; row < 2; row++) {
        int k = row, k2 = row + 1;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        for (int i = 0; i < 8; i++) {
            float a = -HW + i * (2 * HW / 8.0f), b = a + 2 * HW / 8.0f;
            quad3(mk(tcx[k] + rx0 * a, 0, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0, tcz[k] + rz0 * b),
                  mk(tcx[k2] + rx1 * b, 0, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0, tcz[k2] + rz1 * a),
                  ((i + row) & 1) ? RGB(250, 250, 250) : RGB(20, 20, 20));
        }
    }
    nFlat = nv;

    /* --- sahne objeleri --- */
    /* kapi */
    {
        float fx = tfx[0], fz = tfz[0], rx = -fz, rz = fx;
        float px0 = tcx[0], pz0 = tcz[0];
        float off = HW + 2.0f;
        boxO(px0 + rx * off, 4.0f, pz0 + rz * off, fx, fz, 0.6f, 4.0f, 0.6f, RGB(230, 230, 235));
        boxO(px0 - rx * off, 4.0f, pz0 - rz * off, fx, fz, 0.6f, 4.0f, 0.6f, RGB(230, 230, 235));
        boxO(px0, 8.4f, pz0, fx, fz, 0.9f, 0.9f, off + 0.6f, RGB(220, 40, 40));
        boxO(px0, 8.4f, pz0, fx, fz, 0.95f, 0.35f, off * 0.7f, RGB(250, 250, 250));
    }
    /* agaclar */
    for (int k = 0; k < M; k += 2) {
        for (int side = -1; side <= 1; side += 2) {
            if (rnd() < th->skip) continue;
            float lat = side * (HW + 15.0f + rnd() * 18.0f);
            float x = tcx[k] - tfz[k] * lat, z = tcz[k] + tfx[k] * lat;
            float s = 0.8f + rnd() * 0.9f;
            int pine = rnd() < 0.7f;
            box3(x, 1.2f * s, z, 0.35f * s, 1.2f * s, 0.35f * s, RGB(110, 72, 40));
            unsigned leaf = pine ? scol(th->leaf, 0.9f + rnd() * 0.35f) : scol(th->leaf, 1.25f + rnd() * 0.3f);
            cone3(x, 2.0f * s, z, 2.2f * s, 4.2f * s, 6, rnd() * 3, leaf);
            cone3(x, 3.8f * s, z, 1.6f * s, 3.6f * s, 6, rnd() * 3, scol(leaf, 1.1f));
        }
    }
    /* daglar */
    {
        float mx = (mapMinX + mapMaxX) * 0.5f, mz = (mapMinZ + mapMaxZ) * 0.5f;
        float ext = 0;
        for (int m = 0; m < M; m++) {
            float d = sqrtf((tcx[m] - mx) * (tcx[m] - mx) + (tcz[m] - mz) * (tcz[m] - mz));
            if (d > ext) ext = d;
        }
        for (int i = 0; i < 22; i++) {
            float a = i * 2 * PI / 22 + rnd() * 0.1f;
            float rad = ext + 470.0f + rnd() * 120.0f;
            float h = 160.0f + rnd() * 160.0f, r = h * (0.9f + rnd() * 0.5f);
            float x = mx + cosf(a) * rad, z = mz + sinf(a) * rad;
            cone3(x, 0, z, r, h, 5, rnd() * 3, th->mtn);
            cone3(x, h * 0.72f, z, r * 0.29f, h * 0.28f, 5, 0, th->cap);
        }
    }
    nScene = nv;

    /* --- arac meshleri --- */
    kartCount = 0;
    for (int i = 0; i < NK; i++) {
        kartStart[i] = nv;
        buildKartMesh(kBody[kChar[i]], kTrim[kChar[i]]);
        kartCount = nv - kartStart[i];
    }
    /* ghost araci: yari saydam */
    pvAlpha = 140;
    ghostStart = nv;
    buildKartMesh(kBody[ghostBestChar], kTrim[ghostBestChar]);
    ghostCount3 = nv - ghostStart;
    pvAlpha = 255;
    boxStart = nv;
    box3(0, 0, 0, 0.8f, 0.8f, 0.8f, RGB(255, 200, 40));
    box3(0, 0.82f, 0, 0.82f, 0.02f, 0.82f, RGB(255, 245, 160));
    box3(0, 0, 0.82f, 0.18f, 0.5f, 0.02f, RGB(255, 255, 255));
    box3(0, 0, -0.82f, 0.18f, 0.5f, 0.02f, RGB(255, 255, 255));
    box3(0.82f, 0, 0, 0.02f, 0.5f, 0.18f, RGB(255, 255, 255));
    box3(-0.82f, 0, 0, 0.02f, 0.5f, 0.18f, RGB(255, 255, 255));
    boxCount = nv - boxStart;
    coneStart = nv;
    cone3(0, 0, 0, 0.9f, 2.2f, 8, 0, RGB(250, 120, 20));
    cone3(0, 0.7f, 0, 0.64f, 1.0f, 8, 0, RGB(255, 255, 255));
    box3(0, 0.1f, 0, 1.0f, 0.1f, 1.0f, RGB(60, 60, 66));
    coneCount = nv - coneStart;
    flameStart = nv;
    /* cok katmanli turbo alevi: dis turuncu, orta sari, ic beyaz */
    box3(-0.85f, 0.05f, 0.50f, 0.85f, 0.18f, 0.18f, RGB(255, 90, 15));
    box3(-0.85f, 0.05f, -0.50f, 0.85f, 0.18f, 0.18f, RGB(255, 90, 15));
    box3(-0.70f, 0.08f, 0.45f, 0.70f, 0.13f, 0.13f, RGB(255, 170, 30));
    box3(-0.70f, 0.08f, -0.45f, 0.70f, 0.13f, 0.13f, RGB(255, 170, 30));
    box3(-0.50f, 0.10f, 0.40f, 0.50f, 0.09f, 0.09f, RGB(255, 245, 160));
    box3(-0.50f, 0.10f, -0.40f, 0.50f, 0.09f, 0.09f, RGB(255, 245, 160));
    flameCount = nv - flameStart;
    shadowStart = nv;
    /* yumusak cift katman golge */
    quad3(mk(-2.4f, 0.0f, -1.6f), mk(2.8f, 0.0f, -1.6f), mk(2.8f, 0.0f, 1.6f), mk(-2.4f, 0.0f, 1.6f), RGBA(0, 0, 0, 55));
    quad3(mk(-1.7f, 0.01f, -1.1f), mk(2.1f, 0.01f, -1.1f), mk(2.1f, 0.01f, 1.1f), mk(-1.7f, 0.01f, 1.1f), RGBA(0, 0, 0, 120));
    shadowCount = nv - shadowStart;
    /* gercek yesil / kirmizi kabuk ve muz meshleri */
    shellGStart = nv;
    sphereMesh(1.0f, 0.62f, 10, 6, RGB(40, 200, 70), RGB(245, 235, 200), RGB(250, 250, 250));
    shellGCount = nv - shellGStart;
    shellRStart = nv;
    sphereMesh(1.0f, 0.62f, 10, 6, RGB(225, 45, 45), RGB(245, 235, 200), RGB(250, 250, 250));
    shellRCount = nv - shellRStart;
    banStart = nv;
    bananaMesh();
    banCount = nv - banStart;
    buildSparkMesh();
    buildCharacterAccessoryMeshes();
    sceKernelDcacheWritebackAll();
}

/* ---------- Oyun nesneleri ---------- */
typedef struct {
    float x, z, h, vh, spd;
    int idx, lapc;
    float prog, lat;
    float boostT, driftT, spinT, spinA;
    float slowT, starT;
    int shield, driftDir;
    int item;
    float skill, lane, steerVis;
    float bobA, lean, pitch, accelVis, fxTimer;
} Kart;
static Kart K[NK];
static float kartBumpCooldown[NK][NK];

typedef struct { float x, z; int type; int active; float resp; } Thing;

/* ---------- Animasyon / parcacik efektleri ---------- */
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life, maxLife, size;
    int active, kind;
} FxPart;
#define MAXFX 64
static FxPart fx[MAXFX];
static int fxHead = 0;
static float screenFlash = 0.0f;   /* 2D ekran flasi (boost / item) */
static float boostLines = 0.0f;    /* hiz cizgileri yogunlugu */

static void spawnFx(float x, float y, float z, float vx, float vy, float vz,
                    float life, float size, int kind) {
    int id = fxHead++ % MAXFX;
    fx[id].x = x; fx[id].y = y; fx[id].z = z;
    fx[id].vx = vx; fx[id].vy = vy; fx[id].vz = vz;
    fx[id].life = life; fx[id].maxLife = life; fx[id].size = size;
    fx[id].active = 1; fx[id].kind = kind;
}

static void spawnKartFx(int who, int kind, int count) {
    if (who < 0 || who >= NK) return;
    Kart *k = &K[who];
    float ca = cosf(k->h), sa = sinf(k->h);
    for (int n = 0; n < count; n++) {
        float a = k->h + PI + (rnd() - 0.5f) * 1.1f;
        float sp = 5.0f + rnd() * 11.0f + k->spd * 0.08f;
        float side = (rnd() - 0.5f) * 1.6f;
        float dist = 1.6f + rnd() * 1.2f;
        float px = k->x - ca * dist - sa * side;
        float pz = k->z - sa * dist + ca * side;
        float py = 0.12f + rnd() * 0.9f;
        spawnFx(px, py, pz,
                cosf(a) * sp + (rnd() - 0.5f) * 2.5f,
                2.2f + rnd() * 5.5f,
                sinf(a) * sp + (rnd() - 0.5f) * 2.5f,
                0.28f + rnd() * 0.40f,
                0.18f + rnd() * 0.22f,
                kind);
    }
}

static int state, lap, finalRank;

static void updateFx(void) {
    for (int i = 0; i < MAXFX; i++) if (fx[i].active) {
        fx[i].x += fx[i].vx * DT;
        fx[i].y += fx[i].vy * DT;
        fx[i].z += fx[i].vz * DT;
        fx[i].vy -= 12.0f * DT;
        fx[i].vx *= 0.98f;
        fx[i].vz *= 0.98f;
        fx[i].life -= DT;
        if (fx[i].life <= 0 || fx[i].y < 0.02f) fx[i].active = 0;
    }
    if (screenFlash > 0) screenFlash -= DT * 2.5f;
    if (screenFlash < 0) screenFlash = 0;
    /* boost cizgileri: oyuncu boost'taysa yukselt, degilse dusur */
    {
        float target = (state == 1 && K[0].boostT > 0) ? 1.0f : 0.0f;
        boostLines += (target - boostLines) * clampf(8.0f * DT, 0, 1);
    }
}


/* type: 0 = esya kutusu, 1 = trafik konisi, 2 = muz tuzagi */
#define TH_ITEM   0
#define TH_CONE   1
#define TH_BANANA 2
#define MAXTHING 64
static Thing things[MAXTHING];
static int nthings = 0;

#define STATE_MENU  (-1)
#define STATE_CHAR  (-2)
#define STATE_TRACK (-3)
#define STATE_PAUSE (3)
static int cameraMode = 0;
static float cd, raceTime, finalTime, shake, lapFlash, tAnim, camH, fovCur;
static unsigned prevB = 0;

/* ---------- Oyun modu ---------- */
#define GAME_RACE          0
#define GAME_TIME_TRIAL    1
#define GAME_CHAMPIONSHIP  2
#define GAME_CAREER        3

#define STATE_DIFF         (-4)
#define STATE_CUPSELECT    (-5)
#define STATE_STATS        (-6)
#define STATE_CUPRESULT    4

#define NCUPS              4
#define CHAMP_RACES        4

static int gameMode = GAME_RACE;
static int menuSel = 0;
static int diffSel = 2;         /* 50, 100, 150, Mirror */
static int cupSel = 0;
static int champRace = 0;
static int champPoints[NK];
static int champOverallRank = 0;
static int champMedal = 0;
static int careerLevel = 0;
static int careerActive = 0;
static int skyPhase = 0;         /* 0 day, 1 sunset, 2 night, 3 N64 Rainbow Road */
static float skyScroll = 0.0f;

static const char *diffNames[4] = { "50cc", "100cc", "150cc", "Mirror" };
static const float diffBotMul[4] = { 0.78f, 0.93f, 1.08f, 1.08f };
static const char *cupNames[NCUPS] = { "MUSHROOM CUP", "FLOWER CUP", "STAR CUP", "SPECIAL CUP" };
static const int cupTracks[NCUPS][CHAMP_RACES] = {
    {0, 1, 2, 3},       /* Luigi Raceway, Moo Moo, Koopa Beach, Kalimari */
    {4, 5, 6, 7},       /* Turnpike, Frappe, Choco, Mario Raceway */
    {8, 9, 10, 11},     /* Wario Stadium, Sherbet, Royal, Bowser */
    {12, 13, 14, 15}    /* DK Jungle, Yoshi Valley, Banshee, Rainbow Road */
};

static int activeKarts(void) { return gameMode == GAME_TIME_TRIAL ? 1 : NK; }
static int isChampMode(void) { return gameMode == GAME_CHAMPIONSHIP || gameMode == GAME_CAREER; }

/* ---------- Ses: motor + muzik (sentezlenir, dosya gerekmez) ---------- */
#define AUDIO_BLOCK 512
static volatile float aSpd = 0.0f;
static volatile int aEngineOn = 0, aMusicOn = 1, aBoost = 0, aTrackId = 0;
static volatile int aMusicScene = 1; /* 0 menu, 1 race, 2 finish */
static volatile unsigned aSfxSeq = 0;
static volatile int aSfx = 0;
#define SFX_NONE 0
#define SFX_ITEM 1
#define SFX_SHELL 2
#define SFX_BOOST 3
#define SFX_COLLISION 4
#define SFX_DRIFT 5
#define SFX_FINISH 6

/* ---------- Gercek WAV ses bankasi ----------
 * Secilen dosyalar build sirasinda 44.1 kHz / mono / 16-bit PCM'e
 * cevrilir. PSP'de dosyadan okunur; boylece oyun gercek asset seslerini
 * sentez yerine kullanir. */
typedef struct { short *data; int frames; } PcmSample;

enum {
    FSFX_MENU_SELECT = 0,
    FSFX_MENU_CONFIRM,
    FSFX_MENU_BACK,
    FSFX_ITEM,
    FSFX_SHELL,
    FSFX_BOOST,
    FSFX_HIT,
    FSFX_FINISH,
    FSFX_RACE_START,
    FSFX_MARIO,
    FSFX_LUIGI,
    FSFX_YOSHI,
    FSFX_COUNT
};

static PcmSample realSfx[FSFX_COUNT];
static PcmSample realEngine[4];
static PcmSample realMusic[3]; /* menu / race / finish */
static volatile int realSfxId = -1;
static volatile unsigned realSfxSeq = 0;
static volatile int useRealMusic = 0;
static volatile int engineBand = 0;

static uint16_t rd16le(const unsigned char *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32le(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int loadWavFile(const char *path, PcmSample *out) {
    if (!out) return 0;
    out->data = NULL; out->frames = 0;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    SceOff end = sceIoLseek(fd, 0, PSP_SEEK_END);
    if (end <= 44 || end > (16 * 1024 * 1024)) { sceIoClose(fd); return 0; }
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    int size = (int)end;
    unsigned char *file = (unsigned char*)malloc(size);
    if (!file) { sceIoClose(fd); return 0; }
    int got = sceIoRead(fd, file, size);
    sceIoClose(fd);
    if (got != size || memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) { free(file); return 0; }

    int channels = 1, bits = 16, rate = 44100;
    int dataOff = -1, dataSize = 0;
    int pos = 12;
    while (pos + 8 <= size) {
        uint32_t ck = rd32le(file + pos + 4);
        int next = pos + 8 + (int)ck + ((ck & 1u) ? 1 : 0);
        if (next > size) break;
        if (memcmp(file + pos, "fmt ", 4) == 0 && ck >= 16) {
            int fmt = rd16le(file + pos + 8);
            channels = rd16le(file + pos + 10);
            rate = (int)rd32le(file + pos + 12);
            bits = rd16le(file + pos + 22);
            if (fmt != 1) { free(file); return 0; }
        } else if (memcmp(file + pos, "data", 4) == 0) {
            dataOff = pos + 8; dataSize = (int)ck; break;
        }
        pos = next;
    }
    if (dataOff < 0 || dataSize <= 0 || bits != 16 || (channels != 1 && channels != 2)) { free(file); return 0; }
    int inFrames = dataSize / (2 * channels);
    if (inFrames <= 0) { free(file); return 0; }
    short *dst = (short*)memalign(16, (size_t)inFrames * sizeof(short));
    if (!dst) { free(file); return 0; }
    const unsigned char *pcm = file + dataOff;
    for (int i = 0; i < inFrames; i++) {
        int a = (int16_t)rd16le(pcm + i * channels * 2);
        int v = a;
        if (channels == 2) {
            int b = (int16_t)rd16le(pcm + i * 4 + 2);
            v = (a + b) / 2;
        }
        dst[i] = (short)v;
    }
    free(file);
    /* 44.1 kHz expected by PSP audio channel. Files in this project are normalized to it. */
    (void)rate;
    sceKernelDcacheWritebackInvalidateAll();
    out->data = dst; out->frames = inFrames;
    return 1;
}

static int loadAudioBank(void) {
    static const char *sfxPath[FSFX_COUNT] = {
        ASSET_ROOT "assets/audio/menu_select.wav",
        ASSET_ROOT "assets/audio/menu_confirm.wav",
        ASSET_ROOT "assets/audio/menu_back.wav",
        ASSET_ROOT "assets/audio/item.wav",
        ASSET_ROOT "assets/audio/shell.wav",
        ASSET_ROOT "assets/audio/boost.wav",
        ASSET_ROOT "assets/audio/hit.wav",
        ASSET_ROOT "assets/audio/finish.wav",
        ASSET_ROOT "assets/audio/race_start.wav",
        ASSET_ROOT "assets/audio/mario_voice.wav",
        ASSET_ROOT "assets/audio/luigi_voice.wav",
        ASSET_ROOT "assets/audio/yoshi_voice.wav",
    };
    static const char *enginePath[4] = {
        ASSET_ROOT "assets/audio/engine0.wav",
        ASSET_ROOT "assets/audio/engine3.wav",
        ASSET_ROOT "assets/audio/engine6.wav",
        ASSET_ROOT "assets/audio/engine9.wav",
    };
    static const char *musicPath[3] = {
        ASSET_ROOT "assets/audio/menu_music.wav",
        ASSET_ROOT "assets/audio/race_music.wav",
        ASSET_ROOT "assets/audio/finish_music.wav",
    };
    int ok = 0;
    for (int i = 0; i < FSFX_COUNT; i++) if (loadWavFile(sfxPath[i], &realSfx[i])) ok++;
    for (int i = 0; i < 4; i++) if (loadWavFile(enginePath[i], &realEngine[i])) ok++;
    int musicOk = 0;
    for (int i = 0; i < 3; i++) if (loadWavFile(musicPath[i], &realMusic[i])) musicOk++;
    useRealMusic = (musicOk == 3) ? 1 : 0;
    return ok + musicOk;
}

static void playFileSfx(int id) {
    if (id < 0 || id >= FSFX_COUNT || !realSfx[id].data) return;
    realSfxId = id;
    realSfxSeq++;
}

static void playMenuSelect(void) { playFileSfx(FSFX_MENU_SELECT); }
static void playMenuConfirm(void) { playFileSfx(FSFX_MENU_CONFIRM); }
static void playMenuBack(void) { playFileSfx(FSFX_MENU_BACK); }

static void triggerSfx(int s) {
    aSfx=s; aSfxSeq++;
    switch (s) {
        case SFX_ITEM:      playFileSfx(FSFX_ITEM); break;
        case SFX_SHELL:     playFileSfx(FSFX_SHELL); break;
        case SFX_BOOST:     playFileSfx(FSFX_BOOST); break;
        case SFX_COLLISION: playFileSfx(FSFX_HIT); break;
        case SFX_DRIFT:     playFileSfx(FSFX_BOOST); break;
        case SFX_FINISH:    playFileSfx(FSFX_FINISH); break;
        default: break;
    }
}

static int realSfxThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_MONO);
    if (ch < 0) return 0;
    static short buf[AUDIO_BLOCK];
    int cur = -1, pos = 0; unsigned seq = 0;
    for (;;) {
        if (seq != realSfxSeq) { seq = realSfxSeq; cur = realSfxId; pos = 0; }
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            if (cur >= 0 && cur < FSFX_COUNT && realSfx[cur].data && pos < realSfx[cur].frames) buf[i] = realSfx[cur].data[pos++];
            else { buf[i] = 0; cur = -1; }
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}

static int realEngineThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_MONO);
    if (ch < 0) return 0;
    static short buf[AUDIO_BLOCK];
    int curBand = -1, pos = 0;
    for (;;) {
        int band = aEngineOn ? engineBand : -1;
        if (band != curBand) { curBand = band; pos = 0; }
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            if (curBand >= 0 && curBand < 4 && realEngine[curBand].data && realEngine[curBand].frames > 0) {
                buf[i] = realEngine[curBand].data[pos++];
                if (pos >= realEngine[curBand].frames) pos = 0;
            } else buf[i] = 0;
        }
        int vol = aEngineOn ? (PSP_AUDIO_VOLUME_MAX * 3 / 10) : 0;
        sceAudioOutputBlocking(ch, vol, buf);
    }
    return 0;
}

static int realMusicThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_MONO);
    if (ch < 0) return 0;
    static short buf[AUDIO_BLOCK];
    int sceneCur = -1, pos = 0;
    for (;;) {
        int scene = aMusicScene;
        if (!aMusicOn || !useRealMusic || scene < 0 || scene > 2) scene = -1;
        if (scene != sceneCur) { sceneCur = scene; pos = 0; }
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            if (sceneCur >= 0 && realMusic[sceneCur].data && realMusic[sceneCur].frames > 0) {
                buf[i] = realMusic[sceneCur].data[pos++];
                if (pos >= realMusic[sceneCur].frames) pos = 0;
            } else buf[i] = 0;
        }
        int vol = (sceneCur >= 0) ? (PSP_AUDIO_VOLUME_MAX * 4 / 10) : 0;
        sceAudioOutputBlocking(ch, vol, buf);
    }
    return 0;
}

#define REST 99
static const signed char leadPat[NTRACKS][16] = {
    { 0, 4, 7, 12, 7, 4, 7, 9, 5, 9, 12, 9, 7, 4, 2, 4 },
    { 0, 3, 7, 10, 12, 10, 7, 3, 5, 8, 12, 8, 7, 10, 14, 10 },
    { 12, REST, 7, REST, 9, REST, 5, REST, 7, REST, 4, REST, 2, 4, 5, 7 },
    { 0, REST, 3, 5, 7, REST, 5, 3, 0, REST, -2, 0, 3, 5, 7, REST },
    { 0, 5, 7, 12, 10, 7, 5, 3, 0, 3, 7, 10, 12, 7, 5, 0 },
    { 0, REST, 4, REST, 7, REST, 11, REST, 12, 11, 7, 4, 0, 4, 7, 12 },
    { 0, 2, 5, 7, 9, 7, 5, 2, 0, 5, 9, 12, 9, 5, 2, 0 },
    { 0, 4, 7, 12, 7, 4, 0, 4, 5, 9, 12, 9, 7, 5, 4, 0 },
    { 0, 3, 5, 8, 12, 8, 5, 3, 0, REST, 5, REST, 8, 12, 8, 5 },
    { 12, 9, 7, 4, 0, 4, 7, 9, 12, REST, 7, REST, 4, 0, 4, 7 },
    { 0, 5, 9, 12, 9, 5, 0, 5, 7, 12, 14, 12, 9, 7, 5, 0 },
    { 0, REST, 3, REST, 7, REST, 10, REST, 12, 10, 7, 3, 0, 3, 7, REST },
    { 0, 4, 7, 9, 12, 9, 7, 4, 2, 5, 9, 12, 9, 5, 2, 0 },
    { 0, 5, 7, 10, 12, 14, 12, 10, 7, 5, 3, 0, 3, 7, 12, 7 },
    { 0, REST, 5, REST, 8, REST, 12, REST, 8, 5, 0, 5, 8, 12, 8, REST },
    { 0, 4, 7, 11, 12, 16, 12, 11, 7, 4, 0, 4, 7, 12, 16, 12 },
};
static const signed char barRoot[NTRACKS][4] = {
    { 0, 5, 7, 3 }, { 0, -4, -2, -5 }, { 0, -3, 5, 2 }, { 0, 3, -2, -4 },
    { 0, 5, 3, 7 }, { 0, -5, -2, 3 }, { 0, 4, 7, 2 }, { 0, 5, 7, 4 },
    { 0, -3, 5, -5 }, { 0, 4, -2, 7 }, { 0, 5, 9, 3 }, { 0, -4, 3, -7 },
    { 0, 5, 2, 7 }, { 0, 3, 7, -2 }, { 0, -5, 3, -3 }, { 0, 7, 12, 5 },
};
static const float baseFreq[NTRACKS] = {
    261.63f, 220.0f, 293.66f, 196.0f,
    246.94f, 277.18f, 233.08f, 261.63f,
    185.0f, 311.13f, 329.63f, 174.61f,
    207.65f, 293.66f, 155.56f, 349.23f
};
static const float stepRate[NTRACKS] = {
    9.0f, 8.0f, 10.0f, 7.5f,
    9.5f, 8.5f, 9.0f, 9.2f,
    8.0f, 8.8f, 9.5f, 7.8f,
    9.0f, 10.0f, 7.2f, 11.0f
};

static int audioThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    static short buf[AUDIO_BLOCK * 2];
    float leadPh = 0, bassPh = 0, engPh = 0, eng2Ph = 0, sfxPh = 0, leadF = 0, bassF = 0, stepT = 0, env = 0, engVol = 0;
    int step = 0, lastSfxSeq = 0, sfxType = 0; float sfxTime = 0;
    unsigned noise = 1;
    const float inv = 1.0f / 44100.0f;
    while (1) {
        int tr = aTrackId;
        if (tr < 0 || tr >= NTRACKS) tr = 0;
        float ef = 48.0f + aSpd * 3.6f + (aBoost ? 30.0f : 0.0f);
        float engTarget = aEngineOn ? 1.0f : 0.0f;
        int musicOn = aMusicOn;
        int scene = aMusicScene;
        if (aSfxSeq != (unsigned)lastSfxSeq) { lastSfxSeq=(int)aSfxSeq; sfxType=aSfx; sfxTime=0; }
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            stepT += stepRate[tr] * inv;
            if (stepT >= 1.0f) {
                stepT -= 1.0f;
                step = (step + 1) & 63;
                int ls = leadPat[tr][step & 15], root = barRoot[tr][(step >> 4) & 3];
                leadF = (ls == REST) ? 0.0f : baseFreq[tr] * powf(2.0f, (ls + root) / 12.0f);
                if ((step & 3) == 0) bassF = baseFreq[tr] * 0.5f * powf(2.0f, root / 12.0f);
                env = 1.0f;
            }
            env *= 0.9997f;
            leadPh += leadF * inv; if (leadPh >= 1.0f) leadPh -= 1.0f;
            bassPh += bassF * inv; if (bassPh >= 1.0f) bassPh -= 1.0f;
            float lead = (leadF > 0.0f) ? (leadPh < 0.25f ? 0.5f : -0.5f) * env : 0.0f;
            float bass = (bassPh < 0.5f ? 4.0f * bassPh - 1.0f : 3.0f - 4.0f * bassPh) * 0.5f;
            float music = (musicOn && !useRealMusic) ? (lead * (scene==0?0.24f:0.30f) + bass * (scene==2?0.20f:0.32f)) : 0.0f;
            if(scene==0) music += 0.10f * sinf(leadPh*2.0f*PI);
            if(scene==2) music += 0.12f * sinf(leadPh*2.0f*PI*0.5f);

            engPh += ef * inv; if (engPh >= 1.0f) engPh -= 1.0f;
            eng2Ph += ef * 2.01f * inv; if (eng2Ph >= 1.0f) eng2Ph -= 1.0f;
            noise = noise * 1664525u + 1013904223u;
            float nz = ((noise >> 16) & 0xFF) / 128.0f - 1.0f;
            engVol += (engTarget - engVol) * 0.0005f;
            float eng = ((2.0f * engPh - 1.0f) * 0.5f + (eng2Ph < 0.5f ? 0.25f : -0.25f) + nz * 0.08f) * 0.30f * engVol;

            float sfx = 0.0f;
            if(sfxType != SFX_NONE) {
                float dur=(sfxType==SFX_FINISH?0.9f:(sfxType==SFX_BOOST?0.28f:0.20f));
                if(sfxTime<dur) {
                    float f=180.0f;
                    if(sfxType==SFX_ITEM) f=880.0f + 180.0f*sinf(sfxTime*28.0f);
                    else if(sfxType==SFX_SHELL) f=220.0f+260.0f*sinf(sfxTime*16.0f);
                    else if(sfxType==SFX_BOOST) f=120.0f+700.0f*(1.0f-sfxTime/dur);
                    else if(sfxType==SFX_COLLISION) f=85.0f;
                    else if(sfxType==SFX_DRIFT) f=360.0f+90.0f*sinf(sfxTime*30.0f);
                    else if(sfxType==SFX_FINISH) f=520.0f+220.0f*sinf(sfxTime*7.0f);
                    sfxPh += f*inv; if(sfxPh>=1.0f) sfxPh-=1.0f;
                    float pulse = 1.0f - sfxTime/dur;
                    sfx = (sinf(sfxPh*2.0f*PI)*0.42f + nz*0.08f) * pulse;
                } else sfxType=SFX_NONE;
                sfxTime += inv;
            }
            float o = music + eng + sfx;
            if (o > 1.0f) o = 1.0f;
            if (o < -1.0f) o = -1.0f;
            short sm = (short)(o * 14000.0f);
            buf[2 * i] = sm; buf[2 * i + 1] = sm;
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}
static int realMixedAudioThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_MONO);
    if (ch < 0) return 0;
    static short buf[AUDIO_BLOCK];
    int sfxId = -1, sfxPos = 0; unsigned seenSeq = 0;
    int engBandCur = -1, engPos = 0;
    int musicSceneCur = -1, musicPos = 0;
    for (;;) {
        if (seenSeq != realSfxSeq) { seenSeq = realSfxSeq; sfxId = realSfxId; sfxPos = 0; }
        int eb = aEngineOn ? (int)clampf((float)engineBand, 0.0f, 3.0f) : -1;
        if (eb != engBandCur) { engBandCur = eb; engPos = 0; }
        int ms = aMusicOn ? aMusicScene : -1;
        if (ms != musicSceneCur) { musicSceneCur = ms; musicPos = 0; }
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            float mix = 0.0f;
            if (engBandCur >= 0 && engBandCur < 4 && realEngine[engBandCur].data && realEngine[engBandCur].frames > 0) {
                mix += ((float)realEngine[engBandCur].data[engPos++] / 32768.0f) * 0.34f;
                if (engPos >= realEngine[engBandCur].frames) engPos = 0;
            }
            if (musicSceneCur >= 0 && musicSceneCur < 3 && realMusic[musicSceneCur].data && realMusic[musicSceneCur].frames > 0) {
                mix += ((float)realMusic[musicSceneCur].data[musicPos++] / 32768.0f) * 0.20f;
                if (musicPos >= realMusic[musicSceneCur].frames) musicPos = 0;
            }
            if (sfxId >= 0 && sfxId < FSFX_COUNT && realSfx[sfxId].data && sfxPos < realSfx[sfxId].frames) {
                mix += ((float)realSfx[sfxId].data[sfxPos++] / 32768.0f) * 0.60f;
                if (sfxPos >= realSfx[sfxId].frames) sfxId = -1;
            }
            if (mix > 0.95f) mix = 0.95f;
            if (mix < -0.95f) mix = -0.95f;
            buf[i] = (short)(mix * 30000.0f);
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}

static void startAudio(void) {
    /* Tek kanal: WAV efekt + motor + muzik birlikte mikslenir. */
    int th = sceKernelCreateThread("real_mix_audio", realMixedAudioThread, 0x12, 0x9000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

/* ---------- Dokular (kodla uretilir, tekrarlanan 64x64) ---------- */
static unsigned int __attribute__((aligned(16))) texGrass[64 * 64];
static unsigned int __attribute__((aligned(16))) texRoad[64 * 64];
static void makeTextures(void) {
    unsigned seed = 777u;
    for (int pass = 0; pass < 2; pass++) {
        unsigned int *tex = pass == 0 ? texGrass : texRoad;
        float coarse[8][8];
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            seed = seed * 1664525u + 1013904223u;
            coarse[y][x] = ((seed >> 8) & 0xFF) / 255.0f;
        }
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            seed = seed * 1664525u + 1013904223u;
            float n = ((seed >> 8) & 0xFF) / 255.0f;
            seed = seed * 1664525u + 1013904223u;
            float n2 = ((seed >> 8) & 0xFF) / 255.0f;
            float c = coarse[y / 8][x / 8];
            float v;
            int r, g, b;
            if (pass == 0) {            /* cim: yaprak benekleri */
                v = 205.0f + 28.0f * c + 22.0f * n;
                if (n2 > 0.93f) v -= 38.0f;
                r = (int)(v * 0.96f); g = (int)v; b = (int)(v * 0.88f);
            } else {                    /* asfalt: ince cakil */
                v = 215.0f + 14.0f * c + 22.0f * n;
                if (n2 > 0.95f) v -= 55.0f;
                r = (int)v; g = (int)v; b = (int)(v * 1.02f);
            }
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            tex[y * 64 + x] = RGB(r, g, b);
        }
    }
    sceKernelDcacheWritebackAll();
}


/* ---------- Gercek PNG texture sistemi + sabit pointer cache + atlas ---------- */
typedef struct {
    char path[96];
    unsigned char *data;
    int w, h;
    unsigned stamp;
    int ready;
} PspTexture;
#define TEX_CACHE_MAX 80
static PspTexture texCache[TEX_CACHE_MAX];
static unsigned texStamp = 1;
static PspTexture *texGrassFile = NULL, *texRoadFile = NULL;
static PspTexture *texSkyDay = NULL, *texSkySunset = NULL, *texSkyNight = NULL;
static PspTexture *texSkyRainbow = NULL; /* precomposed PSP-friendly N64 Rainbow Road panorama */
static PspTexture *texCloud = NULL, *texAtlas = NULL, *texMountain = NULL, *texItemBox = NULL;
static PspTexture *texTitilliumFont = NULL; /* embedded Titillium Web Bold bitmap atlas */
static PspTexture *texFace[NK][3];
static PspTexture *texMarioModel[5], *texYoshiModel[2], *texLuigiModel[2];
static PspTexture *texWarioModel[5], *texToadModel[2], *texPeachModel[5];
static PspTexture *kartTextures[KART_TEX_COUNT];
static PspTexture *kartFrameVariants[NK];
/* Built-in color materials guarantee that imported 3D kart meshes still draw
   even if a Memory Stick asset path or a PNG is missing. 14 x 64 x 64 x RGBA. */
static unsigned int __attribute__((aligned(16))) kartFallbackTex[KART_TEX_COUNT][64 * 64];
static int textureAssetCount = 0;
static int audioAssetCount = 0;

static void makeKartFallbackTextures(void) {
    static const unsigned int colors[KART_TEX_COUNT] = {
        RGB(220, 28, 35), RGB(30, 190, 55), RGB(40, 175, 65),
        RGB(48, 48, 55), RGB(170, 170, 178), RGB(64, 65, 72),
        RGB(138, 140, 148), RGB(62, 39, 24), RGB(100, 65, 45),
        RGB(225, 125, 24), RGB(220, 80, 155), RGB(215, 215, 220),
        RGB(245, 205, 25), RGB(27, 28, 31)
    };
    for (int t = 0; t < KART_TEX_COUNT; ++t) {
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                /* Light checker subtly reveals panels without real textures. */
                float f = (((x >> 3) ^ (y >> 3)) & 1) ? 1.0f : 0.90f;
                kartFallbackTex[t][y * 64 + x] = scol(colors[t], f);
            }
        }
    }
    sceKernelDcacheWritebackAll();
}

typedef struct { int x, y, w, h; } AtlasUV;
static const AtlasUV atlasUV[8] = {
    {0,0,64,64},{64,0,64,64},{128,0,64,64},{192,0,64,64},
    {0,64,64,64},{64,64,64,64},{128,64,64,64},{192,64,64,64}
};

static int texPow2(int n) { int p=1; while(p<n && p<1024) p<<=1; return p; }
static PspTexture *texFind(const char *path) {
    for (int i=0;i<TEX_CACHE_MAX;i++) if (texCache[i].ready && strcmp(texCache[i].path,path)==0) { texCache[i].stamp=++texStamp; return &texCache[i]; }
    return NULL;
}
static PspTexture *loadPngTexture(const char *path) {
    PspTexture *hit=texFind(path); if(hit) return hit;
    /* Texture pointers are stored in several model banks. Never evict a live
       slot: replacing an LRU slot would silently turn previously loaded model
       textures into pointers to another image and create visual corruption. */
    int slot=-1;
    for(int i=0;i<TEX_CACHE_MAX;i++) {
        if(!texCache[i].ready && texCache[i].data == NULL) { slot=i; break; }
    }
    if(slot<0) return NULL;
    int w=0,h=0,nc=0;
    unsigned char *raw=stbi_load(path,&w,&h,&nc,4);
    if(!raw) { texCache[slot].ready=0; return NULL; }
    int tw=texPow2(w), th=texPow2(h);
    if(tw>512 || th>512) { stbi_image_free(raw); texCache[slot].ready=0; return NULL; }
    unsigned char *dst=(unsigned char*)memalign(16,(size_t)tw*(size_t)th*4u);
    if(!dst){ stbi_image_free(raw); texCache[slot].ready=0; return NULL; }
    memset(dst,0,(size_t)tw*(size_t)th*4u);
    for(int y=0;y<h;y++) memcpy(dst+(size_t)y*tw*4u,raw+(size_t)y*w*4u,(size_t)w*4u);
    stbi_image_free(raw);
    memset(texCache[slot].path,0,sizeof(texCache[slot].path)); strncpy(texCache[slot].path,path,sizeof(texCache[slot].path)-1);
    texCache[slot].data=dst; texCache[slot].w=tw; texCache[slot].h=th; texCache[slot].stamp=++texStamp; texCache[slot].ready=1;
    sceKernelDcacheWritebackInvalidateAll();
    return &texCache[slot];
}
static int loadTextureBank(void) {
    texGrassFile=loadPngTexture(ASSET_ROOT "assets/textures/grass.png");
    texRoadFile=loadPngTexture(ASSET_ROOT "assets/textures/road.png");
    texSkyDay=loadPngTexture(ASSET_ROOT "assets/textures/sky_day.png");
    texSkySunset=loadPngTexture(ASSET_ROOT "assets/textures/sky_sunset.png");
    texSkyNight=loadPngTexture(ASSET_ROOT "assets/textures/sky_night.png");
    texSkyRainbow=loadPngTexture(ASSET_ROOT "assets/textures/sky_rainbow_road.png");
    texCloud=loadPngTexture(ASSET_ROOT "assets/textures/cloud.png");
    texAtlas=loadPngTexture(ASSET_ROOT "assets/textures/atlas.png");
    texTitilliumFont=loadPngTexture(ASSET_ROOT "assets/textures/font_titillium.png");
    texMountain=loadPngTexture(ASSET_ROOT "assets/textures/mountain.png");
    (void)loadPngTexture(ASSET_ROOT "assets/textures/kart_red.png");
    (void)loadPngTexture(ASSET_ROOT "assets/textures/kart_blue.png");
    (void)loadPngTexture(ASSET_ROOT "assets/textures/kart_green.png");
    (void)loadPngTexture(ASSET_ROOT "assets/textures/kart_yellow.png");
    texItemBox=loadPngTexture(ASSET_ROOT "assets/textures/item_box.png");
    (void)loadPngTexture(ASSET_ROOT "assets/textures/spark.png");

    /* Uploaded character textures: Mario/Luigi/Yoshi faces + extra expressions. */
    /* Mario yuz animasyonu */
    texFace[0][0]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face0.png");
    texFace[0][1]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face1.png");
    texFace[0][2]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face2.png");
    /* Luigi */
    texFace[1][0]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_face0.png");
    texFace[1][1]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_face1.png");
    texFace[1][2]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_face2.png");
    /* Yoshi */
    texFace[2][0]=loadPngTexture(ASSET_ROOT "assets/textures/yoshi_face.png");
    texFace[2][1]=texFace[2][0]; texFace[2][2]=texFace[2][0];
    /* Wario: ofkeli Mario yuzleri */
    texFace[3][0]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face_angry.png");
    texFace[3][1]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face2.png");
    texFace[3][2]=texFace[3][0];
    /* Toad: Luigi ifadeleri (kucuk surucu) */
    texFace[4][0]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_face_angry.png");
    texFace[4][1]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_face0.png");
    texFace[4][2]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_face1.png");
    /* Peach: mutlu Mario varyasyonu */
    texFace[5][0]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face_happy.png");
    texFace[5][1]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face0.png");
    texFace[5][2]=loadPngTexture(ASSET_ROOT "assets/textures/mario_face1.png");

    /* Kullanici tarafindan verilen 3D model texturelari. */
    texMarioModel[0]=loadPngTexture(ASSET_ROOT "assets/textures/mario_model_hair.png");
    texMarioModel[1]=loadPngTexture(ASSET_ROOT "assets/textures/mario_model_cap.png");
    texMarioModel[2]=loadPngTexture(ASSET_ROOT "assets/textures/mario_model_face.png");
    texMarioModel[3]=loadPngTexture(ASSET_ROOT "assets/textures/mario_model_overalls.png");
    texMarioModel[4]=loadPngTexture(ASSET_ROOT "assets/textures/mario_model_glove.png");
    texYoshiModel[0]=loadPngTexture(ASSET_ROOT "assets/textures/yoshi_model_body.png");
    texYoshiModel[1]=loadPngTexture(ASSET_ROOT "assets/textures/yoshi_model_belly.png");
    texLuigiModel[0]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_model_body.png");
    texLuigiModel[1]=loadPngTexture(ASSET_ROOT "assets/textures/luigi_model_face.png");

    /* Character-specific material variants: Wario (yellow/purple), Toad
       (white/red/blue mushroom palette), and Peach (pink/blonde). */
    texWarioModel[0]=loadPngTexture(ASSET_ROOT "assets/textures/wario_model_hair.png");
    texWarioModel[1]=loadPngTexture(ASSET_ROOT "assets/textures/wario_model_cap.png");
    texWarioModel[2]=loadPngTexture(ASSET_ROOT "assets/textures/wario_model_face.png");
    texWarioModel[3]=loadPngTexture(ASSET_ROOT "assets/textures/wario_model_overalls.png");
    texWarioModel[4]=loadPngTexture(ASSET_ROOT "assets/textures/wario_model_glove.png");
    texToadModel[0]=loadPngTexture(ASSET_ROOT "assets/textures/toad_model_body.png");
    texToadModel[1]=loadPngTexture(ASSET_ROOT "assets/textures/toad_model_face.png");
    texPeachModel[0]=loadPngTexture(ASSET_ROOT "assets/textures/peach_model_hair.png");
    texPeachModel[1]=loadPngTexture(ASSET_ROOT "assets/textures/peach_model_cap.png");
    texPeachModel[2]=loadPngTexture(ASSET_ROOT "assets/textures/peach_model_face.png");
    texPeachModel[3]=loadPngTexture(ASSET_ROOT "assets/textures/peach_model_overalls.png");
    texPeachModel[4]=loadPngTexture(ASSET_ROOT "assets/textures/peach_model_glove.png");

    /* Actual Pipe Frame 64 material maps. All names are supplied in kart_models.h. */
    for (int kt=0; kt<KART_TEX_COUNT; ++kt) {
        char path[160];
        snprintf(path, sizeof(path), ASSET_ROOT "assets/textures/%s", kartTexFileNames[kt]);
        kartTextures[kt]=loadPngTexture(path);
    }
    kartFrameVariants[0]=kartTextures[0];
    kartFrameVariants[1]=kartTextures[1];
    kartFrameVariants[2]=kartTextures[2];
    kartFrameVariants[3]=loadPngTexture(ASSET_ROOT "assets/textures/frame_wario.png");
    kartFrameVariants[4]=loadPngTexture(ASSET_ROOT "assets/textures/frame_toad.png");
    kartFrameVariants[5]=loadPngTexture(ASSET_ROOT "assets/textures/frame_peach.png");
    textureAssetCount = 0;
    for (int i = 0; i < 3; i++) if (texFace[i][0] && texFace[i][0]->ready) textureAssetCount++;
    return textureAssetCount;
}
static void bindTexture(const PspTexture *t, const void *fallback) {
    /* Track/ground UVs in this project are measured in texture texels. */
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexMode(GU_PSM_8888,0,0,0); sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGB); sceGuTexFilter(GU_LINEAR,GU_LINEAR); sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    if(t && t->ready) sceGuTexImage(0,t->w,t->h,t->w,t->data);
    else sceGuTexImage(0,64,64,64,fallback);
    sceGuTexFlush();
}

typedef struct { float u,v; unsigned c; float x,y,z; } V2T;
#define VF2T (GU_COLOR_8888 | GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
static void drawTexRect(const PspTexture *t,float x,float y,float w,float h,float u0,float v0,float u1,float v1,unsigned c) {
    if(!t || !t->ready) return;
    V2T *v=(V2T*)sceGuGetMemory(2*sizeof(V2T));
    v[0].u=u0; v[0].v=v0; v[0].c=c; v[0].x=x; v[0].y=y; v[0].z=0;
    v[1].u=u1; v[1].v=v1; v[1].c=c; v[1].x=x+w; v[1].y=y+h; v[1].z=0;
    sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    /* Face quad UVs are written in source texel units. */
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexImage(0,t->w,t->h,t->w,t->data);
    sceGuTexFlush();
    sceGuEnable(GU_TEXTURE_2D); sceGuDrawArray(GU_SPRITES,VF2T,2,0,v); sceGuDisable(GU_TEXTURE_2D);
}
/* Draw Titillium atlas text as one textured sprite batch per shadow/color pass.
   Texture cells are 16x16, ASCII 32..126 in row-major order (16 columns). */
static int drawTitilliumText(int x, int y, int w, int h, int t, const char *s, unsigned c) {
    if (!texTitilliumFont || !texTitilliumFont->ready || !texTitilliumFont->data || !s || !*s) return 0;
    int count = 0;
    for (const unsigned char *p=(const unsigned char*)s; *p; ++p) if (*p != ' ') ++count;
    if (count <= 0) return 1;
    if (count > 160) return 0; /* preserve old-font fallback for pathological strings */

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f); /* GU_TRANSFORM_2D uses texel-space UVs directly */
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexImage(0, texTitilliumFont->w, texTitilliumFont->h,
                  texTitilliumFont->w, texTitilliumFont->data);
    sceGuTexFlush();

    for (int pass=0; pass<2; ++pass) {
        unsigned col = pass == 0 ? RGBA(0,0,0,190) : c;
        V2T *v = (V2T*)sceGuGetMemory((size_t)count * 2u * sizeof(V2T));
        if (!v) { sceGuDisable(GU_TEXTURE_2D); return 0; }
        int n=0, cx=x;
        for (const unsigned char *p=(const unsigned char*)s; *p; ++p) {
            unsigned char ch=*p;
            if (ch==' ') { cx += w; continue; }
            if (ch < 32 || ch > 126) ch='?';
            int idx=(int)ch-32;
            int gw=w, adv=w+t+3;
            if (ch=='.' || ch==':') { gw = (t > 1) ? t+1 : 2; adv=t+3; }
            else if (ch=='/') { gw=w; adv=w; }
            int gx=(idx % 16)*16, gy=(idx / 16)*16;
            float ox = (pass==0)?1.0f:0.0f, oy=(pass==0)?1.0f:0.0f;
            V2T *a=&v[2*n];
            a[0].u=(float)gx; a[0].v=(float)gy; a[0].c=col;
            a[0].x=(float)cx+ox; a[0].y=(float)y+oy; a[0].z=0;
            a[1].u=(float)(gx+16); a[1].v=(float)(gy+16); a[1].c=col;
            a[1].x=(float)(cx+gw)+ox; a[1].y=(float)(y+h)+oy; a[1].z=0;
            ++n;
            cx += adv;
        }
        if (n>0) sceGuDrawArray(GU_SPRITES, VF2T, 2*n, 0, v);
    }
    sceGuDisable(GU_TEXTURE_2D);
    return 1;
}

static void drawAtlasIcon(int cell,float x,float y,float size,unsigned c) {
    if(!texAtlas || !texAtlas->ready || cell<0 || cell>=8) return;
    AtlasUV a=atlasUV[cell]; drawTexRect(texAtlas,x,y,size,size,(float)a.x,(float)a.y,(float)(a.x+a.w),(float)(a.y+a.h),c);
}

static int charModelId(int cid) {
    if (cid < 0 || cid >= NK) return 0;
    return charMeshId[cid];
}

static const CharModelPart *charModelPartsFor(int cid, int *count) {
    int m = charModelId(cid);
    if (m == 0) { *count = MARIO_MODEL_PARTS; return marioModelParts; }
    if (m == 1) { *count = LUIGI_MODEL_PARTS; return luigiModelParts; }
    *count = YOSHI_MODEL_PARTS; return yoshiModelParts;
}

static const PspTexture *charModelTexFor(int cid, int part) {
    if (cid == 0 && part >= 0 && part < 5) return texMarioModel[part];
    if (cid == 1 && part >= 0 && part < 2) return texLuigiModel[part];
    if (cid == 2 && part >= 0 && part < 2) return texYoshiModel[part];
    if (cid == 3 && part >= 0 && part < 5) return texWarioModel[part];
    if (cid == 4 && part >= 0 && part < 2) return texToadModel[part];
    if (cid == 5 && part >= 0 && part < 5) return texPeachModel[part];
    return NULL;
}

static int shouldDrawDriver(int who) {
    /* Never hide a driver because another kart became closer. The previous
       nearest-rival LOD made racers appear to vanish while bumping/overlapping. */
    return (who >= 0 && who < activeKarts());
}

static const KartModelPart *kartPartsFor(int who, int *count) {
    int mid = charModelId(kChar[who]);
    if (mid == 1) { *count = LUIGI_KART_PARTS; return luigiKartParts; }
    if (mid == 2) { *count = YOSHI_KART_PARTS; return yoshiKartParts; }
    *count = MARIO_KART_PARTS;
    return marioKartParts;
}

static void drawKartModel(int who) {
    if (who < 0 || who >= NK) return;
    int count = 0;
    const KartModelPart *parts = kartPartsFor(who, &count);
    /* Animate all wheels for the player, and nearby rivals only. Distant rivals
       still use the same complete 3D mesh but are submitted in one draw call per
       wheel material, reducing PSP GU call overhead. */
    int animateWheels = (who == 0);
    float dxPlayer = K[who].x - K[0].x, dzPlayer = K[who].z - K[0].z;
    float dist2Player = dxPlayer * dxPlayer + dzPlayer * dzPlayer;
    if (!animateWheels && who > 0 && who < NK) {
        animateWheels = dist2Player < (42.0f * 42.0f);
    }
    /* PSP performance LOD: distant rival karts keep their main silhouette,
       but tiny material submeshes are skipped when they contribute very few pixels. */
    int distantKart = (who != 0 && dist2Player > (95.0f * 95.0f));

    /* The source COLLADA mesh is Z-up; its named front wheels are at source
       Y < 0. Rotate Z-up to Y-up, then source -Y forward to game +X forward. */
    ScePspFVector3 lift = { 0.0f, 0.005f, 0.0f };
    ScePspFVector3 scale = { 4.15f, 4.15f, 4.15f };
    sceGumTranslate(&lift);
    sceGumRotateY(PI * 0.5f);
    sceGumRotateX(-PI * 0.5f);
    sceGumScale(&scale);

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexOffset(0.0f, 0.0f);

    for (int i = 0; i < count; ++i) {
        int partTex = parts[i].tex;
        int texId = partTex;
        int cid = kChar[who];
        int frameFallback = (cid == 1 || cid == 4) ? 1 : ((cid == 2) ? 2 : 0);
        if (partTex == KART_TEX_FRAME) texId = frameFallback;
        if (parts[i].count <= 0) continue;
        if (distantKart && parts[i].count < 180 && partTex != KART_TEX_FRAME) continue;
        if (texId < 0 || texId >= KART_TEX_COUNT) texId = 3;

        const PspTexture *tx = (partTex == KART_TEX_FRAME && cid >= 0 && cid < NK &&
                                kartFrameVariants[cid] && kartFrameVariants[cid]->ready)
                               ? kartFrameVariants[cid] : kartTextures[texId];
        if (tx && tx->ready && tx->data) {
            /* Model UVs are normalized to 0..1 (repeat coordinates may exceed that); scale back to texel units. */
            sceGuTexScale((float)tx->w, (float)tx->h);
            sceGuTexImage(0, tx->w, tx->h, tx->w, tx->data);
        } else {
            /* Do not skip geometry when external assets are absent. Use an
               embedded flat material so every kart remains a visible 3D mesh. */
            sceGuTexScale(64.0f, 64.0f);
            sceGuTexImage(0, 64, 64, 64, kartFallbackTex[texId]);
        }
        sceGuTexFlush();

        /* Turn each of the four wheels around its own pivot. Wheel centers
           remain in the source model's local coordinates; parent transforms
           translate them with the complete kart. */
        if ((partTex == 9 || partTex == 12 || partTex == 13) && animateWheels) {
            int perWheel;
            const int *wheelStarts;
            if (partTex == 9) {
                perWheel = kartWheelCount_RIM_ORANGE;
                wheelStarts = kartWheelStart_RIM_ORANGE;
            } else if (partTex == 12) {
                perWheel = kartWheelCount_RIM_YELLOW;
                wheelStarts = kartWheelStart_RIM_YELLOW;
            } else {
                perWheel = kartWheelCount_TYRE;
                wheelStarts = kartWheelStart_TYRE;
            }
            float wheelOmega = clampf(K[who].spd * 2.5f, 0.0f, 120.0f);
            float roll = tAnim * wheelOmega;
            float steerAngle = K[who].steerVis * 0.42f;
            for (int w = 0; w < 4; ++w) {
                int begin = wheelStarts[w];
                if (begin < 0 || begin + perWheel > parts[i].count) continue;
                float cx = kartWheelCenterXYZ[w][0];
                float cy = kartWheelCenterXYZ[w][1];
                float cz = kartWheelCenterXYZ[w][2];
                ScePspFVector3 center = { cx, cy, cz };
                ScePspFVector3 negCenter = { -cx, -cy, -cz };
                float spin = roll * (w < 2 ? 1.0f : 1.04f);
                sceGumPushMatrix();
                sceGumTranslate(&center);
                sceGumRotateX(spin);
                if (w < 2) sceGumRotateZ(steerAngle);
                sceGumTranslate(&negCenter);
                sceGumDrawArray(GU_TRIANGLES, VF3, perWheel, 0, parts[i].v + begin);
                sceGumPopMatrix();
            }
        } else {
            sceGumDrawArray(GU_TRIANGLES, VF3, parts[i].count, 0, parts[i].v);
        }
    }
    sceGuDisable(GU_TEXTURE_2D);
}

static void drawCharacterModel(int who) {
    if (who < 0 || who >= NK || !shouldDrawDriver(who)) return;
    int cid = kChar[who];
    if (cid < 0 || cid >= NK) cid = 0;
    int count = 0;
    const CharModelPart *parts = charModelPartsFor(cid, &count);

    /* Karakter bazli olcek: Wario buyuk, Toad kucuk, digerleri dengeli.
       Hafif bob: hizla salinan oturma. */
    float scale = charScale[cid];
    float bob = 0.02f * sinf(K[who].bobA * 1.4f) * clampf(K[who].spd / VMAX, 0, 1);
    ScePspFVector3 tr = { -0.08f, charSeatY[cid] + bob, 0.02f };
    ScePspFVector3 sc = { scale, scale, scale };
    sceGumTranslate(&tr);
    /* Imported character meshes face toward source -Z; with game forward +X
       they must rotate -90 degrees around Y (the old +90 showed their backs). */
    sceGumRotateY(-PI * 0.5f);
    sceGumRotateX(charLeanX[cid]);
    /* Wario biraz daha genis, Toad dar */
    if (cid == 3) { sc.x *= 1.12f; sc.z *= 1.10f; }
    if (cid == 4) { sc.x *= 0.92f; sc.z *= 0.92f; }
    if (cid == 5) { sc.y *= 1.06f; } /* Peach biraz daha uzun */
    sceGumScale(&sc);

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexOffset(0.0f, 0.0f);

    for (int part = 0; part < count; part++) {
        if (parts[part].count <= 0) continue;
        const PspTexture *tx = charModelTexFor(cid, parts[part].tex);
        if (tx && tx->ready && tx->data) {
            sceGuEnable(GU_TEXTURE_2D);
            /* Character model UVs are normalized; convert them to the loaded texture dimensions. */
            sceGuTexScale((float)tx->w, (float)tx->h);
            sceGuTexImage(0, tx->w, tx->h, tx->w, tx->data);
            sceGuTexFlush();
        } else {
            /* Draw geometry in flat vertex color if a PNG fails to load. */
            sceGuDisable(GU_TEXTURE_2D);
        }
        sceGumDrawArray(GU_TRIANGLES, VF3, parts[part].count, 0, parts[part].v);
    }
    sceGuDisable(GU_TEXTURE_2D);
}


static void drawCharacterAccessories(int who) {
    if (who < 0 || who >= activeKarts()) return;
    int cid = kChar[who];
    ScePspFVector3 pos;
    sceGuDisable(GU_TEXTURE_2D);
    if (cid == 3) {
        /* Wario's face points along game +X. Nose and moustache are raised a
           little so they remain visible over the kart when racers bump. */
        pos.x = 0.48f; pos.y = 2.22f; pos.z = 0.00f;
        sceGumPushMatrix(); sceGumTranslate(&pos);
        sceGumDrawArray(GU_TRIANGLES, VF3, warioNoseCount, 0, &mesh[warioNoseStart]);
        sceGumPopMatrix();
        for (int side = -1; side <= 1; side += 2) {
            pos.x = 0.40f; pos.y = 2.08f; pos.z = side * 0.13f;
            sceGumPushMatrix(); sceGumTranslate(&pos);
            sceGumDrawArray(GU_TRIANGLES, VF3, warioMustacheCount, 0, &mesh[warioMustacheStart]);
            sceGumPopMatrix();
        }
        pos.x = 0.24f; pos.y = 2.76f; pos.z = 0.0f;
        sceGumPushMatrix(); sceGumTranslate(&pos);
        sceGumDrawArray(GU_TRIANGLES, VF3, warioBadgeCount, 0, &mesh[warioBadgeStart]);
        sceGumPopMatrix();
    } else if (cid == 4) {
        /* Toad's high mushroom cap and white spots. */
        pos.x = -0.02f; pos.y = charSeatY[cid] + charScale[cid] * 3.02f + 0.04f; pos.z = -0.12f;
        sceGumPushMatrix(); sceGumTranslate(&pos);
        sceGumDrawArray(GU_TRIANGLES, VF3, toadCapCount, 0, &mesh[toadCapStart]);
        sceGumPopMatrix();
        static const float spots[3][3] = {
            {0.10f, 0.22f, -0.20f}, {0.10f, 0.26f, 0.00f}, {0.10f, 0.22f, 0.20f}
        };
        for (int sp = 0; sp < 3; ++sp) {
            pos.x = -0.02f + spots[sp][0];
            pos.y = charSeatY[cid] + charScale[cid] * 3.02f + 0.04f + spots[sp][1];
            pos.z = -0.12f + spots[sp][2];
            sceGumPushMatrix(); sceGumTranslate(&pos);
            sceGumDrawArray(GU_TRIANGLES, VF3, toadSpotCount, 0, &mesh[toadSpotStart]);
            sceGumPopMatrix();
        }
    } else if (cid == 5) {
        /* Peach's crown and blonde side curls help distinguish her silhouette. */
        pos.x = -0.04f; pos.y = charSeatY[cid] + charScale[cid] * 3.02f + 0.04f; pos.z = -0.12f;
        sceGumPushMatrix(); sceGumTranslate(&pos);
        sceGumDrawArray(GU_TRIANGLES, VF3, peachCrownCount, 0, &mesh[peachCrownStart]);
        sceGumPopMatrix();
        for (int side = -1; side <= 1; side += 2) {
            pos.x = -0.04f; pos.y = charSeatY[cid] + 2.10f; pos.z = side * 0.31f;
            sceGumPushMatrix(); sceGumTranslate(&pos);
            sceGumDrawArray(GU_TRIANGLES, VF3, peachHairCount, 0, &mesh[peachHairStart]);
            sceGumPopMatrix();
        }
    }
    sceGuEnable(GU_TEXTURE_2D);
}

static void drawItemBoxFace3D(void) {
    if (!texItemBox || !texItemBox->ready) return;
    /* Question-block image on the front face of the rotating 3D pickup cube. */
    Vtx *v=(Vtx*)sceGuGetMemory(6*sizeof(Vtx));
    const unsigned c=RGBA(255,255,255,255);
    const float x=0.824f, a=-0.68f, b=0.68f;
    v[0]=(Vtx){0,0,c,x,a,a};
    v[1]=(Vtx){64,0,c,x,b,a};
    v[2]=(Vtx){64,64,c,x,b,b};
    v[3]=(Vtx){0,0,c,x,a,a};
    v[4]=(Vtx){64,64,c,x,b,b};
    v[5]=(Vtx){0,64,c,x,a,b};
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    /* These face vertices use pixel/texel coordinates (0..64). */
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexImage(0,texItemBox->w,texItemBox->h,texItemBox->w,texItemBox->data);
    sceGuTexFlush();
    sceGumDrawArray(GU_TRIANGLES,VF3,6,0,v);
    sceGuDisable(GU_TEXTURE_2D);
}

static void drawFace3D(int who, float tAnimNow) {
    /* Oyuncu kamerası arkadan baktığı için yüzü sürücünün arka tarafına
       yerleştiriyoruz. Böylece Mario/Luigi/Yoshi yarışta gerçekten görünür. */
    if (who < 0 || who >= NK) return;
    int cid = charModelId(kChar[who]);
    int fi = (cid == 0 || cid == 1) ? ((int)(tAnimNow * 2.5f) % 3) : 0;
    PspTexture *t = texFace[cid][fi];
    if (!t || !t->ready) return;
    Vtx *v = (Vtx*)sceGuGetMemory(6 * sizeof(Vtx));
    float x = -0.57f;
    float y0 = 1.25f, y1 = 2.00f;
    float z0 = -0.38f, z1 = 0.38f;
    unsigned c = RGBA(255,255,255,255);
    v[0]=(Vtx){0,0,c,x,y1,z0}; v[1]=(Vtx){(float)t->w,0,c,x,y1,z1}; v[2]=(Vtx){(float)t->w,(float)t->h,c,x,y0,z1};
    v[3]=(Vtx){0,0,c,x,y1,z0}; v[4]=(Vtx){(float)t->w,(float)t->h,c,x,y0,z1}; v[5]=(Vtx){0,(float)t->h,c,x,y0,z0};
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888,0,0,0); sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA); sceGuTexFilter(GU_NEAREST,GU_NEAREST); sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    /* Face quad UVs are written in source texel units. */
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f,0.0f);
    sceGuTexImage(0,t->w,t->h,t->w,t->data); sceGuTexFlush();
    sceGumDrawArray(GU_TRIANGLES,VF3,6,0,v);
    sceGuDisable(GU_TEXTURE_2D);
}

static void drawPlayerPortraitHUD(void) {
    int cid = kChar[0];
    if (cid < 0 || cid >= NK) cid = 0;
    int fi = ((int)(tAnim * 2.5f) % 3);
    PspTexture *t = texFace[cid][fi];
    if (!t || !t->ready) t = texFace[cid][0];
    if (!t || !t->ready) return;
    /* karakter rengi cerceve */
    rect(2, 28, 56, 56, kBody[cid]);
    rect(4, 30, 52, 52, RGBA(0,0,0,170));
    drawTexRect(t, 6, 32, 48, 48, 0, 0, (float)t->w, (float)t->h, RGBA(255,255,255,255));
    text(64, 32, 8, 14, 2, charNames[cid], RGB(255,220,70));
    text(64, 48, 6, 10, 2, charClass[cid], RGB(180, 200, 220));
}

/* ---------- Kayit sistemi (v2) ---------- */
#define SAVE_DIR   "ms0:/PSP/SAVEDATA/MKPSP"
#define SAVE_FILE  "ms0:/PSP/SAVEDATA/MKPSP/save3.dat"
#define SAVE_MAGIC 0x33504B4Du   /* 'MKP3' */

typedef struct {
    unsigned magic;
    float bestLap[NTRACKS];
    float bestRace[NTRACKS];
    float bestTT[NTRACKS];
    int totalWins;
    int lastChar, lastTrack;
    unsigned unlockedChars;
    unsigned unlockedTracks;
    unsigned unlockedCups;
    int careerLevel;
    int cupsGold, cupsSilver, cupsBronze;
    int totalRaces, totalItems, totalHits, totalBoosts;
} SaveData;

static SaveData saveData;

static void saveDefaults(void) {
    memset(&saveData, 0, sizeof(saveData));
    saveData.magic = SAVE_MAGIC;
    saveData.unlockedChars = 0x0000003Fu; /* 6 karakterin hepsi secilebilir */
    saveData.unlockedTracks = 0x000000FFu; /* ilk 8 pist (Mushroom+Flower) */
    saveData.unlockedCups = 0x00000001u; /* ilk kupa */
    saveData.careerLevel = 0;
}
static int charUnlocked(int id) { return id >= 0 && id < NK && (saveData.unlockedChars & (1u << id)); }
static int trackUnlocked(int id) { return id >= 0 && id < NTRACKS && (saveData.unlockedTracks & (1u << id)); }
static int cupUnlocked(int id) { return id >= 0 && id < NCUPS && (saveData.unlockedCups & (1u << id)); }
static int countUnlocked(unsigned bits, int maxn) { int n=0; for(int i=0;i<maxn;i++) if(bits&(1u<<i)) n++; return n; }

/* ---------- Ghost (pist basina ayri dosya, 30 Hz kayit, en fazla 300 sn, checksum) ---------- */
#define GHOST_MAX   9000
#define GHOST_MAGIC 0x31485347u  /* 'GSH1' */
typedef struct { float x, z, h; } GhostPoint;
typedef struct { unsigned magic; int version, track, charId, count; float time; unsigned checksum; } GhostHeader;

static GhostPoint ghostRec[GHOST_MAX];    /* su an kaydedilen tur */
static GhostPoint ghostBest[GHOST_MAX];   /* en iyi tur (oynatilan) */
static int ghostCount = 0, ghostBestCount = 0, ghostMode = 0, ghostTick = 0;
static float ghostBestTime = 0;

static float lapTimer = 0, raceBestLap = 0;
static int newRecord = 0;

static unsigned ghostSum(const GhostPoint *g, int n) {
    const unsigned char *q = (const unsigned char *)g;
    unsigned h = 2166136261u;
    for (size_t i = 0; i < (size_t)n * sizeof(GhostPoint); i++) { h ^= q[i]; h *= 16777619u; }
    return h;
}
static void ghostPath(char *out, int n, int track) { snprintf(out, n, SAVE_DIR "/ghost_t%d.dat", track); }

static void saveGhost(int track, float time, int charId) {
    char path[96];
    ghostPath(path, sizeof(path), track);
    GhostHeader hd;
    hd.magic = GHOST_MAGIC; hd.version = 1; hd.track = track; hd.charId = charId;
    hd.count = ghostBestCount; hd.time = time; hd.checksum = ghostSum(ghostBest, ghostBestCount);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &hd, sizeof(hd));
        sceIoWrite(fd, ghostBest, ghostBestCount * sizeof(GhostPoint));
        sceIoClose(fd);
    }
}

static void loadGhost(int track) {
    ghostBestCount = 0; ghostBestChar = 0; ghostBestTime = 0;
    char path[96];
    ghostPath(path, sizeof(path), track);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) return;
    GhostHeader hd;
    if (sceIoRead(fd, &hd, sizeof(hd)) == (int)sizeof(hd) &&
        hd.magic == GHOST_MAGIC && hd.version == 1 && hd.track == track &&
        hd.count > 0 && hd.count <= GHOST_MAX && hd.charId >= 0 && hd.charId < NK) {
        int bytes = hd.count * (int)sizeof(GhostPoint);
        if (sceIoRead(fd, ghostBest, bytes) == bytes && ghostSum(ghostBest, hd.count) == hd.checksum) {
            ghostBestCount = hd.count; ghostBestChar = hd.charId; ghostBestTime = hd.time;
        }
    }
    sceIoClose(fd);
}

static void saveGame(void) {
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    saveData.magic = SAVE_MAGIC;
    SceUID fd = sceIoOpen(SAVE_FILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &saveData, sizeof(saveData));
        sceIoClose(fd);
    }
}

static void loadSave(void) {
    memset(&saveData, 0, sizeof(saveData));
    SceUID fd = sceIoOpen(SAVE_FILE, PSP_O_RDONLY, 0777);
    if (fd >= 0) {
        sceIoRead(fd, &saveData, sizeof(saveData));
        sceIoClose(fd);
    }
    if (saveData.magic != SAVE_MAGIC) saveDefaults();
    saveData.magic = SAVE_MAGIC;
    /* En az ilk 3 karakter her zaman acik; bu build'de hepsi secilebilir */
    saveData.unlockedChars |= 0x0000003Fu;
    if (saveData.unlockedTracks == 0) saveData.unlockedTracks = 0x000000FFu; /* Mushroom+Flower */
    saveData.unlockedTracks |= 0x0000000Fu; /* en az Mushroom Cup */
    if (saveData.unlockedCups == 0) saveData.unlockedCups = 0x00000001u;
    if (saveData.careerLevel < 0) saveData.careerLevel = 0;
    for (int i = 0; i < NTRACKS; i++) {
        if (!(saveData.bestLap[i] >= 0)) saveData.bestLap[i] = 0;    /* bozuk veri korumasi */
        if (!(saveData.bestRace[i] >= 0)) saveData.bestRace[i] = 0;
        if (!(saveData.bestTT[i] >= 0)) saveData.bestTT[i] = 0;
    }
    if (saveData.totalWins < 0) saveData.totalWins = 0;
    if (saveData.lastChar < 0 || saveData.lastChar >= NK) saveData.lastChar = 0;
    if (saveData.lastTrack < 0 || saveData.lastTrack >= NTRACKS) saveData.lastTrack = 0;
}

/* tur bitince en iyi tur kontrolu */
static void checkBestLap(void) {
    if (lapTimer <= 0) return;
    float *bl = &saveData.bestLap[trackSel];
    if (*bl == 0 || lapTimer < *bl) *bl = lapTimer;
    if (raceBestLap == 0 || lapTimer < raceBestLap) raceBestLap = lapTimer;
}

/* yaris bitince: istatistik + kayit */
static void finishRace(float raceT, int rank) {
    checkBestLap();
    newRecord = 0;
    int t = trackSel;
    saveData.totalRaces++;
    if (gameMode == GAME_TIME_TRIAL) {
        if (saveData.bestTT[t] == 0 || raceT < saveData.bestTT[t]) {
            saveData.bestTT[t] = raceT;
            newRecord = 1;
            if (ghostCount > 0) {
                memcpy(ghostBest, ghostRec, ghostCount * sizeof(GhostPoint));
                ghostBestCount = ghostCount;
                ghostBestChar = charSel;
                ghostBestTime = raceT;
                saveGhost(t, raceT, charSel);
            }
        }
    } else {
        if (saveData.bestRace[t] == 0 || raceT < saveData.bestRace[t]) saveData.bestRace[t] = raceT;
        if (gameMode == GAME_RACE && rank == 1) saveData.totalWins++;
    }
    saveData.lastChar = charSel; saveData.lastTrack = trackSel;
    saveGame();
}

static int racePointsForRank(int r) {
    static const int pts[NK] = {10, 8, 6, 4, 2, 1};
    if (r < 1) r = 1;
    if (r > NK) r = NK;
    return pts[r - 1];
}

static int rankForKart(int who) {
    int r = 1;
    for (int i = 0; i < NK; i++) if (i != who && K[i].prog > K[who].prog) r++;
    return r;
}

static int calcChampOverallRank(int who) {
    int r = 1;
    for (int i = 0; i < NK; i++) if (i != who && champPoints[i] > champPoints[who]) r++;
    return r;
}

static void applyChampionshipRacePoints(void) {
    for (int i = 0; i < NK; i++) champPoints[i] += racePointsForRank(rankForKart(i));
    champOverallRank = calcChampOverallRank(0);
}

static void awardChampionship(void) {
    champOverallRank = calcChampOverallRank(0);
    champMedal = (champOverallRank == 1) ? 3 : ((champOverallRank <= 3) ? 2 : 1);
    if (champMedal == 3) saveData.cupsGold++;
    else if (champMedal == 2) saveData.cupsSilver++;
    else saveData.cupsBronze++;
    if (gameMode == GAME_CAREER) {
        careerActive = 1;
        if (champOverallRank <= 3) careerLevel++;
        saveData.careerLevel = careerLevel;
        /* Bronze: yeni pist; Silver: pist + karakter; Gold: pist + karakter + kupa */
        int nextTrack = 0;
        while (nextTrack < NTRACKS && trackUnlocked(nextTrack)) nextTrack++;
        if (champMedal >= 1 && nextTrack < NTRACKS) saveData.unlockedTracks |= (1u << nextTrack);
        int nextChar = 0;
        while (nextChar < NK && charUnlocked(nextChar)) nextChar++;
        if (champMedal >= 2 && nextChar < NK) saveData.unlockedChars |= (1u << nextChar);
        if (champMedal == 3) {
            int nextCup = 0;
            while (nextCup < NCUPS && cupUnlocked(nextCup)) nextCup++;
            if (nextCup < NCUPS) saveData.unlockedCups |= (1u << nextCup);
        }
    }
    if (champOverallRank == 1) saveData.totalWins++;
    saveData.lastChar = charSel; saveData.lastTrack = trackSel;
    saveGame();
}

/* ---------- Ozel gucler ---------- */
#define ITEM_NONE        0
#define ITEM_MUSH        1
#define ITEM_GREENSHELL  2
#define ITEM_REDSHELL    3
#define ITEM_BANANA      4
#define ITEM_LIGHT       5
#define ITEM_SHIELD      6
#define ITEM_STAR        7

#define SHELL_GREEN 0
#define SHELL_RED   1

typedef struct {
    float x, z;
    float vx, vz;
    float speed;
    float life;
    int active;
    int target;
    int type;
    int owner;
} ItemShot;

static ItemShot shellShots[NK];
static int itemCntG = 0;
static float itemRouletteT = 0.0f;   /* >0 ise item kutusu ruleti donuyor */
static int itemRouletteResult = ITEM_NONE;

static const char *itemName(int item) {
    switch (item) {
    case ITEM_MUSH:   return "MUSHROOM";
    case ITEM_GREENSHELL: return "G.SHELL";
    case ITEM_REDSHELL:   return "R.SHELL";
    case ITEM_BANANA: return "BANANA";
    case ITEM_LIGHT:  return "LIGHTNING";
    case ITEM_SHIELD: return "BUBBLE";
    case ITEM_STAR:   return "STAR";
    }
    return "???";
}

static unsigned itemColor(int item) {
    switch (item) {
    case ITEM_MUSH:   return RGB(235, 50, 50);
    case ITEM_GREENSHELL: return RGB(50, 220, 80);
    case ITEM_REDSHELL:   return RGB(240, 60, 60);
    case ITEM_BANANA: return RGB(255, 210, 40);
    case ITEM_LIGHT:  return RGB(255, 245, 70);
    case ITEM_SHIELD: return RGB(80, 230, 255);
    case ITEM_STAR:   return RGB(255, 150, 40);
    }
    return RGB(180, 180, 180);
}

static int calcRank(void);
static int kartRank(int who) {
    int r = 1;
    for (int i = 0; i < NK; i++) if (i != who && K[i].prog > K[who].prog) r++;
    return r;
}
static void initRace(void);
static int addBanana(float x, float z);

/* kabuk cikarsa yesil veya kirmizi */
static int randShell(void) { return rnd() < 0.5f ? ITEM_GREENSHELL : ITEM_REDSHELL; }

static int randomItem(void) {
    int rank = calcRank();
    int r = (int)(rnd() * 100.0f);
    if (rank >= 4) {
        if (r < 25) return ITEM_STAR;
        if (r < 45) return ITEM_LIGHT;
        if (r < 65) return randShell();
        if (r < 80) return ITEM_MUSH;
        if (r < 90) return ITEM_SHIELD;
        return ITEM_BANANA;
    }
    if (r < 30) return ITEM_MUSH;
    if (r < 50) return ITEM_BANANA;
    if (r < 70) return randShell();
    if (r < 82) return ITEM_SHIELD;
    if (r < 93) return ITEM_LIGHT;
    return ITEM_STAR;
}

static void giveItem(Kart *p) {
    int it = randomItem();
    if (p == &K[0]) {
        /* Mario Kart tarzi item ruleti: once doner, sonra sonucu kilitler */
        itemRouletteResult = it;
        itemRouletteT = 1.35f;
        itemCntG = 0;
        p->item = ITEM_NONE;
        playMenuSelect();
    } else {
        p->item = it;
    }
}

static int botRandomItem(int who) {
    int rank = kartRank(who);
    int r = (int)(rnd() * 100.0f);
    if (rank >= 4) {
        if (r < 24) return ITEM_STAR;
        if (r < 44) return ITEM_LIGHT;
        if (r < 68) return randShell();
        if (r < 82) return ITEM_MUSH;
        if (r < 92) return ITEM_BANANA;
        return ITEM_SHIELD;
    }
    if (r < 28) return ITEM_MUSH;
    if (r < 50) return ITEM_BANANA;
    if (r < 73) return randShell();
    if (r < 86) return ITEM_SHIELD;
    if (r < 95) return ITEM_LIGHT;
    return ITEM_STAR;
}

static void botGiveItem(int who) {
    if (who <= 0 || K[who].item != ITEM_NONE) return;
    K[who].item = botRandomItem(who);
}

static int botTargetAhead(int who) {
    int best = -1; float bestD = 1e30f;
    for (int i = 0; i < NK; i++) if (i != who) {
        float d = K[i].prog - K[who].prog;
        if (d > 0 && d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) {
        for (int i = 0; i < NK; i++) if (i != who) {
            float d = fabsf(K[i].prog - K[who].prog);
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    return best;
}

static int botTargetBehind(int who) {
    int best = -1; float bestD = 1e30f;
    for (int i = 0; i < NK; i++) if (i != who) {
        float d = K[who].prog - K[i].prog;
        if (d > 0 && d < bestD) { bestD = d; best = i; }
    }
    return best;
}

static void fireGreenShell(int owner, float x, float z, float ang) {
    for (int i = 0; i < NK; i++) {
        if (!shellShots[i].active) {
            ItemShot *sh = &shellShots[i];
            sh->active = 1;
            sh->type = SHELL_GREEN;
            sh->owner = owner;
            sh->target = -1;
            sh->x = x;
            sh->z = z;
            sh->vx = cosf(ang) * 80.0f;
            sh->vz = sinf(ang) * 80.0f;
            sh->speed = 80.0f;
            sh->life = 8.0f;
            triggerSfx(SFX_SHELL);
            break;
        }
    }
}

static void fireRedShell(int owner, int target) {
    if (target < 0 || target >= NK || owner == target) return;
    for (int i = 0; i < NK; i++) {
        if (!shellShots[i].active) {
            ItemShot *sh = &shellShots[i];
            sh->active = 1;
            sh->type = SHELL_RED;
            sh->owner = owner;
            sh->target = target;
            sh->x = K[owner].x + cosf(K[owner].h) * 2.5f;
            sh->z = K[owner].z + sinf(K[owner].h) * 2.5f;
            sh->vx = sh->vz = 0;
            sh->speed = 72.0f;
            sh->life = 10.0f;
            triggerSfx(SFX_SHELL);
            break;
        }
    }
}

static void botUseItem(int who) {
    if (who <= 0 || state != 1) return;
    Kart *b = &K[who];
    int item = b->item;
    if (item == ITEM_NONE) return;
    int ahead = botTargetAhead(who);
    int behind = botTargetBehind(who);

    if (item == ITEM_MUSH) {
        if (b->spd < VMAX * 0.82f || b->prog < K[0].prog - 40.0f) {
            b->boostT = 2.0f; b->spd += VMAX * 0.16f; b->item = ITEM_NONE;
        }
    } else if (item == ITEM_GREENSHELL) {
        if (ahead >= 0 && fabsf(K[ahead].prog - b->prog) < 420.0f) {
            fireGreenShell(who, b->x + cosf(b->h) * 2.5f, b->z + sinf(b->h) * 2.5f, b->h);
            b->item = ITEM_NONE;
        }
    } else if (item == ITEM_REDSHELL) {
        if (ahead >= 0) {
            fireRedShell(who, ahead);
            b->item = ITEM_NONE;
        }
    } else if (item == ITEM_BANANA) {
        if (behind >= 0 && K[who].prog - K[behind].prog < 120.0f) {
            addBanana(b->x - cosf(b->h) * 2.0f, b->z - sinf(b->h) * 2.0f); b->item = ITEM_NONE;
        }
    } else if (item == ITEM_LIGHT) {
        if (ahead >= 0 || behind >= 0) {
            for (int i = 0; i < NK; i++) if (i != who && K[i].starT <= 0) {
                if (K[i].shield) K[i].shield = 0;
                else { K[i].spd *= 0.45f; K[i].slowT = 2.8f; K[i].spinT = 0.45f; }
            }
            b->item = ITEM_NONE; shake = 0.45f;
        }
    } else if (item == ITEM_SHIELD) {
        if (behind >= 0 || b->prog < K[0].prog + 100.0f) { b->shield = 1; b->item = ITEM_NONE; }
    } else if (item == ITEM_STAR) {
        if (b->prog < K[0].prog + 180.0f || b->spd < VMAX * 0.7f) {
            b->starT = 5.0f; b->boostT = 5.0f; b->spd += VMAX * 0.18f; b->item = ITEM_NONE;
        }
    }
}

static void hitKart(Kart *k, float slow, float spin) {
    int who = (int)(k - K);
    if (k->starT > 0 || k->shield) {
        if (k->shield) {
            k->shield = 0;
            spawnKartFx(who, 2, 8);
        }
        return;
    }
    spawnKartFx(who, 2, 10);
    saveData.totalHits++; triggerSfx(SFX_COLLISION);
    k->spd *= slow;
    k->boostT = 0;
    k->spinT = spin;
    k->driftDir = 0;
    k->slowT = 0.0f;
}

static int addBanana(float x, float z) {
    for (int i = 0; i < MAXTHING; i++) {
        if (things[i].active == 0 && things[i].type == TH_BANANA) {
            things[i].x = x; things[i].z = z;
            things[i].type = TH_BANANA; things[i].active = 1; things[i].resp = 0.6f;
            if (i >= nthings) nthings = i + 1;
            return 1;
        }
    }
    if (nthings >= MAXTHING) return 0;
    things[nthings].x = x; things[nthings].z = z;
    things[nthings].type = TH_BANANA; things[nthings].active = 1; things[nthings].resp = 0.6f;
    nthings++;
    return 1;
}

static void useItem(void) {
    Kart *p = &K[0];
    int item = p->item;
    if (itemRouletteT > 0) return; /* rulet bitmeden kullanilamaz */
    if (item == ITEM_NONE || itemCntG <= 0 || state != 1) return;

    itemCntG = 0;
    p->item = ITEM_NONE;

    saveData.totalItems++; triggerSfx(SFX_ITEM);
    if (item == ITEM_MUSH) {
        p->boostT = 2.0f; saveData.totalBoosts++; triggerSfx(SFX_BOOST);
        p->spd += VMAX * 0.18f;
        screenFlash = 0.30f;
        spawnKartFx(0, 0, 5);
    } else if (item == ITEM_GREENSHELL) {
        fireGreenShell(0, p->x + cosf(p->h) * 2.5f, p->z + sinf(p->h) * 2.5f, p->h);
    } else if (item == ITEM_REDSHELL) {
        int best = botTargetAhead(0);
        if (best >= 0) fireRedShell(0, best);
    } else if (item == ITEM_BANANA) {
        addBanana(p->x - cosf(p->h) * 2.0f, p->z - sinf(p->h) * 2.0f);
    } else if (item == ITEM_LIGHT) {
        for (int i = 1; i < NK; i++) {
            if (K[i].starT <= 0 && K[i].shield) {
                K[i].shield = 0;
            } else if (K[i].starT <= 0) {
                K[i].spd *= 0.45f;
                K[i].slowT = 2.8f;
                K[i].spinT = 0.45f;
            }
        }
        shake = 0.65f;
        screenFlash = 0.55f;
    } else if (item == ITEM_SHIELD) {
        p->shield = 1;
    } else if (item == ITEM_STAR) {
        p->starT = 5.0f; saveData.totalBoosts++; triggerSfx(SFX_BOOST);
        p->boostT = 5.0f;
        p->spd += VMAX * 0.22f;
        screenFlash = 0.45f;
        spawnKartFx(0, 0, 8);
    }
}

static void updateShell(void) {
    for (int n = 0; n < NK; n++) {
        ItemShot *shot = &shellShots[n];
        if (!shot->active) continue;
        shot->life -= DT;
        if (shot->life <= 0) { shot->active = 0; continue; }

        if (shot->type == SHELL_GREEN) {
            shot->x += shot->vx * DT;
            shot->z += shot->vz * DT;
            if (shot->x < mapMinX || shot->x > mapMaxX) {
                shot->vx = -shot->vx;
                shot->x = clampf(shot->x, mapMinX, mapMaxX);
            }
            if (shot->z < mapMinZ || shot->z > mapMaxZ) {
                shot->vz = -shot->vz;
                shot->z = clampf(shot->z, mapMinZ, mapMaxZ);
            }
            for (int i = 0; i < activeKarts(); i++) {
                if (i == shot->owner && shot->life > 7.5f) continue;   /* atar atmaz kendine carpmasin */
                float dx = K[i].x - shot->x, dz = K[i].z - shot->z;
                if (dx * dx + dz * dz < 2.2f * 2.2f) {
                    hitKart(&K[i], 0.35f, 1.2f);
                    if (i == 0) shake = 0.35f;
                    shot->active = 0;
                    break;
                }
            }
        } else {
            if (shot->target < 0 || shot->target >= NK || shot->target == shot->owner) { shot->active = 0; continue; }
            Kart *t = &K[shot->target];
            float dx = t->x - shot->x, dz = t->z - shot->z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d < 2.2f) {
                hitKart(t, 0.35f, 1.2f);
                shot->active = 0;
                if (shot->target == 0) shake = 0.35f;
                continue;
            }
            if (d < 0.001f) d = 0.001f;
            shot->x += dx / d * shot->speed * DT;
            shot->z += dz / d * shot->speed * DT;
        }
    }
}

static void updateItemTimers(void) {
    for (int i = 0; i < NK; i++) {
        if (K[i].starT > 0) K[i].starT -= DT;
        if (K[i].slowT > 0) K[i].slowT -= DT;
    }
}

static void buildThings(void) {
    nthings = 0;
    for (int q = 0; q < 9; q++) {
        int s = (M * q / 9 + 10) % M;
        for (int j = -1; j <= 1; j++) {
            float lat = j * HW * 0.42f;
            things[nthings].x = tcx[s] - tfz[s] * lat; things[nthings].z = tcz[s] + tfx[s] * lat;
            things[nthings].type = TH_ITEM; things[nthings].active = 1; things[nthings].resp = 0; nthings++;
        }
    }
    for (int i = 0; i < 20; i++) {
        int s = (M * i / 20 + 17) % M;
        float lat = (((i * 37) % 9) - 4) * 0.2f * HW * 0.75f;
        things[nthings].x = tcx[s] - tfz[s] * lat; things[nthings].z = tcz[s] + tfx[s] * lat;
        things[nthings].type = TH_CONE; things[nthings].active = 1; things[nthings].resp = 0; nthings++;
    }
}

static void trackUpdate(Kart *k) {
    int best = k->idx; float bd = 1e18f;
    for (int j = -8; j <= 12; j++) {
        int i = (k->idx + j + M * 4) % M;
        float dx = k->x - tcx[i], dz = k->z - tcz[i];
        float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    int old = k->idx;
    if (best < old - M / 2) k->lapc++;
    else if (best > old + M / 2) k->lapc--;
    k->idx = best;
    float dx = k->x - tcx[best], dz = k->z - tcz[best];
    k->lat = dx * (-tfz[best]) + dz * tfx[best];
    float along = dx * tfx[best] + dz * tfz[best];
    k->prog = k->lapc * TL + tsd[best] + along;
    /* gorunmez duvar */
    float lim = HW + 12.0f;
    if (fabsf(k->lat) > lim) {
        float sgn = k->lat > 0 ? 1.0f : -1.0f, over = fabsf(k->lat) - lim;
        k->x -= (-tfz[best]) * over * sgn; k->z -= tfx[best] * over * sgn;
        k->lat = sgn * lim; k->spd *= 0.96f;
    }
}

static void stepKart(Kart *k, float steer, int gas, int brake, int driftBtn) {
    float oldSpd = k->spd;
    const int ki = (int)(k - K);
    const float spdM = cSpd[kChar[ki]], accM = cAcc[kChar[ki]], trnM = cTrn[kChar[ki]];
    if (k->spinT > 0) { k->spinT -= DT; k->spinA += 14.0f * DT; steer = 0; gas = 0; brake = 0; driftBtn = 0; if (k->spinT <= 0) k->spinA = 0; }
    int boosting = k->boostT > 0;
    if (boosting) k->boostT -= DT;
    int off = fabsf(k->lat) > HW + 1.8f;
    float slowMul = (k->slowT > 0 ? 0.48f : 1.0f) * spdM;
    float lim = (boosting ? VMAX * (off ? 0.8f : 1.35f) : (off ? VMAX * 0.45f : VMAX)) * slowMul;
    if (boosting) k->spd += 90.0f * DT;
    else if (brake) k->spd -= 60.0f * DT;
    else if (gas) k->spd += 30.0f * accM * (1.0f - 0.55f * k->spd / VMAX) * DT;
    else k->spd -= 10.0f * DT;
    if (k->spd > lim) k->spd -= (off ? 80.0f : 40.0f) * DT;
    if (k->spd < 0) k->spd = 0;

    if (driftBtn && !k->driftDir && k->spd > VMAX * 0.45f && fabsf(steer) > 0.3f) { k->driftDir = steer > 0 ? 1 : -1; k->driftT = 0; }
    if (k->driftDir) {
        if (!driftBtn || k->spd < VMAX * 0.3f) {
            if (k->driftT > 1.8f) {
                k->boostT = 1.5f; saveData.totalBoosts++; triggerSfx(SFX_BOOST);
                spawnKartFx((int)(k - K), 0, 6);
                if (k == &K[0]) { screenFlash = 0.35f; }
            } else if (k->driftT > 0.8f) {
                k->boostT = 0.9f; saveData.totalBoosts++; triggerSfx(SFX_DRIFT);
                spawnKartFx((int)(k - K), 0, 4);
                if (k == &K[0]) { screenFlash = 0.20f; }
            }
            k->driftDir = 0; k->driftT = 0;
        } else k->driftT += DT;
    }
    float sp = k->spd;
    float rate = 2.0f * trnM * clampf(sp / 14.0f, 0, 1) * (1.0f - 0.25f * sp / VMAX);
    float turn = k->driftDir ? (k->driftDir * 0.85f + steer * 0.75f) * rate * 1.15f : steer * rate;
    k->h = wrapA(k->h + turn * DT);
    float grip = k->driftDir ? 2.4f : 14.0f;
    k->vh = wrapA(k->vh + wrapA(k->h - k->vh) * clampf(grip * DT, 0, 1));
    k->x += cosf(k->vh) * k->spd * DT;
    k->z += sinf(k->vh) * k->spd * DT;
    k->steerVis += (steer - k->steerVis) * 0.2f;

    /* Hareket animasyonlari: suspansiyon, govde yatmasi, ivmelenme (daha belirgin) */
    k->bobA += DT * (3.5f + k->spd * 0.15f);
    k->accelVis += ((k->spd - oldSpd) - k->accelVis) * 0.25f;
    {
        float targetLean = -k->steerVis * 0.22f - (k->driftDir ? k->driftDir * 0.14f : 0.0f);
        float targetPitch = clampf(-k->accelVis * 0.028f, -0.14f, 0.14f);
        k->lean += (targetLean - k->lean) * 0.22f;
        k->pitch += (targetPitch - k->pitch) * 0.22f;
    }

    /* Boost ve drift sirasinda daha yogun parcacik */
    k->fxTimer -= DT;
    if (k->boostT > 0 && k->fxTimer <= 0) {
        spawnKartFx((int)(k - K), 0, 3);
        k->fxTimer = 0.045f;
    } else if (k->driftDir && k->spd > VMAX * 0.45f && k->fxTimer <= 0) {
        spawnKartFx((int)(k - K), 1, 2);
        k->fxTimer = 0.07f;
    }
}

static int botThreatened(int who) {
    for(int i=0;i<NK;i++) if(i!=who && shellShots[i].active && shellShots[i].target==who) return 1;
    for(int i=0;i<nthings;i++) if(things[i].active && things[i].type==TH_BANANA) {
        float dx=things[i].x-K[who].x, dz=things[i].z-K[who].z;
        if(dx*dx+dz*dz < 42.0f) return 1;
    }
    return 0;
}
static int botTargetLeader(int who) {
    int best=-1; float bp=-1e30f;
    for(int i=0;i<NK;i++) if(i!=who && K[i].prog>bp){ bp=K[i].prog; best=i; }
    return best;
}

static void aiDrive(Kart *k, int n) {
    int la = (k->idx + 6 + (int)(k->spd / 8.0f)) % M;
    float lane = (k->lane + 0.30f * sinf(k->prog * 0.012f + n)) * HW * 0.62f;
    /* obstacle dodge: banana/cone ahead */
    for(int i=0;i<nthings;i++) if(things[i].active && things[i].type==TH_BANANA) {
        float ddx=things[i].x-k->x, ddz=things[i].z-k->z;
        float along=ddx*tfx[k->idx]+ddz*tfz[k->idx];
        float side=ddx*(-tfz[k->idx])+ddz*tfx[k->idx];
        if(along>0 && along<26.0f && fabsf(side)<3.0f) lane += (side>=0?-1.0f:1.0f)*HW*0.34f;
    }
    float tx = tcx[la] - tfz[la] * lane, tz = tcz[la] + tfx[la] * lane;
    float want = atan2f(tz-k->z,tx-k->x);
    float steer = clampf(wrapA(want-k->h)*2.6f,-1,1);
    int j=(k->idx+14)%M;
    float turn=fabsf(wrapA(tth[j]-tth[k->idx]));
    float target=VMAX*k->skill*(1.0f-clampf(turn*0.9f,0,0.40f));
    float pd=k->prog-K[0].prog;
    if(pd>300) target*=0.92f;
    else if(pd<-300) target*=1.10f;
    /* defense: use shield/star when a shell is targeting us */
    if(k->item!=ITEM_NONE && botThreatened(n)) {
        if(k->item==ITEM_SHIELD || k->item==ITEM_STAR) botUseItem(n);
    }
    /* item boxes */
    for(int i=0;i<nthings;i++) {
        Thing*t=&things[i]; if(!t->active||t->type!=TH_ITEM||k->item!=ITEM_NONE) continue;
        float dx=t->x-k->x,dz=t->z-k->z;
        if(dx*dx+dz*dz<5.5f*5.5f){ t->active=0; t->resp=8.0f; botGiveItem(n); break; }
    }
    /* attack analysis */
    if(k->item!=ITEM_NONE) {
        int leader=botTargetLeader(n);
        int ahead=botTargetAhead(n);
        if(k->item==ITEM_REDSHELL && leader>=0 && leader!=n) {
            k->item=ITEM_REDSHELL; fireRedShell(n,leader); k->item=ITEM_NONE;
        } else if(k->item==ITEM_GREENSHELL && ahead>=0 && fabsf(K[ahead].prog-k->prog)<360) {
            fireGreenShell(n,k->x+cosf(k->h)*2.5f,k->z+sinf(k->h)*2.5f,k->h); k->item=ITEM_NONE;
        } else if(k->item==ITEM_LIGHT && (pd<-120 || leader>=0)) botUseItem(n);
        else if(k->item==ITEM_BANANA && botTargetBehind(n)>=0 && K[n].prog-K[botTargetBehind(n)].prog<110) botUseItem(n);
        else if((rnd()<0.09f) || k->item==ITEM_STAR || k->item==ITEM_SHIELD) botUseItem(n);
    }
    int drift=(fabsf(turn)>0.28f && k->spd>VMAX*0.52f && ((int)(tAnim*12.0f)+n)%3==0);
    stepKart(k,steer,k->spd<target,k->spd>target+5.5f,drift);
}

static int calcRank(void) {
    int r = 1;
    for (int i = 1; i < NK; i++) if (K[i].prog > K[0].prog) r++;
    return r;
}

static void initRace(void) {
    memset(kartBumpCooldown, 0, sizeof(kartBumpCooldown));
    state = 0; lap = 1; cd = 4.0f; raceTime = 0; finalRank = 0; finalTime = 0; shake = 0; lapFlash = 0;
    playFileSfx(FSFX_RACE_START);
    lapTimer = 0; raceBestLap = 0; newRecord = 0; ghostCount = 0; ghostTick = 0;
    ghostMode = (gameMode == GAME_TIME_TRIAL && ghostBestCount > 0);
    itemCntG = 0; itemRouletteT = 0; itemRouletteResult = ITEM_NONE;
    fxHead = 0; memset(fx, 0, sizeof(fx));
    for (int i = 0; i < NK; i++) shellShots[i].active = 0;
    static const float skills[NK] = { 0.85f, 0.95f, 0.92f, 0.90f, 0.88f, 0.86f };
    for (int i = 0; i < NK; i++) {
        int ii; float lat;
        if (i == 0) { ii = M - 9; lat = 0; }
        else { int r = (i - 1) / 2; ii = M - 3 - r * 2; lat = ((i - 1) % 2 ? 0.45f : -0.45f) * HW; }
        Kart *k = &K[i];
        memset(k, 0, sizeof(Kart));
        k->x = tcx[ii] - tfz[ii] * lat; k->z = tcz[ii] + tfx[ii] * lat;
        k->h = tth[ii]; k->vh = k->h; k->idx = ii; k->lapc = -1;
        k->skill = skills[i] * diffBotMul[diffSel];
        if(k->skill>1.15f) k->skill=1.15f;
        k->lane = -0.8f + (i - 1) * 0.4f;
        trackUpdate(k);
    }
    /* Time trial: botlar hareketsiz ve gizli */
    if (gameMode == GAME_TIME_TRIAL) for (int i = 1; i < NK; i++) K[i].spd = 0;
    for (int i = 0; i < nthings; i++) { things[i].active = 1; things[i].resp = 0; }
    camH = K[0].h; fovCur = 62.0f;
    cameraMode = 0;
}

static void assignChars(void) {
    kChar[0] = charSel;
    int n = 1;
    for (int c = 0; c < NK; c++) if (c != charSel) kChar[n++] = c;
}

/* karakter / pist degisince: yolu, meshi ve ghost'u yeniden kur */
static void rebuildAll(void) {
    assignChars();
    loadGhost(trackSel);
    rngState = 12345u + (unsigned)trackSel * 7777u;
    buildTrackPath();
    buildMesh();
    buildThings();
    initRace();
}

static float readSteer(const SceCtrlData *pad) {
    /* PSP analog stick: dead-zone + normalized response. */
    int lx = (int)pad->Lx - 128;
    float steer = 0.0f;
    const int dead = 18;
    const int full = 110;

    if (pad->Buttons & PSP_CTRL_LEFT) steer -= 1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) steer += 1.0f;

    if (abs(lx) > dead) {
        float a = (float)(abs(lx) - dead) / (float)(full - dead);
        if (a > 1.0f) a = 1.0f;
        float analog = (lx < 0) ? -a : a;
        /* Analog is the fine steering source; D-pad remains a digital fallback. */
        if (steer == 0.0f) steer = analog;
        else steer = clampf(steer + analog * 0.25f, -1.0f, 1.0f);
    }
    return clampf(steer, -1.0f, 1.0f);
}

static int nextUnlockedChar(int cur, int dir) {
    for(int n=0;n<NK;n++){ cur=(cur+dir+NK)%NK; if(charUnlocked(cur)) return cur; }
    return charSel;
}
static int nextUnlockedTrack(int cur, int dir) {
    for(int n=0;n<NTRACKS;n++){ cur=(cur+dir+NTRACKS)%NTRACKS; if(!careerActive || trackUnlocked(cur)) return cur; }
    return trackSel;
}

static void resetChampionship(void) {
    memset(champPoints,0,sizeof(champPoints)); champRace=0; champOverallRank=0; champMedal=0;
    careerActive = (gameMode==GAME_CAREER);
    careerLevel = saveData.careerLevel;
}

static void update(SceCtrlData *pad) {
    tAnim += DT; skyScroll += DT; updateFx();
    for (int i = 0; i < NK; ++i) for (int j = i + 1; j < NK; ++j) {
        if (kartBumpCooldown[i][j] > 0.0f) {
            kartBumpCooldown[i][j] -= DT;
            if (kartBumpCooldown[i][j] < 0.0f) kartBumpCooldown[i][j] = 0.0f;
            kartBumpCooldown[j][i] = kartBumpCooldown[i][j];
        }
    }
    unsigned pressed = pad->Buttons & ~prevB;

    if (state == STATE_MENU) {
        prevB=pad->Buttons;
        if (pressed & PSP_CTRL_UP) { menuSel=(menuSel+4)%5; playMenuSelect(); }
        if (pressed & PSP_CTRL_DOWN) { menuSel=(menuSel+1)%5; playMenuSelect(); }
        if (pressed & PSP_CTRL_CROSS) {
            playMenuConfirm();
            if(menuSel==4) state=STATE_STATS;
            else { gameMode=menuSel; state=STATE_DIFF; }
        }
        if (pressed & PSP_CTRL_TRIANGLE) { skyPhase=(skyPhase+1)%4; playMenuSelect(); }
        return;
    }
    if (state == STATE_STATS) {
        prevB=pad->Buttons;
        if (pressed & (PSP_CTRL_CIRCLE|PSP_CTRL_START)) { playMenuBack(); state=STATE_MENU; }
        return;
    }
    if (state == STATE_DIFF) {
        prevB=pad->Buttons;
        if(pressed & PSP_CTRL_LEFT) { diffSel=(diffSel+3)%4; playMenuSelect(); }
        if(pressed & PSP_CTRL_RIGHT) { diffSel=(diffSel+1)%4; playMenuSelect(); }
        if(pressed & PSP_CTRL_TRIANGLE) { skyPhase=(skyPhase+1)%4; playMenuSelect(); }
        if(pressed & PSP_CTRL_CIRCLE) { playMenuBack(); state=STATE_MENU; }
        if(pressed & PSP_CTRL_CROSS) { playMenuConfirm();
            mirrorMode=(diffSel==3); if(gameMode==GAME_CHAMPIONSHIP||gameMode==GAME_CAREER) { resetChampionship(); state=STATE_CUPSELECT; }
            else state=STATE_CHAR;
        }
        return;
    }
    if (state == STATE_CUPSELECT) {
        prevB=pad->Buttons;
        int old=cupSel;
        if(pressed & PSP_CTRL_LEFT){ playMenuSelect(); do{ cupSel=(cupSel+NCUPS-1)%NCUPS; }while(gameMode==GAME_CAREER && !cupUnlocked(cupSel) && cupSel!=old); }
        if(pressed & PSP_CTRL_RIGHT){ playMenuSelect(); do{ cupSel=(cupSel+1)%NCUPS; }while(gameMode==GAME_CAREER && !cupUnlocked(cupSel) && cupSel!=old); }
        if(pressed & PSP_CTRL_CIRCLE) { playMenuBack(); state=STATE_DIFF; }
        if(pressed & PSP_CTRL_CROSS) { playMenuConfirm(); state=STATE_CHAR; resetChampionship(); }
        return;
    }
    if (state == STATE_CHAR) {
        prevB=pad->Buttons;
        int old=charSel;
        /* D-pad and analog both change character (edge-detect analog via threshold). */
        static int analogLatch = 0;
        int lx = (int)pad->Lx - 128;
        if(pressed & PSP_CTRL_LEFT) { charSel=nextUnlockedChar(charSel,-1); playMenuSelect(); }
        if(pressed & PSP_CTRL_RIGHT) { charSel=nextUnlockedChar(charSel,1); playMenuSelect(); }
        if (lx < -60) {
            if (!analogLatch) { charSel=nextUnlockedChar(charSel,-1); playMenuSelect(); analogLatch = 1; }
        } else if (lx > 60) {
            if (!analogLatch) { charSel=nextUnlockedChar(charSel,1); playMenuSelect(); analogLatch = 1; }
        } else {
            analogLatch = 0;
        }
        if (!charUnlocked(charSel)) charSel = nextUnlockedChar(charSel, 1);
        if(charSel!=old) {
            rebuildAll();
            {
                int mid = charModelId(charSel);
                if (mid == 0) playFileSfx(FSFX_MARIO);
                else if (mid == 1) playFileSfx(FSFX_LUIGI);
                else playFileSfx(FSFX_YOSHI);
            }
        }
        if(pressed & PSP_CTRL_CIRCLE) { playMenuBack(); state=(isChampMode()?STATE_CUPSELECT:STATE_DIFF); }
        if(pressed & PSP_CTRL_CROSS) {
            if (charUnlocked(charSel)) {
                playMenuConfirm();
                if(isChampMode()) { champRace=0; trackSel=cupTracks[cupSel][0]; rebuildAll(); }
                else state=STATE_TRACK;
            }
        }
        return;
    }
    if (state == STATE_TRACK) {
        prevB=pad->Buttons;
        int old=trackSel;
        if(pressed & PSP_CTRL_LEFT) trackSel=nextUnlockedTrack(trackSel,-1);
        if(pressed & PSP_CTRL_RIGHT) trackSel=nextUnlockedTrack(trackSel,1);
        if(pressed & PSP_CTRL_TRIANGLE) skyPhase=(skyPhase+1)%4;
        if(trackSel!=old) rebuildAll();
        if(pressed & PSP_CTRL_CIRCLE) state=STATE_CHAR;
        if(pressed & PSP_CTRL_CROSS) rebuildAll();
        return;
    }

    /* START is the pause button during a race. */
    if (state == 1 && (pressed & PSP_CTRL_START)) {
        state = STATE_PAUSE;
        prevB = pad->Buttons;
        return;
    }

    if (state == STATE_PAUSE) {
        if (pressed & PSP_CTRL_START) state = 1;
        else if (pressed & PSP_CTRL_SELECT) rebuildAll();
        if (pressed & PSP_CTRL_TRIANGLE) aMusicOn = !aMusicOn;
        prevB = pad->Buttons;
        return;
    }

    /* Triangle cycles three lightweight chase-camera presets. */
    if (state == 1 && (pressed & PSP_CTRL_TRIANGLE))
        cameraMode = (cameraMode + 1) % 3;

    if (state == STATE_CUPRESULT) {
        prevB=pad->Buttons;
        if(pressed & PSP_CTRL_CIRCLE) { state=STATE_MENU; return; }
        if(pressed & (PSP_CTRL_START|PSP_CTRL_CROSS)) {
            if(champRace < CHAMP_RACES) { trackSel=cupTracks[cupSel][champRace]; rebuildAll(); }
        }
        return;
    }
    /* SELECT provides a quick restart after the finish. */
    if (state == 2 && (pressed & PSP_CTRL_SELECT)) { rebuildAll(); return; }
    if (state == 2 && (pressed & PSP_CTRL_START)) { state=STATE_MENU; return; }

    prevB = pad->Buttons;

    float steer = readSteer(pad);
    int gas = (pad->Buttons & PSP_CTRL_CROSS) != 0;
    int brake = (pad->Buttons & PSP_CTRL_SQUARE) != 0;
    int drift = (pad->Buttons & (PSP_CTRL_RTRIGGER | PSP_CTRL_LTRIGGER)) != 0;

    if (shake > 0) shake -= DT;
    if (lapFlash > 0) lapFlash -= DT;
    /* Item ruleti: donme bitince item ver */
    if (itemRouletteT > 0) {
        itemRouletteT -= DT;
        if (itemRouletteT <= 0) {
            itemRouletteT = 0;
            K[0].item = itemRouletteResult;
            itemCntG = 1;
            playMenuConfirm();
            screenFlash = 0.25f;
        }
    }

    if (state == 0) {
        cd -= DT;
        if (cd <= 1.0f) state = 1;
    } else if (state == 1) {
        raceTime += DT;
        lapTimer += DT;
        if (gameMode == GAME_TIME_TRIAL && (ghostTick++ & 1) == 0 && ghostCount < GHOST_MAX) {
            ghostRec[ghostCount].x = K[0].x;
            ghostRec[ghostCount].z = K[0].z;
            ghostRec[ghostCount].h = K[0].h;
            ghostCount++;
        }
        stepKart(&K[0], steer, gas, brake, drift);
        if (gameMode == GAME_RACE) for (int i = 1; i < NK; i++) aiDrive(&K[i], i);

        /* kart-kart carpisma */
        const int nk = activeKarts();
        for (int i = 0; i < nk; i++) for (int j = i + 1; j < nk; j++) {
            float dx = K[j].x - K[i].x, dz = K[j].z - K[i].z;
            float d2 = dx * dx + dz * dz;
            const float minDist = 2.7f;
            if (d2 < minDist * minDist) {
                float d, nx, nz;
                if (d2 < 0.0001f) {
                    /* Stable fallback direction avoids coincident karts locking
                       together (which used to look like the characters vanished). */
                    float a = (float)(i * 7 + j * 11) * 0.6180339f;
                    nx = cosf(a); nz = sinf(a); d = 0.0f;
                } else {
                    d = sqrtf(d2); nx = dx / d; nz = dz / d;
                }
                float ov = (minDist - d) * 0.52f + 0.015f;
                K[i].x -= nx * ov; K[i].z -= nz * ov;
                K[j].x += nx * ov; K[j].z += nz * ov;
                K[i].spd *= 0.992f; K[j].spd *= 0.992f;
                if (kartBumpCooldown[i][j] <= 0.0f) {
                    spawnKartFx(i, 2, 4); spawnKartFx(j, 2, 4);
                    kartBumpCooldown[i][j] = kartBumpCooldown[j][i] = 0.35f;
                    triggerSfx(SFX_COLLISION);
                    if (i == 0 || j == 0) shake = 0.15f;
                }
            }
        }
        for (int i = 0; i < NK; i++) trackUpdate(&K[i]);

        Kart *p = &K[0];
        updateItemTimers();
        updateShell();

        if (padMark[p->idx] && fabsf(p->lat) < HW * 0.45f && p->boostT < 0.8f)
            p->boostT = 1.2f;

        /* Player item boxes. Bots are handled in aiDrive(). */
        for (int i = 0; i < nthings; i++) {
            Thing *t = &things[i];
            if (!t->active) {
                if ((t->type == TH_ITEM || t->type == TH_BANANA) && t->resp > 0) {
                    t->resp -= DT;
                    if (t->resp <= 0 && t->type == TH_ITEM) t->active = 1;
                }
                continue;
            }

            float dx = t->x - p->x, dz = t->z - p->z;
            if (t->type == TH_ITEM) {
                if (dx * dx + dz * dz < 2.3f * 2.3f) {
                    t->active = 0;
                    t->resp = 8.0f;
                    if (itemCntG == 0 && itemRouletteT <= 0 && gameMode == GAME_RACE) giveItem(p);
                }
            } else if (t->type == TH_CONE) {
                if (dx * dx + dz * dz < 1.8f * 1.8f) {
                    t->active = 0;
                    hitKart(p, 0.35f, 0.9f);
                    shake = 0.5f;
                }
            }
        }

        /* Banana traps can hit player or any bot. */
        for (int ti = 0; ti < nthings; ti++) {
            Thing *t = &things[ti];
            if (!t->active || t->type != TH_BANANA) continue;
            for (int ki = 0; ki < NK; ki++) {
                float dx = t->x - K[ki].x, dz = t->z - K[ki].z;
                if (dx * dx + dz * dz < 2.0f * 2.0f) {
                    hitKart(&K[ki], 0.32f, 1.0f);
                    t->active = 0;
                    t->resp = 0;
                    if (ki == 0) shake = 0.45f;
                    break;
                }
            }
        }

        if (pressed & PSP_CTRL_CIRCLE) useItem();

        /* tur */
        {
            int lp = (int)floorf(p->prog / TL) + 1;
            if (lp > lap && lp <= LAPS) {
                checkBestLap(); lapTimer = 0;
                lap = lp; lapFlash = 2.0f;
                for (int i = 0; i < nthings; i++) if (things[i].type == TH_CONE) things[i].active = 1;
            }
            if (p->prog >= LAPS * TL) {
                finalRank = (gameMode == GAME_TIME_TRIAL) ? 1 : calcRank();
                finalTime = raceTime;
                finishRace(finalTime, finalRank);
                if (isChampMode()) {
                    applyChampionshipRacePoints();
                    champRace++;
                    if (champRace < CHAMP_RACES) {
                        state = STATE_CUPRESULT;
                        triggerSfx(SFX_FINISH);
                    } else {
                        awardChampionship();
                        state = 2;
                        triggerSfx(SFX_FINISH);
                    }
                } else { state=2; triggerSfx(SFX_FINISH); }
            }
        }
    }
}
/* ---------- Cizim ---------- */
static void modelAt(float x, float y, float z, float yaw) {
    ScePspFVector3 t; t.x = x; t.y = y; t.z = z;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t);
    sceGumRotateY(-yaw);
}


static void render3D(void) {
    Kart *p = &K[0];
    float tgt = p->vh + wrapA(p->h - p->vh) * 0.5f;
    camH = wrapA(camH + wrapA(tgt - camH) * clampf(5.0f * DT, 0, 1));
    float boostF = (p->boostT > 0) ? 1.0f : 0.0f;

    /* Three PSP-friendly chase cameras: normal, close, wide. */
    float baseFov = 57.0f, back = 8.4f, eyeY = 3.55f, targetY = 1.05f, targetD = 8.0f;
    if (cameraMode == 1) {
        baseFov = 53.0f; back = 6.8f; eyeY = 3.0f; targetD = 8.2f;
    } else if (cameraMode == 2) {
        baseFov = 65.0f; back = 12.0f; eyeY = 4.9f; targetD = 9.0f;
    }
    /* Keep the kart framed during boosts; strong FOV changes made models look
       stretched and blurred on the PSP's small screen. */
    back += 0.45f * boostF;
    fovCur += ((baseFov + 4.0f * boostF) - fovCur) * clampf(5.0f * DT, 0, 1);

    /* Subtle camera feedback only: large shake obscures the driver and road. */
    float sh = (shake > 0) ? sinf(tAnim * 70.0f) * 0.10f : 0.0f;
    if (boostF > 0) sh += sinf(tAnim * 42.0f) * 0.018f;
    ScePspFVector3 eye, ctr, up;
    eye.x = p->x - cosf(camH) * back; eye.y = eyeY + sh; eye.z = p->z - sinf(camH) * back;
    ctr.x = p->x + cosf(camH) * targetD; ctr.y = targetY; ctr.z = p->z + sinf(camH) * targetD;
    up.x = 0; up.y = 1; up.z = 0;

    /* Sky layer: cycle Day/Sunset/Night/Rainbow Road; Rainbow Road track selects its own sky. */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    int useRainbowSky = (skyPhase == 3 || trackSel == NTRACKS - 1);
    const PspTexture *skyTex = useRainbowSky ? texSkyRainbow :
        ((skyPhase==0)?texSkyDay:((skyPhase==1)?texSkySunset:texSkyNight));
    if (skyTex && skyTex->ready) {
        drawTexRect(skyTex,0,0,W,H,0,0,(float)skyTex->w,(float)skyTex->h,RGBA(255,255,255,255));
        if (useRainbowSky) {
            /* lightweight twinkling highlights: no extra texture passes */
            for (int si=0; si<10; ++si) {
                float sx=fmodf(si*71.0f + skyScroll*(4.0f + (si%3)), (float)W);
                float sy=12.0f + (float)((si*37)%112);
                int a=90+(int)(80.0f*(0.5f+0.5f*sinf(tAnim*3.0f+si)));
                rect(sx,sy,1+(si%2),1+(si%2),RGBA(190+(si%3)*25,200,255,a));
            }
        }
    } else {
        unsigned st=SKYTOP, hz=HAZE;
        if(useRainbowSky){ st=RGB(7,5,34); hz=RGB(70,48,120); }
        else if(skyPhase==1){ st=mixc(st,RGB(245,100,40),0.52f); hz=mixc(hz,RGB(255,180,90),0.55f); }
        else if(skyPhase==2){ st=mixc(st,RGB(8,12,42),0.80f); hz=mixc(hz,RGB(35,45,85),0.65f); }
        vgrad(0,0,W,150,st,hz); rect(0,150,W,H-150,hz);
    }
    /* regular fluffy clouds only on the classic three skies */
    if (!useRainbowSky && skyPhase != 2 && texCloud && texCloud->ready) {
        for(int ci=0;ci<3;ci++){
            float cx=fmodf(ci*174.0f + tAnim*(5.0f+ci) + skyScroll*35.0f,W+120.0f)-60.0f;
            float cy=28.0f + (ci%3)*28.0f;
            drawTexRect(texCloud,cx,cy,96,42,0,0,(float)texCloud->w,(float)texCloud->h,RGBA(255,255,255,185));
        }
    }

    /* uzak dag panoramasi: hareket etmeyen 3 katman yerine hafif kayan texture */
    if (!useRainbowSky && texMountain && texMountain->ready) {
        float mx=fmodf(-tAnim*3.0f + skyScroll*12.0f, (float)W);
        drawTexRect(texMountain,mx-2*W/3.0f,116,W*0.92f,72,0,0,(float)texMountain->w,(float)texMountain->h,RGBA(255,255,255,220));
        drawTexRect(texMountain,mx+W/4.0f,126,W*0.92f,64,0,0,(float)texMountain->w,(float)texMountain->h,RGBA(225,235,255,180));
    }

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fovCur, (float)W / (float)H, 1.5f, 1200.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &ctr, &up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    sceGuFog(200.0f, 900.0f, HAZE);
    sceGuEnable(GU_FOG);
    /* Terrain UVs are authored as repeat coordinates. Imported kart/character meshes use normalized UVs and set their own per-texture scales. */
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    /* zemin (cim dokusu) */
    sceGuEnable(GU_TEXTURE_2D);
    bindTexture(texGrassFile, texGrass);
    sceGumDrawArray(GU_TRIANGLES, VF3, groundEnd, 0, mesh);
    sceGuDisable(GU_TEXTURE_2D);
    /* cim seritleri + rumble (duz renk) */
    sceGumDrawArray(GU_TRIANGLES, VF3, roadStart - groundEnd, 0, &mesh[groundEnd]);
    /* yol (asfalt dokusu) */
    sceGuEnable(GU_TEXTURE_2D);
    bindTexture(texRoadFile, texRoad);
    sceGumDrawArray(GU_TRIANGLES, VF3, roadEnd - roadStart, 0, &mesh[roadStart]);
    sceGuDisable(GU_TEXTURE_2D);
    /* turbo seritleri, cizgiler, baslangic (duz renk) */
    sceGumDrawArray(GU_TRIANGLES, VF3, nFlat - roadEnd, 0, &mesh[roadEnd]);
    /* Depth-test shadows against road/terrain; otherwise they smear over
       distant geometry on bends. */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    /* golgeler */
    for (int i = 0; i < activeKarts(); i++) {
        float sdx = K[i].x - K[0].x, sdz = K[i].z - K[0].z;
        if (i != 0 && sdx * sdx + sdz * sdz > 58.0f * 58.0f) continue;
        modelAt(K[i].x, 0.08f, K[i].z, K[i].h);
        sceGumDrawArray(GU_TRIANGLES, VF3, shadowCount, 0, &mesh[shadowStart]);
    }
    /* nesneler (derinlik testli) */
    sceGuEnable(GU_DEPTH_TEST);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumDrawArray(GU_TRIANGLES, VF3, nScene - nFlat, 0, &mesh[nFlat]);

    for (int i = 0; i < activeKarts(); i++) {
        Kart *k = &K[i];
        float bob = 0.08f * sinf(k->bobA) * clampf(0.35f + k->spd / VMAX, 0, 1.2f);
        if (k->spinT > 0) bob += 0.08f * sinf(tAnim * 36.0f);
        if (k->boostT > 0) bob += 0.03f * sinf(tAnim * 40.0f + i);
        float yaw = k->h + k->driftDir * 0.35f + k->spinA;
        modelAt(k->x, bob, k->z, yaw);
        sceGumRotateX(k->pitch);
        sceGumRotateZ(k->lean);

        /* All racers use the real textured Pipe Frame 64 3D kart mesh and
           all active racers keep their driver model visible. */
        sceGumPushMatrix();
        drawKartModel(i);
        sceGumPopMatrix();
        if (k->boostT > 0) {
            /* Player gets the full 3-layer flame; AI racers use one layer to
               reduce overdraw and transform work on the PSP. */
            int flameLayers = (i == 0) ? 3 : 1;
            for (int layer = 0; layer < flameLayers; layer++) {
                float phase = tAnim * (28.0f + layer * 9.0f) + i + layer;
                ScePspFVector3 t, sc;
                t.x = -1.7f - layer * 0.35f;
                t.y = 0.55f + 0.08f * sinf(phase);
                t.z = 0.08f * sinf(phase * 1.3f);
                float pulse = 0.70f + 0.45f * (0.5f + 0.5f * sinf(tAnim * (40.0f + layer * 12.0f) + i));
                sc.x = pulse * (1.0f + layer * 0.15f);
                sc.y = pulse * (0.85f + layer * 0.1f);
                sc.z = pulse * (0.9f + layer * 0.12f);
                sceGumPushMatrix();
                sceGumTranslate(&t);
                sceGumScale(&sc);
                sceGumDrawArray(GU_TRIANGLES, VF3, flameCount, 0, &mesh[flameStart]);
                sceGumPopMatrix();
            }
        }
        if (k->starT > 0) {
            int starCount = (i == 0) ? 6 : 3;
            for (int q = 0; q < starCount; q++) {
                float a = tAnim * 9.0f + q * (PI / 3.0f);
                float r = 2.0f + 0.35f * sinf(tAnim * 5.0f + q);
                ScePspFVector3 t, sc;
                t.x = cosf(a) * r; t.y = 1.0f + 0.6f * sinf(a * 1.7f + tAnim); t.z = sinf(a) * r;
                float s = 0.45f + 0.15f * sinf(tAnim * 12.0f + q);
                sc.x = s; sc.y = s; sc.z = s;
                sceGumPushMatrix();
                sceGumTranslate(&t);
                sceGumScale(&sc);
                sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
                sceGumPopMatrix();
            }
        }
    }
    /* Drivers are drawn in a separate pass after every kart body. This prevents
       a later kart draw call from stamping over an earlier driver's visible
       upper body during close contact, while depth-testing still handles true
       foreground/background occlusion. */
    sceGuEnable(GU_DEPTH_TEST);
    for (int i = 0; i < activeKarts(); i++) {
        if (!shouldDrawDriver(i)) continue;
        /* Keep all drivers visible at close range (including collisions), but
           skip costly multipart driver meshes when rivals are far away. */
        float ddx = K[i].x - K[0].x, ddz = K[i].z - K[0].z;
        if (i != 0 && ddx * ddx + ddz * ddz > 88.0f * 88.0f) continue;
        Kart *k = &K[i];
        float bob = 0.08f * sinf(k->bobA) * clampf(0.35f + k->spd / VMAX, 0, 1.2f);
        if (k->spinT > 0) bob += 0.08f * sinf(tAnim * 36.0f);
        if (k->boostT > 0) bob += 0.03f * sinf(tAnim * 40.0f + i);
        float yaw = k->h + k->driftDir * 0.35f + k->spinA;
        modelAt(k->x, bob, k->z, yaw);
        sceGumRotateX(k->pitch);
        sceGumRotateZ(k->lean);
        sceGumPushMatrix();
        drawCharacterModel(i);
        sceGumPopMatrix();
        drawCharacterAccessories(i);
    }
    /* ghost (en iyi time trial turu, 30 Hz kayit, aralari interpolasyon) */
    if (ghostMode && ghostBestCount > 0) {
        float ft = raceTime * 30.0f;
        int gi = (int)ft;
        float fr = ft - (float)gi;
        if (gi >= 0 && gi < ghostBestCount) {
            int g2 = (gi + 1 < ghostBestCount) ? gi + 1 : gi;
            float gx = ghostBest[gi].x + (ghostBest[g2].x - ghostBest[gi].x) * fr;
            float gz = ghostBest[gi].z + (ghostBest[g2].z - ghostBest[gi].z) * fr;
            float gh = ghostBest[gi].h + wrapA(ghostBest[g2].h - ghostBest[gi].h) * fr;
            modelAt(gx, 0.1f, gz, gh);
            sceGumDrawArray(GU_TRIANGLES, VF3, ghostCount3, 0, &mesh[ghostStart]);
        }
    }
    for (int i = 0; i < nthings; i++) {
        Thing *t = &things[i];
        if (!t->active) continue;
        if (t->type == TH_ITEM) {
            float bobY = 1.45f + 0.28f * sinf(tAnim * 3.5f + i);
            float spin = tAnim * 2.4f + i * 0.4f;
            float pulse = 1.0f + 0.08f * sinf(tAnim * 6.0f + i);
            modelAt(t->x, bobY, t->z, spin);
            {
                ScePspFVector3 sc; sc.x = pulse; sc.y = pulse; sc.z = pulse;
                sceGumScale(&sc);
            }
            sceGumDrawArray(GU_TRIANGLES, VF3, boxCount, 0, &mesh[boxStart]);
            drawItemBoxFace3D();
            /* kutu etrafinda kivilcim halkasi */
            if ((i & 1) == 0) {
                float a = tAnim * 5.0f + i;
                ScePspFVector3 ot;
                ot.x = cosf(a) * 1.1f; ot.y = 0.3f; ot.z = sinf(a) * 1.1f;
                sceGumPushMatrix();
                sceGumTranslate(&ot);
                ScePspFVector3 ss; ss.x = 0.25f; ss.y = 0.25f; ss.z = 0.25f;
                sceGumScale(&ss);
                sceGumDrawArray(GU_TRIANGLES, VF3, sparkCount, 0, &mesh[sparkStart]);
                sceGumPopMatrix();
            }
        } else if (t->type == TH_CONE) {
            modelAt(t->x, 0, t->z, 0);
            sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
        } else if (t->type == TH_BANANA) {
            modelAt(t->x, 0.35f + 0.05f * sinf(tAnim * 4.0f + i), t->z, tAnim * 3.0f + i);
            sceGumDrawArray(GU_TRIANGLES, VF3, banCount, 0, &mesh[banStart]);
        }
    }

    for (int owner = 0; owner < NK; owner++) if (shellShots[owner].active) {
        modelAt(shellShots[owner].x, 1.0f + 0.18f * sinf(tAnim * 16.0f + owner), shellShots[owner].z, tAnim * 10.0f);
        if (shellShots[owner].type == SHELL_RED)
            sceGumDrawArray(GU_TRIANGLES, VF3, shellRCount, 0, &mesh[shellRStart]);
        else
            sceGumDrawArray(GU_TRIANGLES, VF3, shellGCount, 0, &mesh[shellGStart]);
        /* kabuk kuyrugu: kucuk kivilcim */
        if ((owner + (int)(tAnim * 20.0f)) & 1) {
            ScePspFVector3 ot = { -0.6f, 0, 0 };
            sceGumPushMatrix();
            sceGumTranslate(&ot);
            ScePspFVector3 ss = { 0.3f, 0.3f, 0.3f };
            sceGumScale(&ss);
            sceGumDrawArray(GU_TRIANGLES, VF3, sparkCount, 0, &mesh[sparkStart]);
            sceGumPopMatrix();
        }
    }
    /* Parcaciklar: kind'a gore olcek ve donme hizi */
    for (int i = 0; i < MAXFX; i++) if (fx[i].active) {
        float fdx = fx[i].x - K[0].x, fdz = fx[i].z - K[0].z;
        if (fdx * fdx + fdz * fdz > 82.0f * 82.0f) continue;
        float fade = clampf(fx[i].life / fx[i].maxLife, 0, 1);
        float s = fx[i].size * (0.45f + 1.1f * fade);
        /* kind 0 = boost (buyuk), 1 = drift (kucuk), 2 = hit (orta) */
        if (fx[i].kind == 0) s *= 1.25f;
        else if (fx[i].kind == 1) s *= 0.75f;
        float spin = tAnim * (fx[i].kind == 1 ? 10.0f : 22.0f) + i;
        modelAt(fx[i].x, fx[i].y, fx[i].z, spin);
        {
            ScePspFVector3 sc; sc.x = s; sc.y = s * (0.7f + 0.3f * fade); sc.z = s;
            sceGumScale(&sc);
        }
        sceGumDrawArray(GU_TRIANGLES, VF3, sparkCount, 0, &mesh[sparkStart]);
    }
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
}

static void drawMush(float x, float y, float s) {
    tri2(x, y + s * 1.1f, x + s * 2, y + s * 1.1f, x + s, y - s * 0.2f, RGB(235, 40, 40));
    rect(x + s * 0.55f, y + s * 1.1f, s * 0.9f, s * 0.9f, RGB(250, 240, 215));
    rect(x + s * 0.8f, y + s * 0.5f, s * 0.4f, s * 0.35f, RGB(255, 255, 255));
}

/* Animasyonlu Mario Kart menü arka planı: gradyan gökyüzü, kayan bulut, yıldız, dama zemin */
static void drawMenuBG(void) {
    /* Rainbow Road preview can also be selected for the menus. */
    if (skyPhase == 3 && texSkyRainbow && texSkyRainbow->ready) {
        drawTexRect(texSkyRainbow,0,0,W,H,0,0,(float)texSkyRainbow->w,(float)texSkyRainbow->h,RGBA(255,255,255,255));
    } else {
        /* dikey gradyan: ust mavi-mor, alt koyu lacivert */
        for (int y = 0; y < H; y += 4) {
            float t = (float)y / (float)H;
            int r = (int)(18 + 40 * (1.0f - t) + 20 * sinf(tAnim * 0.7f + t * 2.0f));
            int g = (int)(30 + 50 * (1.0f - t));
            int b = (int)(90 + 100 * (1.0f - t) + 15 * sinf(tAnim * 0.5f));
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            rect(0, y, W, 4, RGB(r, g, b));
        }
    }
    /* kayan yildiz / isik noktalari */
    for (int i = 0; i < 18; i++) {
        float sx = fmodf(i * 97.0f + tAnim * (12.0f + (i % 5)) + skyScroll * 20.0f, (float)(W + 20)) - 10.0f;
        float sy = 20.0f + fmodf(i * 53.0f, 140.0f) + 6.0f * sinf(tAnim * 2.0f + i);
        float pulse = 0.5f + 0.5f * sinf(tAnim * 4.0f + i * 0.7f);
        int a = (int)(120 + 100 * pulse);
        int sz = 2 + (i % 3);
        rect(sx, sy, sz, sz, RGBA(255, 255, 220, a));
    }
    /* bulut katmani (varsa texture, yoksa yumusak dikdortgen) */
    if (skyPhase != 3 && texCloud && texCloud->ready) {
        for (int ci = 0; ci < 4; ci++) {
            float cx = fmodf(ci * 160.0f + tAnim * (8.0f + ci * 2.0f) + skyScroll * 25.0f, (float)(W + 160)) - 80.0f;
            float cy = 28.0f + ci * 18.0f + 4.0f * sinf(tAnim * 1.2f + ci);
            drawTexRect(texCloud, cx, cy, 140, 36, 0, 0, (float)texCloud->w, (float)texCloud->h, RGBA(255, 255, 255, 160));
        }
    } else if (skyPhase != 3) {
        for (int ci = 0; ci < 4; ci++) {
            float cx = fmodf(ci * 160.0f + tAnim * (8.0f + ci * 2.0f), (float)(W + 140)) - 70.0f;
            float cy = 30.0f + ci * 16.0f;
            rect(cx, cy, 90, 18, RGBA(220, 230, 255, 50));
            rect(cx + 20, cy - 8, 60, 14, RGBA(220, 230, 255, 40));
        }
    }
    /* alt dama zemin seridi (Mario Kart hissi) */
    int floorY = H - 28;
    rect(0, floorY - 4, W, 4, RGB(255, 220, 40));
    float shift = fmodf(tAnim * 40.0f, 24.0f);
    for (int x = -24; x < W + 24; x += 24) {
        int on = (((int)((x + shift) / 24)) & 1);
        rect(x + shift, floorY, 24, 28, on ? RGB(30, 30, 35) : RGB(240, 240, 245));
    }
}

static int atlasCellForItem(int item) {
    switch(item) {
    case ITEM_MUSH: return 0; case ITEM_GREENSHELL: return 1; case ITEM_REDSHELL: return 2; case ITEM_BANANA: return 3;
    case ITEM_LIGHT: return 4; case ITEM_SHIELD: return 5; case ITEM_STAR: return 6; default: return 7; }
}

static void drawHUD(void) {
    char buf[40];
    Kart *p = &K[0];
    if (state == 0 || state == 1 || state == 2) drawPlayerPortraitHUD();

    if (state == STATE_STATS) {
        rect(0,0,W,H,RGBA(0,0,0,180));
        text(W/2-textWidth("ISTATISTIK",16,5)/2,10,16,28,5,"ISTATISTIK",RGB(255,220,50));
        snprintf(buf,sizeof(buf),"WINS %d",saveData.totalWins); text(42,58,10,16,3,buf,RGB(255,255,255));
        snprintf(buf,sizeof(buf),"RACES %d",saveData.totalRaces); text(42,82,10,16,3,buf,RGB(200,220,240));
        snprintf(buf,sizeof(buf),"ITEMS %d",saveData.totalItems); text(42,106,10,16,3,buf,RGB(200,220,240));
        snprintf(buf,sizeof(buf),"HITS %d",saveData.totalHits); text(42,130,10,16,3,buf,RGB(200,220,240));
        snprintf(buf,sizeof(buf),"BOOSTS %d",saveData.totalBoosts); text(42,154,10,16,3,buf,RGB(200,220,240));
        snprintf(buf,sizeof(buf),"CAREER %d",saveData.careerLevel); text(250,58,10,16,3,buf,RGB(120,240,255));
        snprintf(buf,sizeof(buf),"CHAR %d/%d",countUnlocked(saveData.unlockedChars,NK),NK); text(250,82,10,16,3,buf,RGB(200,220,240));
        snprintf(buf,sizeof(buf),"TRACK %d/%d",countUnlocked(saveData.unlockedTracks,NTRACKS),NTRACKS); text(250,106,10,16,3,buf,RGB(200,220,240));
        snprintf(buf,sizeof(buf),"CUP %d/%d",countUnlocked(saveData.unlockedCups,NCUPS),NCUPS); text(250,130,10,16,3,buf,RGB(200,220,240));
        snprintf(buf,sizeof(buf),"G %d  S %d  B %d",saveData.cupsGold,saveData.cupsSilver,saveData.cupsBronze); text(190,174,10,16,3,buf,RGB(255,220,100));
        text(W/2-textWidth("O/START: GERI",9,3)/2,236,9,14,3,"O/START: GERI",RGB(180,210,235));
        return;
    }
    if (state == STATE_DIFF) {
        drawMenuBG();
        rect(0,0,W,34,RGB(200,30,40));
        text(W/2-textWidth("ZORLUK",16,5)/2,6,16,24,5,"ZORLUK",RGB(255,220,50));
        for(int i=0;i<4;i++){
            int bx=30+i*108;
            int sel=(i==diffSel);
            float bounce = sel ? (3.0f * fabsf(sinf(tAnim * 5.0f))) : 0.0f;
            int by = 96 - (int)bounce;
            if(sel) {
                float g = 0.5f + 0.5f * sinf(tAnim * 7.0f);
                rect(bx-5,by-5,102,58,mixc(RGB(255,220,50),RGB(255,100,40),g));
            }
            rect(bx,by,92,48,sel?RGBA(30,60,120,240):RGBA(20,50,100,200));
            text(bx+8,by+13,10,16,3,diffNames[i],sel?RGB(255,255,255):RGB(170,190,210));
        }
        snprintf(buf,sizeof(buf),"SKY %s",skyPhase==0?"DAY":(skyPhase==1?"SUNSET":(skyPhase==2?"NIGHT":"RAINBOW")));
        text(W/2-textWidth(buf,10,3)/2,176,10,16,3,buf,RGB(255,230,120));
        if (((int)(tAnim * 2.2f)) & 1)
            text(W/2-textWidth("SOL SAG SEC   X DEVAM   O GERI",7,2)/2,240,7,12,2,"SOL SAG SEC   X DEVAM   O GERI",RGB(205,220,235));
        return;
    }
    if (state == STATE_CUPSELECT) {
        drawMenuBG();
        rect(0,0,W,34,RGB(200,30,40));
        text(W/2-textWidth("SELECT CUP",12,4)/2,6,12,20,4,"SELECT CUP",RGB(255,220,50));
        /* 2x2 kupa grid (Mushroom / Flower / Star / Special) */
        for(int i=0;i<NCUPS;i++){
            int col = i % 2, row = i / 2;
            int bx = 40 + col * 210;
            int by0 = 48 + row * 88;
            int locked=(gameMode==GAME_CAREER && !cupUnlocked(i));
            int sel=(i==cupSel);
            float bounce = sel ? (3.0f * fabsf(sinf(tAnim * 5.0f))) : 0.0f;
            int by = by0 - (int)bounce;
            if(sel) {
                float g = 0.5f + 0.5f * sinf(tAnim * 7.0f);
                rect(bx-5,by-5,196,78,mixc(RGB(255,220,50),RGB(255,100,40),g));
            }
            rect(bx,by,186,70,locked?RGBA(35,35,45,230):RGBA(25,70,120,230));
            text(bx+10,by+10,8,14,2,locked?"LOCKED":cupNames[i],locked?RGB(255,90,90):RGB(255,255,255));
            for(int r=0;r<4;r++){
                int tr=cupTracks[i][r];
                rect(bx+12+r*42,by+36,36,14,kBody[tr%NK]);
                /* kisa pist adi */
                text(bx+14+r*42, by+50, 4, 8, 1, themes[tr].name, RGB(220,230,245));
            }
        }
        if (((int)(tAnim * 2.2f)) & 1)
            text(W/2-textWidth("< > CUP   X START   O BACK",8,2)/2,236,8,14,2,"< > CUP   X START   O BACK",RGB(205,220,235));
        return;
    }
    /* Karakter secimi — animasyonlu grid, bounce, pulse */
    if (state == STATE_CHAR) {
        drawMenuBG();
        /* ust banner + dama */
        rect(0, 0, W, 34, RGB(200, 30, 40));
        float chk = fmodf(tAnim * 30.0f, 16.0f);
        for (int cx = -16; cx < W + 16; cx += 16) {
            int on = (((int)((cx + chk) / 16)) & 1);
            rect(cx + chk, 34, 16, 7, on ? RGB(245, 245, 245) : RGB(20, 20, 20));
        }
        float titlePulse = 1.0f + 0.06f * sinf(tAnim * 4.0f);
        int tw = (int)(12 * titlePulse), th = (int)(20 * titlePulse);
        text(W / 2 - textWidth("CHOOSE YOUR RACER", tw, 3) / 2, 6, tw, th, 3, "CHOOSE YOUR RACER", RGB(255, 240, 80));

        /* 6 karakter — secili olan zıplar ve büyür */
        for (int i = 0; i < NK; i++) {
            int unlocked = charUnlocked(i);
            int sel = (i == charSel);
            float bounce = sel ? (4.0f * fabsf(sinf(tAnim * 5.0f))) : 0.0f;
            float scalePop = sel ? (1.0f + 0.08f * sinf(tAnim * 6.0f)) : 1.0f;
            int bw = (int)(60 * scalePop);
            int bh = (int)(72 * scalePop);
            int bx = 18 + i * 76 + (60 - bw) / 2;
            int by = 48 + (int)bounce + (72 - bh) / 2;

            if (sel) {
                /* parlayan secim halkasi */
                float glow = 0.5f + 0.5f * sinf(tAnim * 8.0f);
                rect(bx - 6, by - 6, bw + 12, bh + 12, mixc(RGB(255, 220, 40), RGB(255, 80, 20), glow));
                rect(bx - 3, by - 3, bw + 6, bh + 6, RGB(255, 100, 40));
            }
            rect(bx, by, bw, bh, unlocked ? RGBA(25, 45, 90, 245) : RGBA(25, 25, 30, 245));
            /* karakter rengi alt serit */
            rect(bx + 3, by + bh - 14, bw - 6, 11, unlocked ? kBody[i] : RGB(55, 55, 60));
            if (unlocked && texFace[i][0] && texFace[i][0]->ready) {
                int ff = ((int)(tAnim * 2.8f + i) % 3);
                if (!texFace[i][ff] || !texFace[i][ff]->ready) ff = 0;
                int fw = bw - 8, fh = bh - 18;
                drawTexRect(texFace[i][ff], bx + 4, by + 3, fw, fh, 0, 0,
                    (float)texFace[i][ff]->w, (float)texFace[i][ff]->h, RGBA(255,255,255,255));
            } else {
                rect(bx + 10, by + 14, bw - 20, bh - 40, RGB(50, 50, 58));
                text(bx + 14, by + 26, 8, 14, 2, "?", RGB(120, 120, 130));
            }
            if (!unlocked) {
                rect(bx, by, bw, bh, RGBA(0, 0, 0, 150));
                text(bx + 6, by + bh / 2 - 6, 7, 12, 2, "LOCK", RGB(255, 90, 90));
            }
        }

        /* secili isim + sinif + tagline + statlar */
        if (charUnlocked(charSel)) {
            float np = 1.0f + 0.05f * sinf(tAnim * 5.0f);
            int nw = (int)(15 * np), nh = (int)(24 * np);
            text(W / 2 - textWidth(charNames[charSel], nw, 5) / 2, 128, nw, nh, 5, charNames[charSel], RGB(255, 255, 255));
            /* agirlik sinifi rengi */
            {
                unsigned cc = RGB(120, 220, 255);
                if (charClass[charSel][0] == 'H') cc = RGB(255, 120, 60);      /* HEAVY */
                else if (charClass[charSel][0] == 'L') cc = RGB(80, 240, 120); /* LIGHT */
                text(W / 2 - textWidth(charClass[charSel], 8, 2) / 2, 152, 8, 12, 2, charClass[charSel], cc);
                text(W / 2 - textWidth(charTagline[charSel], 7, 2) / 2, 166, 7, 11, 2, charTagline[charSel], RGB(255, 230, 120));
            }
            {
                static const char *lbl[3] = { "SPEED", "ACCEL", "HANDLING" };
                float vals[3] = { cSpd[charSel], cAcc[charSel], cTrn[charSel] };
                unsigned barCols[3] = { RGB(255, 80, 60), RGB(80, 220, 100), RGB(80, 180, 255) };
                for (int j = 0; j < 3; j++) {
                    int by2 = 182 + j * 18;
                    text(80, by2, 7, 12, 2, lbl[j], RGB(220, 230, 245));
                    rect(170, by2 + 1, 200, 11, RGBA(255, 255, 255, 35));
                    float f = clampf((vals[j] - 0.80f) / 0.42f, 0.05f, 1.0f);
                    float fp = f * (0.92f + 0.08f * sinf(tAnim * 3.0f + j));
                    rect(170, by2 + 1, (int)(200.0f * fp), 11, barCols[j]);
                }
            }
        } else {
            text(W / 2 - textWidth("LOCKED", 16, 5) / 2, 150, 16, 26, 5, "LOCKED", RGB(255, 90, 90));
        }
        /* yanip sonen ipucu */
        if (((int)(tAnim * 2.5f)) & 1)
            text(W / 2 - textWidth("< > SEC   X ONAY   O GERI", 8, 2) / 2, 250, 8, 14, 2, "< > SEC   X ONAY   O GERI", RGB(255, 230, 120));
        return;
    }
    /* Pist secimi (arkada pist onizlemesi) */
    if (state == STATE_TRACK) {
        rect(0, 0, W, 66, RGBA(0, 0, 0, 170));
        rect(0, H - 76, W, 76, RGBA(0, 0, 0, 170));
        text(W / 2 - textWidth("PIST SEC", 14, 4) / 2, 8, 14, 24, 4, "PIST SEC", RGB(255, 220, 50));
        snprintf(buf, sizeof(buf), "%d/%d  %s", trackSel + 1, NTRACKS, themes[trackSel].name);
        text(W / 2 - textWidth(buf, 10, 3) / 2, 40, 10, 16, 3, buf, trackUnlocked(trackSel) || !careerActive ? RGB(255,255,255) : RGB(255,90,90));
        snprintf(buf, sizeof(buf), "%s  %s", diffNames[diffSel], mirrorMode?"MIRROR":"NORMAL");
        text(W / 2 - textWidth(buf, 8, 2) / 2, 60, 8, 12, 2, buf, RGB(255,220,120));
        {
            float bt = (gameMode == GAME_RACE) ? saveData.bestRace[trackSel] : saveData.bestTT[trackSel];
            if (bt > 0) {
                int bm = (int)(bt / 60), bs = (int)bt % 60, bd = (int)(bt * 10) % 10;
                snprintf(buf, sizeof(buf), "BEST %d:%02d.%d", bm, bs, bd);
            } else snprintf(buf, sizeof(buf), "BEST --");
            text(W / 2 - textWidth(buf, 10, 3) / 2, H - 70, 10, 16, 3, buf, RGB(120, 240, 255));
            if (gameMode == GAME_TIME_TRIAL && ghostBestCount > 0)
                text(W / 2 - textWidth("GHOST VAR", 8, 2) / 2, H - 48, 8, 14, 2, "GHOST VAR", RGB(255, 230, 120));
        }
        text(W / 2 - textWidth("SOL SAG: PIST   X: BASLA   O: GERI", 7, 2) / 2, H - 22, 7, 12, 2, "SOL SAG: PIST   X: BASLA   O: GERI", RGB(205, 220, 235));
        return;
    }
    /* Ana menu — animasyonlu Mario Kart tarzi */
    if (state == STATE_MENU) {
        drawMenuBG();
        /* ust kirmizi banner + kayan dama */
        rect(0, 0, W, 40, RGB(210, 25, 35));
        float chk = fmodf(tAnim * 36.0f, 14.0f);
        for (int cx = -14; cx < W + 14; cx += 14) {
            int on = (((int)((cx + chk) / 14)) & 1);
            rect(cx + chk, 40, 14, 7, on ? RGB(245, 245, 245) : RGB(15, 15, 15));
        }
        /* baslik pulse */
        float mp = 1.0f + 0.07f * sinf(tAnim * 3.5f);
        int mw = (int)(18 * mp), mh = (int)(26 * mp);
        text(W / 2 - textWidth("MARIO KART", mw, 6) / 2, 4, mw, mh, 6, "MARIO KART", RGB(255, 240, 60));
        text(W / 2 - textWidth("PSP", 9, 3) / 2, 26, 9, 12, 3, "PSP", RGB(255, 200, 80));

        /* ziplayan mantarlar */
        float mb = 3.0f * fabsf(sinf(tAnim * 4.0f));
        drawMush(10, 52 - mb, 9);
        drawMush(W - 34, 52 - mb * 0.8f, 9);

        {
            static const char *names[5] = { "VS RACE", "TIME TRIAL", "GRAND PRIX", "CAREER", "RECORDS" };
            static const unsigned accent[5] = {
                RGB(230, 50, 50), RGB(50, 160, 255), RGB(255, 200, 40), RGB(80, 210, 90), RGB(180, 120, 255)
            };
            for (int i = 0; i < 5; i++) {
                int dist = i - menuSel;
                if (dist < 0) dist = -dist;
                /* secili satir kayar ve buyur; digerleri hafif solar */
                float slide = (i == menuSel) ? (6.0f * sinf(tAnim * 5.0f)) : 0.0f;
                float pop = (i == menuSel) ? (1.0f + 0.04f * sinf(tAnim * 6.0f)) : 1.0f;
                int by = 54 + i * 30 + (int)(slide * 0.15f);
                int boxW = (int)(260 * pop);
                int boxH = (int)(24 * pop);
                int bx = W / 2 - boxW / 2;
                int sel = (menuSel == i);

                if (sel) {
                    float glow = 0.5f + 0.5f * sinf(tAnim * 7.0f);
                    rect(bx - 6, by - 4, boxW + 12, boxH + 8, mixc(RGB(255, 220, 40), accent[i], glow));
                    rect(bx - 3, by - 2, boxW + 6, boxH + 4, accent[i]);
                }
                rect(bx, by, boxW, boxH, sel ? RGBA(30, 40, 90, 250) : RGBA(20, 35, 70, 160 + (40 - dist * 10)));
                rect(bx, by, 8, boxH, accent[i]);
                /* secili item hafif saga kayar */
                int tx = bx + boxW / 2 - textWidth(names[i], 11, 3) / 2 + (sel ? (int)slide : 0);
                text(tx, by + 3, 11, 18, 3, names[i],
                     sel ? RGB(255, 255, 255) : RGB(160, 175, 200));
            }
        }
        if (((int)(tAnim * 2.2f)) & 1)
            text(W / 2 - textWidth("YUKARI/ASAGI  X: SEC", 8, 2) / 2, 220, 8, 14, 2, "YUKARI/ASAGI  X: SEC", RGB(120, 240, 255));
        if (menuSel == GAME_TIME_TRIAL) text(W/2-textWidth("BEAT YOUR GHOST",9,3)/2,242,9,14,3,"BEAT YOUR GHOST",RGB(255,230,120));
        else if (menuSel == GAME_CHAMPIONSHIP) text(W/2-textWidth("4 RACES  WIN THE CUP",8,2)/2,242,8,14,2,"4 RACES  WIN THE CUP",RGB(255,230,120));
        else if (menuSel == GAME_CAREER) text(W/2-textWidth("UNLOCK CHARACTERS",8,2)/2,242,8,14,2,"UNLOCK CHARACTERS",RGB(255,230,120));
        else text(W/2-textWidth("RACE AGAINST 5 CPUS",8,2)/2,242,8,14,2,"RACE AGAINST 5 CPUS",RGB(255,230,120));
        return;
    }
    /* Mario Kart tarzi sira: 1st / 2nd / 3rd */
    if (gameMode == GAME_RACE) {
        int rank = (state == 2) ? finalRank : calcRank();
        const char *ord = (rank == 1) ? "1st" : (rank == 2) ? "2nd" : (rank == 3) ? "3rd" :
                          (rank == 4) ? "4th" : (rank == 5) ? "5th" : "6th";
        unsigned rc = rank == 1 ? RGB(255, 215, 40) : (rank == 2 ? RGB(220, 225, 235) : (rank == 3 ? RGB(220, 140, 60) : RGB(255, 255, 255)));
        text(8, 6, 16, 28, 4, ord, rc);
        snprintf(buf, sizeof(buf), "/%d", NK);
        text(8 + textWidth(ord, 16, 4) + 2, 16, 8, 14, 2, buf, RGB(180, 200, 220));
    } else {
        text(8, 8, 8, 14, 2, "TIME TRIAL", RGB(255, 215, 40));
        if (saveData.bestTT[trackSel] > 0) {
            float btt = saveData.bestTT[trackSel];
            int bm = (int)(btt / 60), bs = (int)btt % 60, bd = (int)(btt * 10) % 10;
            snprintf(buf, sizeof(buf), "REC %d:%02d.%d", bm, bs, bd);
            text(8, 28, 8, 14, 2, buf, RGB(120, 220, 255));
        }
    }
    if (saveData.bestLap[trackSel] > 0) snprintf(buf, sizeof(buf), "BEST %.1f", saveData.bestLap[trackSel]);
    else snprintf(buf, sizeof(buf), "BEST --");
    text(8, 42, 7, 12, 2, buf, RGB(200, 210, 230));
    if(isChampMode()) { snprintf(buf,sizeof(buf),"%s %d/%d",cupNames[cupSel],champRace+1,CHAMP_RACES); text(8, 56, 7, 12, 2, buf, RGB(255,220,100)); }
    else { snprintf(buf,sizeof(buf),"%s",diffNames[diffSel]); text(8, 56, 7, 12, 2, buf, RGB(255,220,100)); }
    /* LAP sayaci - sag ust, kirmizi kutu */
    {
        rect(W - 78, 4, 74, 28, RGBA(180, 30, 40, 210));
        snprintf(buf, sizeof(buf), "LAP");
        text(W - 72, 6, 6, 10, 2, buf, RGB(255, 200, 200));
        snprintf(buf, sizeof(buf), "%d/%d", lap, LAPS);
        text(W - 72, 16, 10, 16, 3, buf, RGB(255, 255, 255));
    }

    float t = (state == 2) ? finalTime : raceTime;
    int m = (int)(t / 60), s = (int)t % 60, d = (int)(t * 10) % 10;
    snprintf(buf, sizeof(buf), "%d:%02d.%d", m, s, d);
    text(W / 2 - textWidth(buf, 10, 3) / 2, 8, 10, 16, 3, buf, RGB(255, 255, 255));

    /* minimap - dama cerceveli */
    {
        float sx = 92.0f / (mapMaxX - mapMinX), sz = 66.0f / (mapMaxZ - mapMinZ);
        float sc = sx < sz ? sx : sz;
        float ox = W - 104 + 4, oy = 40 + 3;
        rect(W - 106, 38, 102, 78, RGB(200, 40, 40));
        rect(W - 104, 40, 98, 74, RGBA(10, 20, 40, 200));
        for (int i = 0; i < M; i += 3) rect(ox + (tcx[i] - mapMinX) * sc, oy + (tcz[i] - mapMinZ) * sc, 3, 3, RGB(220, 220, 230));
        rect(ox + (tcx[0] - mapMinX) * sc - 1, oy + (tcz[0] - mapMinZ) * sc - 1, 5, 5, RGB(255, 255, 255));
        for (int i = activeKarts() - 1; i >= 0; i--) {
            float mx = ox + (K[i].x - mapMinX) * sc, mz = oy + (K[i].z - mapMinZ) * sc;
            if (i == 0) rect(mx - 4, mz - 4, 9, 9, RGB(255, 255, 255));
            rect(mx - 2, mz - 2, 5, 5, kBody[kChar[i]]);
        }
    }

    /* Item slot: rulet veya sabit item (Mario Kart sol alt) */
    {
        int slotX = 6, slotY = H - 62;
        rect(slotX, slotY, 56, 56, RGB(200, 40, 40));
        rect(slotX + 3, slotY + 3, 50, 50, RGBA(20, 30, 60, 230));
        if (itemRouletteT > 0) {
            /* hizli donen item ikonlari */
            int spin = ((int)(itemRouletteT * 18.0f) % 7) + 1;
            float ip = 0.7f + 0.3f * sinf(tAnim * 20.0f);
            rect(slotX + 7, slotY + 7, 42, 42, scol(itemColor(spin), ip));
            drawAtlasIcon(atlasCellForItem(spin), slotX + 9, slotY + 9, 38, RGBA(255,255,255,255));
            if (((int)(tAnim * 8.0f)) & 1)
                text(slotX + 8, slotY + 44, 5, 9, 1, "???", RGB(255, 240, 80));
        } else if (itemCntG > 0 && p->item != ITEM_NONE) {
            float ip = 0.85f + 0.15f * (0.5f + 0.5f * sinf(tAnim * 8.0f));
            rect(slotX + 7, slotY + 7, 42, 42, scol(itemColor(p->item), ip));
            drawAtlasIcon(atlasCellForItem(p->item), slotX + 9, slotY + 9, 38, RGBA(255,255,255,255));
            text(slotX + 64, slotY + 8, 7, 12, 2, itemName(p->item), RGB(255, 255, 255));
            text(slotX + 64, slotY + 28, 6, 11, 2, "O: USE", RGB(255, 230, 70));
        } else {
            text(slotX + 14, slotY + 20, 8, 14, 2, "?", RGB(100, 110, 140));
        }
    }
    if (p->shield) {
        rect(70, H - 28, 60, 16, RGBA(30, 80, 120, 200));
        text(74, H - 26, 7, 12, 2, "BUBBLE", RGB(80, 230, 255));
    }
    if (p->starT > 0) {
        rect(70, H - 48, 50, 16, RGBA(80, 50, 10, 200));
        text(74, H - 46, 7, 12, 2, "STAR!", RGB(255, 200, 40));
    }

    /* PSP kontrol ipucu: kalabalik HUD yerine alt satirda kisa tut. */
    if (state == 1 && (((int)(tAnim * 1.5f)) & 3) == 0)
        text(8, H - 20, 7, 12, 2, "O:ITEM  T:CAM  L/R:DRIFT", RGB(205, 220, 235));

    /* hiz */
    float lim = VMAX * 1.35f;
    for (int i = 0; i < 20; i++) {
        int on = (p->spd / lim * 20.0f) > i;
        unsigned c = on ? mixc(RGB(60, 220, 60), RGB(240, 50, 40), i / 19.0f) : RGBA(0, 0, 0, 130);
        float hh = 5 + i * 0.6f;
        rect(W - 130 + i * 6, H - 12 - hh, 4, hh, c);
    }
    snprintf(buf, sizeof(buf), "%d", (int)(p->spd / VMAX * 180.0f));
    text(W - 130, H - 42, 11, 18, 3, buf, p->boostT > 0 ? RGB(255, 190, 40) : RGB(255, 255, 255));

    /* drift gostergesi */
    if (p->driftDir) {
        float f = clampf(p->driftT / 1.8f, 0, 1);
        unsigned c = p->driftT > 1.8f ? RGB(255, 150, 20) : (p->driftT > 0.8f ? RGB(60, 150, 255) : RGB(230, 230, 230));
        rect(W / 2 - 42, H - 26, 84, 10, RGBA(0, 0, 0, 160));
        rect(W / 2 - 40, H - 24, 80 * f, 6, c);
    }

    /* turbo hiz cizgileri + kenar vignette */
    if (boostLines > 0.05f && state != 0) {
        int alpha = (int)(70 * boostLines);
        for (int i = 0; i < 18; i++) {
            int side = i & 1;
            int y = (i * 37 + (int)(tAnim * 900)) % H;
            int len = 40 + (i % 5) * 12;
            rect(side ? W - len : 0, y, len, 2 + (i % 2), RGBA(255, 240, 180, alpha + (i % 3) * 15));
        }
        /* ust/alt turuncu sis */
        int va = (int)(50 * boostLines);
        rect(0, 0, W, 18, RGBA(255, 120, 20, va));
        rect(0, H - 18, W, 18, RGBA(255, 120, 20, va));
    }
    /* boost / hit ekran flasi */
    if (screenFlash > 0.02f) {
        int a = (int)(clampf(screenFlash, 0, 1) * 90);
        rect(0, 0, W, H, RGBA(255, 230, 120, a));
    }

    /* geri sayim — daha buyuk pulse */
    if (state == 0) {
        int lit = (cd < 3.0f) ? ((cd < 2.0f) ? 3 : 2) : 1;
        for (int i = 0; i < 3; i++) {
            float lp = (i < lit) ? (1.0f + 0.12f * sinf(tAnim * 16.0f + i)) : 1.0f;
            int s = (int)(28 * lp);
            int ox = W / 2 - 55 + i * 38 + (28 - s) / 2;
            int oy = 50 + (28 - s) / 2;
            rect(ox - 2, oy - 2, s + 4, s + 4, RGB(40, 40, 40));
            rect(ox, oy, s, s, (i < lit) ? RGB(240, 40, 40) : RGB(80, 20, 20));
        }
        int dg = (int)ceilf(cd) - 1;
        if (dg >= 1 && dg <= 3) {
            snprintf(buf, sizeof(buf), "%d", dg);
            float pulse = 1.0f + 0.14f * sinf(tAnim * 16.0f);
            int nw = (int)(52 * pulse), nh = (int)(86 * pulse);
            text(W / 2 - nw / 2, 92 - (nh - 86) / 2, nw, nh, 12, buf, RGB(255, 230, 60));
        }
    } else if (state == 1 && raceTime < 1.2f) {
        for (int i = 0; i < 3; i++) {
            rect(W / 2 - 55 + i * 38, 50, 28, 28, RGB(20, 20, 20));
            rect(W / 2 - 52 + i * 38, 53, 22, 22, RGB(50, 235, 70));
        }
        float gp = 1.0f + 0.12f * sinf(tAnim * 18.0f);
        int gw = (int)(44 * gp), gh = (int)(76 * gp);
        text(W / 2 - gw / 2, 96 - (gh - 76) / 2, gw, gh, 11, "GO", RGB(60, 240, 80));
        if (raceTime < 0.15f) screenFlash = 0.4f;
    }
    if (lapFlash > 0 && state == 1) {
        char b2[20];
        if (lap == LAPS) snprintf(b2, sizeof(b2), "FINAL LAP"); else snprintf(b2, sizeof(b2), "LAP %d", lap);
        float lp = 1.0f + 0.08f * sinf(tAnim * 10.0f);
        int lw = (int)(18 * lp), lh = (int)(30 * lp);
        text(W / 2 - textWidth(b2, lw, 5) / 2, 70, lw, lh, 5, b2, RGB(255, 230, 60));
    }

    /* duraklatma ekrani */
    if (state == STATE_PAUSE) {
        rect(0, 72, W, 142, RGBA(0, 0, 0, 175));
        text(W / 2 - textWidth("PAUSE", 30, 8) / 2, 84, 30, 50, 8, "PAUSE", RGB(255, 230, 70));
        text(W / 2 - textWidth("START:DEVAM", 10, 3) / 2, 148, 10, 16, 3, "START:DEVAM", RGB(255, 255, 255));
        text(W / 2 - textWidth("SELECT:YENI", 10, 3) / 2, 172, 10, 16, 3, "SELECT:YENI", RGB(180, 220, 255));
        text(W / 2 - textWidth(aMusicOn ? "T:MUZIK ACIK" : "T:MUZIK KAPALI", 10, 3) / 2, 194, 10, 16, 3, aMusicOn ? "T:MUZIK ACIK" : "T:MUZIK KAPALI", RGB(255, 230, 120));
    }

    /* bitis ekrani — Mario Kart tarzi */
    if (state == 2) {
        rect(0, 60, W, 165, RGBA(0, 0, 0, 170));
        /* dama ust serit */
        for (int cx = 0; cx < W; cx += 16) {
            int on = ((cx / 16) + ((int)(tAnim * 4) % 2)) & 1;
            rect(cx, 60, 16, 8, on ? RGB(245, 245, 245) : RGB(20, 20, 20));
        }
        text(W / 2 - textWidth("FINISH!", 28, 7) / 2, 78, 28, 44, 7, "FINISH!", RGB(255, 240, 80));
        {
            const char *ord = finalRank == 1 ? "1st" : finalRank == 2 ? "2nd" : finalRank == 3 ? "3rd" :
                              finalRank == 4 ? "4th" : finalRank == 5 ? "5th" : "6th";
            unsigned medal = finalRank == 1 ? RGB(255, 215, 40) : (finalRank == 2 ? RGB(210, 215, 225) : (finalRank == 3 ? RGB(215, 140, 70) : RGB(255, 255, 255)));
            float pp = 1.0f + 0.06f * sinf(tAnim * 5.0f);
            int ow = (int)(28 * pp), oh = (int)(48 * pp);
            text(W / 2 - textWidth(ord, ow, 6) / 2, 130, ow, oh, 6, ord, medal);
        }
        if (newRecord && (((int)(tAnim * 3)) & 1))
            text(W / 2 - textWidth("RECORD", 11, 3) / 2, 46, 11, 18, 3, "RECORD", RGB(255, 220, 50));
        if(isChampMode()) {
            const char *mn = champMedal==3?"GOLD":(champMedal==2?"SILVER":"BRONZE");
            text(W/2-textWidth(mn,12,4)/2,46,12,22,4,mn,champMedal==3?RGB(255,215,40):(champMedal==2?RGB(215,220,230):RGB(205,140,80)));
            snprintf(buf,sizeof(buf),"CUP POINTS %d",champPoints[0]);
            text(W/2-textWidth(buf,9,3)/2,124,9,16,3,buf,RGB(120,240,255));
        }
        if (((int)(tAnim * 2)) & 1)
            text(W / 2 - textWidth("PRESS START", 11, 3) / 2, 194, 11, 18, 3, "PRESS START", RGB(255, 255, 255));
    }
    if (state == STATE_CUPRESULT) {
        rect(0,64,W,160,RGBA(0,0,0,185));
        snprintf(buf,sizeof(buf),"%s  %d/%d",cupNames[cupSel],champRace,CHAMP_RACES);
        text(W/2-textWidth(buf,12,4)/2,76,12,22,4,buf,RGB(255,220,50));
        snprintf(buf,sizeof(buf),"RACE PLACE %d",finalRank);
        text(W/2-textWidth(buf,11,3)/2,112,11,18,3,buf,RGB(255,255,255));
        snprintf(buf,sizeof(buf),"POINTS %d",champPoints[0]);
        text(W/2-textWidth(buf,11,3)/2,138,11,18,3,buf,RGB(120,240,255));
        if(champRace<CHAMP_RACES) text(W/2-textWidth("START/X: NEXT RACE",10,3)/2,176,10,16,3,"START/X: NEXT RACE",RGB(255,230,120));
    }
}

static void render(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xff000000);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    render3D();
    drawHUD();
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void initGraphics(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUFW);
    sceGuDispBuffer(W, H, (void *)0x88000, BUFW);
    sceGuDepthBuffer((void *)0x110000, BUFW);
    sceGuOffset(2048 - (W / 2), 2048 - (H / 2));
    sceGuViewport(2048, 2048, W, H);
    sceGuDepthRange(0xc350, 0x2710);
    sceGuScissor(0, 0, W, H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFog(200.0f, 900.0f, HAZE);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(void) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    loadSave();
    charSel = saveData.lastChar;
    if (charSel < 0 || charSel >= NK || !charUnlocked(charSel)) charSel = 0;
    trackSel = saveData.lastTrack;
    if (trackSel < 0 || trackSel >= NTRACKS) trackSel = 0;
    makeTextures();
    makeKartFallbackTextures();
    textureAssetCount = loadTextureBank();
    audioAssetCount = loadAudioBank();
    startAudio();
    initGraphics();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    rebuildAll();
    state = STATE_MENU;
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    while (1) {
        sceCtrlPeekBufferPositive(&pad, 1);
        update(&pad);
        /* ses durumunu ses thread'ine bildir */
        aSpd = K[0].spd;
        aBoost = K[0].boostT > 0;
        aEngineOn = (state == 0 || state == 1);
        engineBand = (int)clampf((K[0].spd / VMAX) * 4.0f, 0.0f, 3.0f);
        aTrackId = trackSel;
        aMusicScene = (state==STATE_MENU || state==STATE_CHAR || state==STATE_TRACK || state==STATE_DIFF || state==STATE_CUPSELECT || state==STATE_STATS) ? 0 : ((state==2 || state==STATE_CUPRESULT) ? 2 : 1);
        render();
    }
    return 0;
}