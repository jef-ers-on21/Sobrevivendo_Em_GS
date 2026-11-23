/*=====================================================================
  SOBREVIVENDO EM GS  -  Jogo de sobrevivencia zumbi para PSP / PPSSPP
  ---------------------------------------------------------------------
  - Camera em primeira pessoa (3D com sceGu), mapa 800x800 m
  - Mira dinamica por arma, 3 armas, recarga, medkit
  - Carros com as texturas do arquivo carros.png (cars_data.h)
  - Sobreviventes para recrutar (seguem e atiram nos zumbis)
  - Loja no menu (armas, itens e carros) com dinheiro e save
  - Camada de rede preparada para multiplayer (pacotes + loopback)

  Controles (no jogo):
    Analogico ........ andar / (no carro: acelerar e esterçar)
    D-Pad ............ olhar (esq/dir = girar, cima/baixo = inclinar)
    R ................ atirar        L ........ mirar (zoom)
    Quadrado ......... recarregar    (no carro: freio de mao)
    Circulo .......... trocar arma   X ........ correr
    Triangulo ........ interagir (recrutar / entrar / sair do carro)
    Select ........... usar kit medico        Start .... pausa
=====================================================================*/
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psputils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "cars_data.h"

PSP_MODULE_INFO("SobrevivendoEmGS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

/*------------------------------ constantes --------------------------*/
#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define FRAME_SIZE (BUF_W * SCR_H * 4)
#define PI 3.14159265f

#define WORLD_HALF 400.0f
#define MAX_ZOMBIES 40
#define MAX_CARS 40
#define MAX_SURV 16
#define MAX_BLD 440
#define MAX_TREES 700
#define MAX_CAMPS 16
#define MAX_FOLLOW 5

#define RGB(r, g, b) (0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define RGBA(r, g, b, a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))

#define FOG_COL RGB(96, 102, 112)

static unsigned int __attribute__((aligned(16))) dlist[524288];
static int draw_off = 0;

/*------------------------------ utilidades --------------------------*/
static unsigned int rng_state = 12345u;
static unsigned int rnd_u(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state; }
static float frand(void) { return (float)(rnd_u() >> 8) / 16777216.0f; }
static float frange(float a, float b) { return a + (b - a) * frand(); }
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float dist2(float ax, float az, float bx, float bz) { float dx = ax - bx, dz = az - bz; return dx * dx + dz * dz; }

static unsigned int shade(unsigned int c, float k)
{
    int r = (int)((c & 255) * k), g = (int)(((c >> 8) & 255) * k), b = (int)(((c >> 16) & 255) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000u) | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}

/*------------------------------ callbacks ---------------------------*/
static int exit_callback(int a1, int a2, void *arg) { (void)a1; (void)a2; (void)arg; sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    (void)args; (void)argp;
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

/*------------------------------ dados dos carros --------------------*/
typedef struct { const char *name; float vmax; float acc; int hp; int price; } CarDef;
static const CarDef CARS[CAR_COUNT] = {
    {"Uno",                 28, 13, 100,     0},
    {"Gol GTS",             32, 14, 100,  1800},
    {"Opala",               34, 13, 120,  2500},
    {"Maverick",            38, 16, 120,  4000},
    {"Chevrolet D20",       29, 11, 170,  3000},
    {"Hilux",               33, 13, 180,  4500},
    {"F-1000",              28, 11, 180,  3200},
    {"Fusca",               24,  9,  90,   900},
    {"Kombi",               23,  9, 140,  1500},
    {"Sprinter",            27, 10, 160,  2800},
    {"Pickup Blindada",     31, 12, 320,  9000},
    {"Buggy",               36, 17,  80,  5000},
    {"Jeep",                31, 14, 150,  4000},
    {"Trator",              15,  7, 220,  2000},
    {"Monster Truck",       29, 12, 350, 12000},
    {"Viatura Policial",    38, 16, 160,  7000},
    {"Ambulancia",          31, 12, 150,  6000},
    {"Guincho",             24, 10, 200,  3500},
    {"Caminhao Militar",    26, 10, 280,  8000},
    {"Caminhao Combustivel",22,  9, 180,  5500},
};

/*------------------------------ armas -------------------------------*/
typedef struct { const char *name; float dmg; int pellets; float spread; float rate; int mag; float range; int automatic; float recoil; } WeaponDef;
static const WeaponDef WEAP[3] = {
    {"PISTOLA",    34, 1, 0.010f, 0.28f, 12,  90, 0, 0.035f},
    {"ESPINGARDA", 16, 9, 0.060f, 0.85f,  6,  40, 0, 0.120f},
    {"RIFLE",      26, 1, 0.018f, 0.11f, 30, 130, 1, 0.018f},
};
static const int CROSS_GAP[3] = {5, 15, 7};

/*------------------------------ texturas ----------------------------*/
static unsigned int __attribute__((aligned(16))) tex_white[8 * 8];
static unsigned int __attribute__((aligned(16))) tex_ground[64 * 64];
static unsigned int __attribute__((aligned(16))) tex_road[64 * 64];
static unsigned int __attribute__((aligned(16))) tex_wall[64 * 64];
enum { T_WHITE = 0, T_GROUND, T_ROAD, T_WALL, T_CAR };

static int hash2(int x, int y)
{
    unsigned int h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h ^ (h >> 16)) & 255u);
}

static void gen_textures(void)
{
    int x, y;
    for (x = 0; x < 64; x++) tex_white[x] = 0xFFFFFFFFu;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int n = hash2(x, y) - 128;
            int g = hash2(x / 3, y / 3) - 128;
            /* chao: terra/grama suja */
            int r = 78 + n / 10 + g / 8, gg = 84 + n / 9 + g / 8, b = 56 + n / 12;
            tex_ground[y * 64 + x] = RGB(clampf(r, 0, 255), clampf(gg, 0, 255), clampf(b, 0, 255));
            /* asfalto com faixas */
            int a = 58 + n / 14;
            unsigned int col = RGB(a, a, a + 4);
            if (x == 3 || x == 60) col = RGB(190, 190, 185);
            if ((x == 31 || x == 32) && (y % 32) < 16) col = RGB(205, 170, 40);
            tex_road[y * 64 + x] = col;
            /* parede com tijolos e janelas */
            int row = y / 8, off = (row & 1) * 8;
            int base = 185 + n / 12;
            unsigned int w = RGB(base, base - 6, base - 16);
            if ((y % 8) == 0 || ((x + off) % 16) == 0) w = RGB(140, 132, 120);
            if (((x >= 8 && x < 24) || (x >= 40 && x < 56)) && ((y >= 10 && y < 26) || (y >= 38 && y < 54))) {
                int lit = hash2(x / 16, y / 28) < 40;
                w = lit ? RGB(210, 190, 110) : RGB(38 + n / 20, 48 + n / 20, 60 + n / 20);
                if (x % 16 == 8 || y % 28 == 10) w = RGB(90, 86, 80);
            }
            tex_wall[y * 64 + x] = w;
        }
    sceKernelDcacheWritebackAll();
}

static void tex_bind(int id, int repeat, int linear)
{
    const void *p;
    int w = 64, h = 64, fmt = GU_PSM_8888;
    switch (id) {
    case T_WHITE: p = tex_white; w = 8; h = 8; break;
    case T_GROUND: p = tex_ground; break;
    case T_ROAD: p = tex_road; break;
    case T_WALL: p = tex_wall; break;
    default: p = cars_data[id - T_CAR]; w = CAR_TEX_W; h = CAR_TEX_H; fmt = GU_PSM_5551; break;
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(fmt, 0, 0, 0);
    sceGuTexImage(0, w, h, w, p);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
    sceGuTexWrap(repeat ? GU_REPEAT : GU_CLAMP, repeat ? GU_REPEAT : GU_CLAMP);
}

/*------------------------------ vertices / camera -------------------*/
typedef struct { float u, v; unsigned int c; float x, y, z; } Vtx;
#define VT3D (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define VT2D (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

static float cam_x, cam_y, cam_z, cam_cs = 1, cam_sn = 0, cam_cp = 1, cam_sp = 0;
static int g_cs = 0; /* 1 = vertices ja estao em espaco de camera (view model) */

static void cam_set(float x, float y, float z, float yaw, float pitch)
{
    cam_x = x; cam_y = y; cam_z = z;
    cam_cs = cosf(yaw); cam_sn = sinf(yaw);
    cam_cp = cosf(pitch); cam_sp = sinf(pitch);
}

/* convencao: frente = (-sin(yaw), -cos(yaw)), direita = (cos(yaw), -sin(yaw)) */
static void setv(Vtx *v, float wx, float wy, float wz, float u, float t, unsigned int c)
{
    float dx, dy, dz, fh;
    v->u = u; v->v = t; v->c = c;
    if (g_cs) { v->x = wx; v->y = wy; v->z = wz; return; }
    dx = wx - cam_x; dy = wy - cam_y; dz = wz - cam_z;
    fh = -dx * cam_sn - dz * cam_cs;
    v->x = dx * cam_cs - dz * cam_sn;
    v->y = dy * cam_cp - fh * cam_sp;
    v->z = -(fh * cam_cp + dy * cam_sp);
}

static int visible(float x, float z, float r)
{
    float dx = x - cam_x, dz = z - cam_z, rx, fh;
    if (dx * dx + dz * dz < 144.0f) return 1;
    rx = dx * cam_cs - dz * cam_sn;
    fh = -dx * cam_sn - dz * cam_cs;
    if (fh < -r) return 0;
    if (fabsf(rx) > fh * 1.9f + r) return 0;
    return 1;
}

static Vtx *put_quad(Vtx *v, const float *p, float u0, float v0, float u1, float v1, unsigned int c)
{
    setv(&v[0], p[0], p[1], p[2], u0, v1, c);
    setv(&v[1], p[3], p[4], p[5], u1, v1, c);
    setv(&v[2], p[6], p[7], p[8], u1, v0, c);
    setv(&v[3], p[0], p[1], p[2], u0, v1, c);
    setv(&v[4], p[6], p[7], p[8], u1, v0, c);
    setv(&v[5], p[9], p[10], p[11], u0, v0, c);
    return v + 6;
}

static void quad1(const float *p, float u0, float v0, float u1, float v1, unsigned int c)
{
    Vtx *v = (Vtx *)sceGuGetMemory(6 * sizeof(Vtx));
    put_quad(v, p, u0, v0, u1, v1, c);
    sceGuDrawArray(GU_TRIANGLES, VT3D, 6, 0, v);
}

/* caixa (sem fundo) com rotacao em Y; cx,cz = centro, by = base */
static void box(float cx, float by, float cz, float sx, float sy, float sz, float yaw, unsigned int col)
{
    static const float k[4] = {0.78f, 0.62f, 0.88f, 0.70f};
    float c = cosf(yaw), s = sinf(yaw), hx = sx * 0.5f, hz = sz * 0.5f, y1 = by + sy;
    float lx[4], lz[4], X[4], Z[4];
    Vtx *v, *w;
    int i, j;
    lx[0] = -hx; lx[1] = hx; lx[2] = hx; lx[3] = -hx;
    lz[0] = -hz; lz[1] = -hz; lz[2] = hz; lz[3] = hz;
    for (i = 0; i < 4; i++) { X[i] = cx + lx[i] * c + lz[i] * s; Z[i] = cz - lx[i] * s + lz[i] * c; }
    v = (Vtx *)sceGuGetMemory(30 * sizeof(Vtx));
    w = v;
    for (i = 0; i < 4; i++) {
        float p[12];
        j = (i + 1) & 3;
        p[0] = X[i]; p[1] = by; p[2] = Z[i]; p[3] = X[j]; p[4] = by; p[5] = Z[j];
        p[6] = X[j]; p[7] = y1; p[8] = Z[j]; p[9] = X[i]; p[10] = y1; p[11] = Z[i];
        w = put_quad(w, p, 0, 0, 1, 1, shade(col, k[i]));
    }
    {
        float p[12];
        p[0] = X[0]; p[1] = y1; p[2] = Z[0]; p[3] = X[1]; p[4] = y1; p[5] = Z[1];
        p[6] = X[2]; p[7] = y1; p[8] = Z[2]; p[9] = X[3]; p[10] = y1; p[11] = Z[3];
        put_quad(w, p, 0, 0, 1, 1, col);
    }
    sceGuDrawArray(GU_TRIANGLES, VT3D, 30, 0, v);
}

static void box_rel(float px, float pz, float yaw, float ox, float by, float oz, float sx, float sy, float sz, unsigned int col)
{
    float c = cosf(yaw), s = sinf(yaw);
    box(px + ox * c + oz * s, by, pz - ox * s + oz * c, sx, sy, sz, yaw, col);
}

/*------------------------------ 2D / texto --------------------------*/
static void begin2d(void)
{
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

static void rect(float x, float y, float w, float h, unsigned int col)
{
    Vtx *v;
    sceGuDisable(GU_TEXTURE_2D);
    v = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    v[0].u = 0; v[0].v = 0; v[0].c = col; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].u = 0; v[1].v = 0; v[1].c = col; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, VT2D, 2, 0, v);
}

static void gradient(float y0, float y1, unsigned int top, unsigned int bot)
{
    Vtx *v;
    sceGuDisable(GU_TEXTURE_2D);
    v = (Vtx *)sceGuGetMemory(4 * sizeof(Vtx));
    v[0].u = 0; v[0].v = 0; v[0].c = top; v[0].x = 0;     v[0].y = y0; v[0].z = 0;
    v[1].u = 0; v[1].v = 0; v[1].c = top; v[1].x = SCR_W; v[1].y = y0; v[1].z = 0;
    v[2].u = 0; v[2].v = 0; v[2].c = bot; v[2].x = 0;     v[2].y = y1; v[2].z = 0;
    v[3].u = 0; v[3].v = 0; v[3].c = bot; v[3].x = SCR_W; v[3].y = y1; v[3].z = 0;
    sceGuDrawArray(GU_TRIANGLE_STRIP, VT2D, 4, 0, v);
}

static void spr2d(int tex, float x, float y, float w, float h, int flip, unsigned int col)
{
    Vtx *v;
    tex_bind(tex, 0, 0);
    v = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    v[0].u = flip ? 1.0f : 0.0f; v[0].v = 0; v[0].c = col; v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].u = flip ? 0.0f : 1.0f; v[1].v = 1; v[1].c = col; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, VT2D, 2, 0, v);
}

/* texto: usa a fonte do pspdebug, impressa depois do GU terminar o quadro */
typedef struct { short x, y; unsigned int col; char s[72]; } TextItem;
static TextItem tq[110];
static int ntq = 0;

static void text(int x, int y, unsigned int col, const char *fmt, ...)
{
    va_list ap;
    if (ntq >= 110) return;
    va_start(ap, fmt);
    vsnprintf(tq[ntq].s, 72, fmt, ap);
    va_end(ap);
    tq[ntq].x = (short)x; tq[ntq].y = (short)y; tq[ntq].col = col;
    ntq++;
}

static void textc(int y, unsigned int col, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (ntq >= 110) return;
    va_start(ap, fmt);
    vsnprintf(tq[ntq].s, 72, fmt, ap);
    va_end(ap);
    n = (int)strlen(tq[ntq].s);
    tq[ntq].x = (short)((SCR_W - n * 8) / 2); tq[ntq].y = (short)y; tq[ntq].col = col;
    ntq++;
}

static void text_flush(void)
{
    int i;
    pspDebugScreenSetOffset(draw_off);
    for (i = 0; i < ntq; i++) {
        pspDebugScreenSetXY(tq[i].x / 8, tq[i].y / 8);
        pspDebugScreenSetTextColor(tq[i].col);
        pspDebugScreenPuts(tq[i].s);
    }
    ntq = 0;
}

/* fonte grande 3x5 (so as letras usadas nos titulos) */
static const char *glyph(char c)
{
    switch (c) {
    case 'S': return "111100111001111";
    case 'O': return "111101101101111";
    case 'B': return "110101110101110";
    case 'R': return "110101110101101";
    case 'E': return "111100110100111";
    case 'V': return "101101101101010";
    case 'I': return "111010010010111";
    case 'N': return "101111111111101";
    case 'D': return "110101101101110";
    case 'M': return "101111111101101";
    case 'G': return "111100101101111";
    case 'L': return "100100100100111";
    case 'J': return "001001001101111";
    case 'A': return "111101111101101";
    case 'C': return "111100100100111";
    case 'U': return "101101101101111";
    default: return NULL;
    }
}

static int big_w(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }

static void big_text(int x, int y, int sc, unsigned int col, const char *s)
{
    int pass, i, k;
    for (pass = 0; pass < 2; pass++) {
        int cx = x + (pass == 0 ? sc : 0), cy = y + (pass == 0 ? sc : 0);
        unsigned int cc = pass == 0 ? RGBA(0, 0, 0, 200) : col;
        for (i = 0; s[i]; i++) {
            const char *g = glyph(s[i]);
            if (g)
                for (k = 0; k < 15; k++)
                    if (g[k] == '1') rect((float)(cx + (k % 3) * sc), (float)(cy + (k / 3) * sc), (float)sc, (float)sc, cc);
            cx += 4 * sc;
        }
    }
}

/*------------------------------ entrada -----------------------------*/
typedef struct { unsigned int b, p; float mx, my, lx, ly; } Input;
static SceCtrlData pad;
static unsigned int pad_old = 0;

static float deadz(float v)
{
    if (fabsf(v) < 0.22f) return 0.0f;
    return (v > 0 ? v - 0.22f : v + 0.22f) / 0.78f;
}

static void read_input(Input *in)
{
    unsigned int b;
    sceCtrlPeekBufferPositive(&pad, 1);
    b = pad.Buttons;
    in->b = b; in->p = b & ~pad_old; pad_old = b;
    in->mx = deadz(((int)pad.Lx - 128) / 128.0f);
    in->my = -deadz(((int)pad.Ly - 128) / 128.0f);
    in->lx = (float)((b & PSP_CTRL_RIGHT) ? 1 : 0) - (float)((b & PSP_CTRL_LEFT) ? 1 : 0);
    in->ly = (float)((b & PSP_CTRL_UP) ? 1 : 0) - (float)((b & PSP_CTRL_DOWN) ? 1 : 0);
}

/*------------------------------ save --------------------------------*/
typedef struct {
    unsigned int magic; int money; int wown[3]; int cown[CAR_COUNT]; int selcar;
    int total[3]; int medkits; int best_wave; int best_kills;
} Save;
static Save sv;
#define SAVE_MAGIC 0x53475331u
#define SAVE_DIR "ms0:/PSP/GAME/SobrevivendoEmGS"
#define SAVE_PATH SAVE_DIR "/save.bin"

static void save_defaults(void)
{
    memset(&sv, 0, sizeof(sv));
    sv.magic = SAVE_MAGIC; sv.money = 500; sv.wown[0] = 1; sv.cown[0] = 1; sv.total[0] = 48; sv.medkits = 1;
}
static void save_load(void)
{
    int fd;
    save_defaults();
    fd = sceIoOpen(SAVE_PATH, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        Save t;
        int n = sceIoRead(fd, &t, sizeof(t));
        sceIoClose(fd);
        if (n == (int)sizeof(t) && t.magic == SAVE_MAGIC) sv = t;
    }
}
static void save_write(void)
{
    int fd;
    sceIoMkdir(SAVE_DIR, 0777);
    fd = sceIoOpen(SAVE_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, &sv, sizeof(sv)); sceIoClose(fd); }
}

/*------------------------------ mundo -------------------------------*/
typedef struct { float x0, z0, x1, z1, h; unsigned int col; int kind; } Bld; /* kind 0 predio, 1 caixote */
typedef struct { float x, z, s; unsigned int col; } Tree;
static Bld blds[MAX_BLD];
static int nbld = 0;
static Tree trees[MAX_TREES];
static int ntree = 0;
static float camps[MAX_CAMPS][2];
static int ncamps = 0;

static void add_bld(float x0, float z0, float x1, float z1, float h, unsigned int col, int kind)
{
    if (nbld >= MAX_BLD) return;
    blds[nbld].x0 = x0; blds[nbld].z0 = z0; blds[nbld].x1 = x1; blds[nbld].z1 = z1;
    blds[nbld].h = h; blds[nbld].col = col; blds[nbld].kind = kind;
    nbld++;
}
static void add_tree(float x, float z)
{
    int g;
    if (ntree >= MAX_TREES) return;
    g = (int)frange(70, 120);
    trees[ntree].x = x; trees[ntree].z = z; trees[ntree].s = frange(0.8f, 1.4f);
    trees[ntree].col = RGB(30 + g / 6, g, 40 + g / 8);
    ntree++;
}

static void world_gen_static(void)
{
    static const unsigned int pal[6] = {
        RGB(215, 205, 190), RGB(190, 180, 170), RGB(200, 170, 150),
        RGB(170, 175, 185), RGB(210, 195, 150), RGB(160, 150, 145)};
    int bx, bz, i, j, k;
    rng_state = 20240607u;
    nbld = 0; ntree = 0; ncamps = 0;
    for (bx = -4; bx < 4; bx++)
        for (bz = -4; bz < 4; bz++) {
            float x0 = bx * 100.0f + 10.0f, z0 = bz * 100.0f + 10.0f, r = frand();
            if (bx == 0 && bz == 0) { /* praca inicial */
                camps[ncamps][0] = 38.0f; camps[ncamps][1] = 36.0f; ncamps++;
                add_bld(30, 28, 32.4f, 30.4f, 1.3f, RGB(110, 80, 50), 1);
                add_bld(44, 30, 46.4f, 32.4f, 1.3f, RGB(110, 80, 50), 1);
                for (k = 0; k < 10; k++) add_tree(frange(14, 86), frange(14, 24));
                continue;
            }
            if (r < 0.62f) {
                for (i = 0; i < 2; i++)
                    for (j = 0; j < 2; j++)
                        if (frand() < 0.88f) {
                            float w = frange(16, 32), d = frange(16, 32);
                            float ox = x0 + i * 40.0f + frange(2, 38 - w), oz = z0 + j * 40.0f + frange(2, 38 - d);
                            add_bld(ox, oz, ox + w, oz + d, frange(6, 22), pal[rnd_u() % 6], 0);
                        }
                for (k = 0; k < 4; k++) add_tree(x0 + frange(0, 80), z0 + frange(0, 80));
            } else if (r < 0.78f) { /* parque */
                for (k = 0; k < 18; k++) add_tree(x0 + frange(2, 78), z0 + frange(2, 78));
            } else if (r < 0.92f) { /* acampamento de sobreviventes */
                float cx = x0 + 40.0f, cz = z0 + 40.0f;
                if (ncamps < MAX_CAMPS) { camps[ncamps][0] = cx; camps[ncamps][1] = cz; ncamps++; }
                for (k = 0; k < 7; k++) {
                    float a = k * (2.0f * PI / 7.0f), px = cx + cosf(a) * 7.0f, pz = cz + sinf(a) * 7.0f;
                    add_bld(px - 1.1f, pz - 1.1f, px + 1.1f, pz + 1.1f, frange(1.0f, 1.8f), RGB(105 + k * 4, 78, 48), 1);
                }
                for (k = 0; k < 6; k++) add_tree(x0 + frange(0, 80), z0 + frange(0, 80));
            } else {
                for (k = 0; k < 9; k++) add_tree(x0 + frange(2, 78), z0 + frange(2, 78));
                for (k = 0; k < 4; k++) {
                    float px = x0 + frange(5, 75), pz = z0 + frange(5, 75);
                    add_bld(px, pz, px + 2.2f, pz + 2.2f, 1.4f, RGB(90, 90, 96), 1);
                }
            }
        }
}

static int inside_bld(float x, float z, float m)
{
    int i;
    for (i = 0; i < nbld; i++)
        if (x > blds[i].x0 - m && x < blds[i].x1 + m && z > blds[i].z0 - m && z < blds[i].z1 + m) return 1;
    return 0;
}

static int collide_bld(float *x, float *z, float r)
{
    int i, hit = 0;
    for (i = 0; i < nbld; i++) {
        const Bld *b = &blds[i];
        float cx, cz, dx, dz, d2;
        if (*x < b->x0 - r || *x > b->x1 + r || *z < b->z0 - r || *z > b->z1 + r) continue;
        cx = clampf(*x, b->x0, b->x1); cz = clampf(*z, b->z0, b->z1);
        dx = *x - cx; dz = *z - cz; d2 = dx * dx + dz * dz;
        if (d2 < r * r) {
            if (d2 > 1e-6f) {
                float d = sqrtf(d2);
                *x = cx + dx / d * r; *z = cz + dz / d * r;
            } else {
                float l = *x - b->x0, rr = b->x1 - *x, t = *z - b->z0, bt = b->z1 - *z;
                float m = fminf(fminf(l, rr), fminf(t, bt));
                if (m == l) *x = b->x0 - r; else if (m == rr) *x = b->x1 + r;
                else if (m == t) *z = b->z0 - r; else *z = b->z1 + r;
            }
            hit = 1;
        }
    }
    return hit;
}

static int collide_trees(float *x, float *z, float r)
{
    int i, hit = 0;
    for (i = 0; i < ntree; i++) {
        float dx = *x - trees[i].x, dz = *z - trees[i].z, rr = r + 0.35f, d2 = dx * dx + dz * dz;
        if (d2 < rr * rr) {
            float d = sqrtf(d2);
            if (d < 1e-4f) { dx = 1; dz = 0; d = 1; }
            *x = trees[i].x + dx / d * rr; *z = trees[i].z + dz / d * rr;
            hit = 1;
        }
    }
    return hit;
}

static float ray_bld(float px, float py, float pz, float dx, float dy, float dz, float maxt)
{
    float best = maxt;
    int i;
    for (i = 0; i < nbld; i++) {
        const Bld *b = &blds[i];
        float tmin = 0.0f, tmax = best, y;
        if (fabsf(dx) < 1e-6f) { if (px < b->x0 || px > b->x1) continue; }
        else {
            float t1 = (b->x0 - px) / dx, t2 = (b->x1 - px) / dx;
            if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) continue;
        }
        if (fabsf(dz) < 1e-6f) { if (pz < b->z0 || pz > b->z1) continue; }
        else {
            float t1 = (b->z0 - pz) / dz, t2 = (b->z1 - pz) / dz;
            if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) continue;
        }
        y = py + dy * tmin;
        if (y > b->h || y < 0.0f) continue;
        best = tmin;
    }
    return best;
}

/*------------------------------ entidades ---------------------------*/
typedef struct { float x, z, yaw, hp, anim, atk, hurt, deadt, wander; int type, alive; } Zombie;
typedef struct { float x, z, yaw, speed, hp; int type, state; } Car; /* state 0 parado, 1 dirigindo, 2 destruido */
typedef struct { float x, z, yaw, hp, anim, cool, flash; int state, name, inside; unsigned int shirt; } Survivor; /* 0 acampamento, 1 seguindo, 2 morto */

typedef struct {
    float x, z, yaw, pitch, hp, stamina, fire_cd, reload_t, flash, recoil, bob, hitmark, dmg_flash, bloom, fov, aim, stam_delay, speed;
    int weapon, mag[3], reserve[3], medkits, in_car, reloading, kills;
} Player;

static Player P;
static Zombie Z[MAX_ZOMBIES];
static Car C[MAX_CARS];
static Survivor S[MAX_SURV];
static int nsurv = 0;
static struct {
    int wave, kills, recruited, prompt_kind, prompt_idx, aim_hit;
    float wave_t, spawn_t, time, msg_t;
    char msg[64];
} G;

static const char *SURV_NAMES[16] = {"Joao", "Maria", "Carlos", "Ana", "Pedro", "Luiza", "Rafael", "Bia",
                                     "Marcos", "Julia", "Tiago", "Lia", "Diego", "Nina", "Caio", "Vera"};

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(G.msg, sizeof(G.msg), fmt, ap);
    va_end(ap);
    G.msg_t = 3.0f;
}

static int count_follow(void)
{
    int i, n = 0;
    for (i = 0; i < nsurv; i++) if (S[i].state == 1) n++;
    return n;
}

static void spawn_zombie(float x, float z, int type)
{
    int i;
    for (i = 0; i < MAX_ZOMBIES; i++)
        if (!Z[i].alive) {
            Zombie *q = &Z[i];
            float base = type == 0 ? 100.0f : (type == 1 ? 60.0f : 400.0f);
            memset(q, 0, sizeof(*q));
            q->alive = 1; q->type = type; q->x = x; q->z = z; q->yaw = frand() * 2 * PI;
            q->hp = base * (1.0f + 0.08f * (float)(G.wave - 1));
            q->wander = frange(1, 4); q->anim = frand() * 6.0f;
            return;
        }
}

static void spawn_near(float cx, float cz, float rmin, float rmax, int type)
{
    int t;
    for (t = 0; t < 10; t++) {
        float a = frand() * 2 * PI, r = frange(rmin, rmax);
        float x = cx + cosf(a) * r, z = cz + sinf(a) * r;
        if (fabsf(x) > WORLD_HALF + 10 || fabsf(z) > WORLD_HALF + 10) continue;
        if (inside_bld(x, z, 1.5f)) continue;
        spawn_zombie(x, z, type);
        return;
    }
}

static int pick_ztype(void)
{
    float r = frand();
    if (G.wave >= 3 && r < 0.07f + 0.01f * G.wave) return 2;
    if (G.wave >= 2 && r < 0.30f) return 1;
    return 0;
}

static void world_gen_entities(void)
{
    int i, n;
    rng_state = (unsigned)sceKernelGetSystemTimeLow() | 1u;
    memset(Z, 0, sizeof(Z)); memset(C, 0, sizeof(C)); memset(S, 0, sizeof(S));
    /* carro do jogador (escolhido na loja) */
    C[0].x = 58.0f; C[0].z = 50.0f; C[0].yaw = -PI / 2; C[0].type = sv.selcar; C[0].hp = (float)CARS[sv.selcar].hp;
    n = 1;
    /* carros espalhados pelas ruas */
    while (n < 32) {
        int horiz = rnd_u() & 1, k = (int)(rnd_u() % 9) - 4;
        float line = k * 100.0f, along = frange(-380, 380), lane = (rnd_u() & 1) ? 3.2f : -3.2f;
        float x = horiz ? along : line + lane, z = horiz ? line + lane : along;
        if (dist2(x, z, 50, 50) < 900.0f) continue;
        C[n].x = x; C[n].z = z; C[n].type = (int)(rnd_u() % CAR_COUNT); C[n].hp = (float)CARS[C[n].type].hp;
        C[n].yaw = (horiz ? PI / 2 : 0.0f) + ((rnd_u() & 1) ? PI : 0.0f) + frange(-0.08f, 0.08f);
        n++;
    }
    /* sobreviventes nos acampamentos */
    nsurv = 0;
    for (i = 0; i < ncamps && nsurv < MAX_SURV; i++) {
        int cnt = (i == 0) ? 1 : 1 + (int)(rnd_u() & 1), j;
        for (j = 0; j < cnt && nsurv < MAX_SURV; j++) {
            Survivor *s = &S[nsurv];
            s->x = camps[i][0] + frange(-3, 3); s->z = camps[i][1] + frange(-3, 3);
            s->yaw = frand() * 2 * PI; s->hp = 60; s->state = 0; s->name = (int)(rnd_u() % 16);
            s->shirt = RGB(40 + (rnd_u() % 80), 70 + (rnd_u() % 90), 110 + (rnd_u() % 120));
            nsurv++;
        }
    }
    /* zumbis iniciais */
    G.wave = 1;
    for (i = 0; i < 12; i++) {
        float x = frange(-380, 380), z = frange(-380, 380);
        if (dist2(x, z, 50, 50) < 3600.0f || inside_bld(x, z, 1.5f)) { i--; continue; }
        spawn_zombie(x, z, 0);
    }
}

/*------------------------------ rede (multiplayer) ------------------*/
/* Camada preparada: pacotes binarios, fila com latencia simulada e
   jogadores remotos. Para ligar o Ad-Hoc real (pspnet_adhoc), basta
   implementar net_transport_send/net_transport_recv. O estado local e
   a entrada ja estao separados da simulacao. O host deve ser a
   autoridade dos zumbis (enviar snapshot de zumbis em NET_PKT_WORLD). */
#define NET_MAX 4
#define NET_MAGIC 0x5347u
#define NET_VERSION 1
#define NET_PKT_STATE 1
#define NET_PKT_WORLD 2

typedef struct __attribute__((packed)) {
    unsigned short magic; unsigned char ver, type, id, weapon, flags, car_type;
    float x, z, yaw, pitch; short hp; unsigned int seq;
} NetPacket;

typedef struct { int active; float x, z, yaw, pitch, anim; int hp, weapon, in_car, car_type; } RemotePlayer;
static RemotePlayer RP[NET_MAX];
static struct { int mode; unsigned int seq; float send_t; NetPacket q[16]; float qt[16]; int qn; } net;

static void net_pack(NetPacket *p)
{
    p->magic = NET_MAGIC; p->ver = NET_VERSION; p->type = NET_PKT_STATE; p->id = 0;
    p->weapon = (unsigned char)P.weapon; p->flags = (unsigned char)(P.in_car >= 0);
    p->car_type = (unsigned char)(P.in_car >= 0 ? C[P.in_car].type : 0);
    p->x = P.x; p->z = P.z; p->yaw = P.yaw; p->pitch = P.pitch; p->hp = (short)P.hp; p->seq = net.seq++;
}

static void net_apply(const NetPacket *p, float ox, float oz)
{
    RemotePlayer *r;
    if (p->magic != NET_MAGIC || p->ver != NET_VERSION || p->type != NET_PKT_STATE || p->id >= NET_MAX) return;
    r = &RP[p->id];
    r->active = 1;
    r->anim += sqrtf(dist2(r->x, r->z, p->x + ox, p->z + oz)) * 1.4f;
    r->x = p->x + ox; r->z = p->z + oz; r->yaw = p->yaw; r->pitch = p->pitch;
    r->hp = p->hp; r->weapon = p->weapon; r->in_car = p->flags & 1; r->car_type = p->car_type;
}

static void net_tick(float dt)
{
    int i;
    if (net.mode == 0) return;
    net.send_t += dt;
    if (net.send_t >= 0.05f) { /* 20 pacotes/s */
        net.send_t = 0;
        if (net.mode == 1 && net.qn < 16) { /* loopback: entrega local com 100 ms de atraso */
            net_pack(&net.q[net.qn]);
            net.q[net.qn].id = 1;
            net.qt[net.qn] = 0.10f;
            net.qn++;
        }
        /* net.mode == 2: net_transport_send(&pacote, sizeof(pacote)); */
    }
    for (i = 0; i < net.qn; i++) net.qt[i] -= dt;
    while (net.qn > 0 && net.qt[0] <= 0) {
        net_apply(&net.q[0], 4.0f, 3.0f);
        memmove(&net.q[0], &net.q[1], (size_t)(net.qn - 1) * sizeof(NetPacket));
        memmove(&net.qt[0], &net.qt[1], (size_t)(net.qn - 1) * sizeof(float));
        net.qn--;
    }
}

/*------------------------------ jogo: logica ------------------------*/
static void zombie_kill_reward(const Zombie *z)
{
    int add = z->type == 0 ? 20 : (z->type == 1 ? 30 : 100);
    sv.money += add + G.wave * 2;
    G.kills++;
    P.kills++;
}

static void zombie_damage(Zombie *z, float dmg, int by_player)
{
    if (!z->alive || z->hp <= 0) return;
    z->hp -= dmg; z->hurt = 0.15f;
    if (z->hp <= 0) { z->deadt = 0.01f; if (by_player) zombie_kill_reward(z); }
}

static float ray_zombie(const Zombie *z, float px, float py, float pz, float dx, float dy, float dz, float maxt)
{
    float ox = px - z->x, oz = pz - z->z, r = z->type == 2 ? 0.8f : 0.55f, h = z->type == 2 ? 2.3f : 1.85f;
    float a = dx * dx + dz * dz, b, c, disc, t, y;
    if (!z->alive || z->hp <= 0 || a < 1e-6f) return -1.0f;
    b = ox * dx + oz * dz; c = ox * ox + oz * oz - r * r;
    disc = b * b - a * c;
    if (disc < 0) return -1.0f;
    t = (-b - sqrtf(disc)) / a;
    if (t < 0) t = 0;
    if (t > maxt) return -1.0f;
    y = py + dy * t;
    if (y < 0 || y > h) return -1.0f;
    return t;
}

static void fire_dir(float yaw, float pitch, float rx, float ry, float *dx, float *dy, float *dz)
{
    float cs = cosf(yaw), sn = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float fx = -sn * cp, fy = sp, fz = -cs * cp;
    float rxx = cs, rzz = -sn;
    float ux = sn * sp, uy = cp, uz = cs * sp;
    float x = fx + rxx * rx + ux * ry, y = fy + uy * ry, z = fz + rzz * rx + uz * ry;
    float l = sqrtf(x * x + y * y + z * z);
    *dx = x / l; *dy = y / l; *dz = z / l;
}

static void player_shoot(void)
{
    const WeaponDef *w = &WEAP[P.weapon];
    int p, i;
    float eye = 1.65f, moving = P.speed > 0.5f ? 1.0f : 0.0f;
    float sp = w->spread * (1.0f + P.bloom * 2.0f + moving * 0.8f) * (P.aim > 0.5f ? 0.45f : 1.0f);
    for (p = 0; p < w->pellets; p++) {
        float dx, dy, dz, tb, best = w->range;
        int bi = -1;
        fire_dir(P.yaw, P.pitch + P.recoil, frange(-sp, sp), frange(-sp, sp), &dx, &dy, &dz);
        tb = ray_bld(P.x, eye, P.z, dx, dy, dz, w->range);
        best = tb;
        for (i = 0; i < MAX_ZOMBIES; i++) {
            float t = ray_zombie(&Z[i], P.x, eye, P.z, dx, dy, dz, best);
            if (t >= 0 && t < best) { best = t; bi = i; }
        }
        if (bi >= 0) {
            float fall = clampf(1.0f - best / w->range, 0.35f, 1.0f);
            zombie_damage(&Z[bi], w->dmg * fall, 1);
            Z[bi].x += dx * 0.25f; Z[bi].z += dz * 0.25f;
            P.hitmark = 0.18f;
        }
    }
    P.mag[P.weapon]--;
    P.fire_cd = w->rate;
    P.flash = 0.07f;
    P.recoil += w->recoil;
    P.bloom = clampf(P.bloom + (w->automatic ? 0.12f : 0.3f), 0, 1);
}

static void player_hurt(float d)
{
    if (P.in_car >= 0) { C[P.in_car].hp -= d * 1.5f; P.dmg_flash = 0.15f; return; }
    P.hp -= d; P.dmg_flash = 0.45f;
}

static void exit_car(int hurt)
{
    Car *c;
    int i, k = 0;
    float rx, rz;
    if (P.in_car < 0) return;
    c = &C[P.in_car];
    rx = cosf(c->yaw); rz = -sinf(c->yaw);
    P.x = c->x + rx * 2.4f; P.z = c->z + rz * 2.4f;
    collide_bld(&P.x, &P.z, 0.5f);
    if (c->state == 1) c->state = 0;
    c->speed = 0;
    P.in_car = -1;
    if (hurt) P.hp -= 15;
    for (i = 0; i < nsurv; i++)
        if (S[i].state == 1 && S[i].inside) {
            float a = k++ * 1.3f;
            S[i].inside = 0; S[i].x = P.x + cosf(a) * 2.0f; S[i].z = P.z + sinf(a) * 2.0f;
        }
}

static void car_update_driven(Car *c, const Input *in, float dt)
{
    const CarDef *d = &CARS[c->type];
    float thr = in->my, sf, fx, fz, ox, oz;
    int i;
    if (thr > 0.05f) c->speed += d->acc * thr * dt * (c->speed < -0.5f ? 2.5f : 1.0f);
    else if (thr < -0.05f) c->speed += d->acc * thr * dt * (c->speed > 0.5f ? 2.5f : 0.6f);
    else {
        float drag = 3.0f * dt;
        if (c->speed > 0) c->speed = fmaxf(0.0f, c->speed - drag); else c->speed = fminf(0.0f, c->speed + drag);
    }
    if (in->b & PSP_CTRL_SQUARE) {
        float br = 20.0f * dt;
        if (c->speed > 0) c->speed = fmaxf(0.0f, c->speed - br); else c->speed = fminf(0.0f, c->speed + br);
    }
    c->speed = clampf(c->speed, -d->vmax * 0.4f, d->vmax);
    sf = clampf(c->speed / 6.0f, -1.0f, 1.0f);
    c->yaw -= in->mx * 1.7f * sf * dt * (1.0f - 0.35f * fabsf(c->speed) / d->vmax);
    fx = -sinf(c->yaw); fz = -cosf(c->yaw);
    ox = c->x; oz = c->z;
    c->x += fx * c->speed * dt; c->z += fz * c->speed * dt;
    if (collide_bld(&c->x, &c->z, 1.9f)) {
        float imp = fabsf(c->speed);
        if (imp > 7.0f) c->hp -= (imp - 7.0f) * 3.0f;
        c->speed *= -0.25f;
    }
    if (collide_trees(&c->x, &c->z, 1.2f)) { c->hp -= fabsf(c->speed) * 0.6f; c->speed *= 0.5f; }
    c->x = clampf(c->x, -WORLD_HALF - 15, WORLD_HALF + 15);
    c->z = clampf(c->z, -WORLD_HALF - 15, WORLD_HALF + 15);
    (void)ox; (void)oz;
    /* batidas em outros carros */
    for (i = 0; i < MAX_CARS; i++) {
        Car *o = &C[i];
        float dx, dz, dd;
        if (o == c || o->hp == 0.0f) continue;
        dx = o->x - c->x; dz = o->z - c->z; dd = sqrtf(dx * dx + dz * dz);
        if (dd < 3.8f && dd > 0.01f) {
            float ov = 3.8f - dd, imp = fabsf(c->speed);
            o->x += dx / dd * ov * 0.6f; o->z += dz / dd * ov * 0.6f;
            c->x -= dx / dd * ov * 0.4f; c->z -= dz / dd * ov * 0.4f;
            if (imp > 5.0f) { c->hp -= (imp - 5.0f) * 1.5f; o->hp -= (imp - 5.0f) * 2.0f; }
            c->speed *= 0.6f;
        }
    }
    /* atropelar zumbis */
    for (i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &Z[i];
        if (!z->alive || z->hp <= 0) continue;
        if (dist2(z->x, z->z, c->x + fx * 1.2f, c->z + fz * 1.2f) < 2.6f * 2.6f && fabsf(c->speed) > 5.0f) {
            zombie_damage(z, fabsf(c->speed) * 9.0f, 1);
            z->x += fx * 2.0f; z->z += fz * 2.0f;
            c->hp -= 1.5f; c->speed *= 0.97f;
        }
    }
    P.x = c->x; P.z = c->z; P.yaw = c->yaw; P.speed = fabsf(c->speed);
    if (c->hp <= 0) {
        c->hp = 0.0001f; c->state = 2; say("O carro foi destruido!");
        exit_car(1);
    }
}

static void update_prompt(void)
{
    int i;
    float best = 1e9f;
    G.prompt_kind = 0; G.prompt_idx = -1;
    if (P.in_car >= 0) return;
    for (i = 0; i < nsurv; i++)
        if (S[i].state == 0) {
            float d = dist2(P.x, P.z, S[i].x, S[i].z);
            if (d < 3.5f * 3.5f && d < best) { best = d; G.prompt_kind = 1; G.prompt_idx = i; }
        }
    for (i = 0; i < MAX_CARS; i++)
        if (C[i].hp > 0 && C[i].state == 0) {
            float d = dist2(P.x, P.z, C[i].x, C[i].z);
            if (d < 4.8f * 4.8f && d < best) { best = d; G.prompt_kind = 2; G.prompt_idx = i; }
        }
}

static void do_interact(void)
{
    if (P.in_car >= 0) { exit_car(0); return; }
    if (G.prompt_kind == 1) {
        if (count_follow() >= MAX_FOLLOW) { say("Grupo cheio (%d)", MAX_FOLLOW); return; }
        S[G.prompt_idx].state = 1; S[G.prompt_idx].inside = 0;
        G.recruited++; sv.money += 100;
        say("%s entrou no grupo! +$100", SURV_NAMES[S[G.prompt_idx].name]);
    } else if (G.prompt_kind == 2) {
        int i;
        P.in_car = G.prompt_idx; C[P.in_car].state = 1;
        for (i = 0; i < nsurv; i++) if (S[i].state == 1) S[i].inside = 1;
        say("Dirigindo: %s", CARS[C[P.in_car].type].name);
    }
}

static void sim_player_foot(const Input *in, float dt)
{
    const WeaponDef *w = &WEAP[P.weapon];
    float turn = (in->b & PSP_CTRL_LTRIGGER) ? 0.9f : 2.3f;
    float fx, fz, rx, rz, mx = in->mx, my = in->my, mag, spd;
    int i;
    P.yaw -= in->lx * turn * dt;
    P.pitch = clampf(P.pitch + in->ly * 1.2f * dt, -1.0f, 1.0f);
    P.aim = (in->b & PSP_CTRL_LTRIGGER) ? 1.0f : 0.0f;
    fx = -sinf(P.yaw); fz = -cosf(P.yaw); rx = cosf(P.yaw); rz = -sinf(P.yaw);
    mag = sqrtf(mx * mx + my * my);
    if (mag > 1.0f) { mx /= mag; my /= mag; mag = 1.0f; }
    spd = 4.2f;
    if ((in->b & PSP_CTRL_CROSS) && P.stamina > 0 && my > 0.3f && P.aim < 0.5f) {
        spd = 7.4f; P.stamina -= 26.0f * dt; P.stam_delay = 1.0f;
        if (P.stamina < 0) P.stamina = 0;
    } else {
        P.stam_delay -= dt;
        if (P.stam_delay <= 0) P.stamina = fminf(100.0f, P.stamina + 18.0f * dt);
    }
    if (P.aim > 0.5f) spd *= 0.55f;
    P.speed = mag * spd;
    P.x += (fx * my + rx * mx) * spd * dt;
    P.z += (fz * my + rz * mx) * spd * dt;
    collide_bld(&P.x, &P.z, 0.45f);
    collide_trees(&P.x, &P.z, 0.4f);
    for (i = 0; i < MAX_CARS; i++)
        if (C[i].hp > 0) {
            float dx = P.x - C[i].x, dz = P.z - C[i].z, d2 = dx * dx + dz * dz;
            if (d2 < 2.1f * 2.1f && d2 > 1e-6f) { float d = sqrtf(d2); P.x = C[i].x + dx / d * 2.1f; P.z = C[i].z + dz / d * 2.1f; }
        }
    P.x = clampf(P.x, -WORLD_HALF - 15, WORLD_HALF + 15);
    P.z = clampf(P.z, -WORLD_HALF - 15, WORLD_HALF + 15);
    P.bob += P.speed * dt * 1.7f;

    /* armas */
    P.fire_cd -= dt;
    P.flash = fmaxf(0.0f, P.flash - dt);
    P.recoil = fmaxf(0.0f, P.recoil - dt * 0.9f);
    P.bloom = fmaxf(0.0f, P.bloom - dt * 1.8f);
    P.hitmark = fmaxf(0.0f, P.hitmark - dt);
    if (P.reloading) {
        P.reload_t -= dt;
        if (P.reload_t <= 0) {
            int need = w->mag - P.mag[P.weapon], take = need < P.reserve[P.weapon] ? need : P.reserve[P.weapon];
            P.mag[P.weapon] += take; P.reserve[P.weapon] -= take; P.reloading = 0;
        }
    }
    if ((in->p & PSP_CTRL_CIRCLE) && !P.reloading) {
        int n = P.weapon;
        for (i = 0; i < 3; i++) { n = (n + 1) % 3; if (sv.wown[n]) break; }
        if (n != P.weapon) { P.weapon = n; P.fire_cd = 0.3f; say("%s", WEAP[n].name); }
    }
    if ((in->p & PSP_CTRL_SQUARE) && !P.reloading && P.mag[P.weapon] < w->mag && P.reserve[P.weapon] > 0) {
        P.reloading = 1; P.reload_t = P.weapon == 1 ? 2.0f : 1.4f;
    }
    if (!P.reloading && P.fire_cd <= 0) {
        int want = w->automatic ? (in->b & PSP_CTRL_RTRIGGER) : (in->p & PSP_CTRL_RTRIGGER);
        if (want) {
            if (P.mag[P.weapon] > 0) player_shoot();
            else if (P.reserve[P.weapon] > 0) { P.reloading = 1; P.reload_t = 1.4f; }
            else P.fire_cd = 0.3f;
        }
    }
    if ((in->p & PSP_CTRL_SELECT) && P.medkits > 0 && P.hp < 100) {
        P.medkits--; P.hp = fminf(100.0f, P.hp + 50.0f); say("Kit medico usado");
    }
    /* alvo sob a mira (para a cor da mira) */
    {
        float dx, dy, dz, best, tb;
        fire_dir(P.yaw, P.pitch + P.recoil, 0, 0, &dx, &dy, &dz);
        tb = ray_bld(P.x, 1.65f, P.z, dx, dy, dz, w->range);
        best = tb; G.aim_hit = 0;
        for (i = 0; i < MAX_ZOMBIES; i++) {
            float t = ray_zombie(&Z[i], P.x, 1.65f, P.z, dx, dy, dz, best);
            if (t >= 0 && t < best) { best = t; G.aim_hit = 1; }
        }
    }
}

static void sim_zombies(float dt)
{
    int i, j, k;
    float tx = P.x, tz = P.z;
    for (i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &Z[i];
        float spd, d, dx, dz, dmg, atkcd;
        int tgt = -1;
        if (!z->alive) continue;
        if (z->hp <= 0) { z->deadt += dt; if (z->deadt > 3.0f) z->alive = 0; continue; }
        z->hurt = fmaxf(0.0f, z->hurt - dt);
        z->atk -= dt;
        spd = z->type == 0 ? 2.0f : (z->type == 1 ? 4.2f : 1.7f);
        spd *= fminf(1.5f, 1.0f + 0.03f * (float)G.wave);
        dmg = z->type == 0 ? 8.0f : (z->type == 1 ? 6.0f : 18.0f);
        atkcd = z->type == 1 ? 0.7f : 1.0f;
        dx = tx - z->x; dz = tz - z->z; d = sqrtf(dx * dx + dz * dz);
        for (j = 0; j < nsurv; j++)
            if (S[j].state == 1 && !S[j].inside) {
                float sx = S[j].x - z->x, sz = S[j].z - z->z, sd = sqrtf(sx * sx + sz * sz);
                if (sd < d && sd < 12.0f) { d = sd; dx = sx; dz = sz; tgt = j; }
            }
        if (d < 60.0f && d > 0.01f) {
            z->yaw = atan2f(-dx, -dz);
            if (d > 1.25f) { z->x += dx / d * spd * dt; z->z += dz / d * spd * dt; z->anim += spd * dt * 1.8f; }
            else if (z->atk <= 0) {
                z->atk = atkcd;
                if (tgt >= 0) S[tgt].hp -= dmg;
                else player_hurt(dmg);
            }
            if (tgt < 0 && P.in_car >= 0 && d < 3.2f && d > 1.25f && z->atk <= 0) { z->atk = atkcd; player_hurt(dmg * 0.6f); }
        } else {
            z->wander -= dt;
            if (z->wander <= 0) { z->yaw = frand() * 2 * PI; z->wander = frange(2, 6); }
            z->x += -sinf(z->yaw) * 0.7f * dt; z->z += -cosf(z->yaw) * 0.7f * dt; z->anim += dt * 1.2f;
        }
        collide_bld(&z->x, &z->z, 0.4f);
        for (k = 0; k < MAX_CARS; k++)
            if (C[k].hp > 0 && !(P.in_car == k)) {
                float cx = z->x - C[k].x, cz = z->z - C[k].z, cd = cx * cx + cz * cz;
                if (cd < 2.0f * 2.0f && cd > 1e-6f) { cd = sqrtf(cd); z->x = C[k].x + cx / cd * 2.0f; z->z = C[k].z + cz / cd * 2.0f; }
            }
        for (j = i + 1; j < MAX_ZOMBIES; j++)
            if (Z[j].alive && Z[j].hp > 0) {
                float sx = Z[j].x - z->x, sz = Z[j].z - z->z, sd = sx * sx + sz * sz;
                if (sd < 0.64f && sd > 1e-6f) {
                    sd = sqrtf(sd);
                    z->x -= sx / sd * 0.02f; z->z -= sz / sd * 0.02f; Z[j].x += sx / sd * 0.02f; Z[j].z += sz / sd * 0.02f;
                }
            }
    }
}

static void sim_survivors(float dt)
{
    int i, j, idx = 0;
    for (i = 0; i < nsurv; i++) {
        Survivor *s = &S[i];
        if (s->state == 2) continue;
        if (s->hp <= 0) { s->state = 2; say("%s morreu!", SURV_NAMES[s->name]); continue; }
        s->flash = fmaxf(0.0f, s->flash - dt);
        s->cool -= dt;
        if (s->state == 0) { s->anim = 0; continue; }
        idx++;
        if (s->inside) { s->x = P.x; s->z = P.z; continue; }
        {
            float dx = P.x - s->x, dz = P.z - s->z, d = sqrtf(dx * dx + dz * dz), spd = 5.2f;
            float bestd = 24.0f * 24.0f;
            int bz = -1;
            if (d > 3.5f) {
                if (d > 14.0f) spd = 8.0f;
                s->x += dx / d * spd * dt; s->z += dz / d * spd * dt; s->anim += spd * dt * 1.6f;
                s->yaw = atan2f(-dx, -dz);
            }
            if (d > 60.0f) { s->x = P.x + 2.0f; s->z = P.z + 2.0f; } /* teleporta se ficou muito longe */
            collide_bld(&s->x, &s->z, 0.4f);
            for (j = 0; j < MAX_ZOMBIES; j++)
                if (Z[j].alive && Z[j].hp > 0) {
                    float dd = dist2(s->x, s->z, Z[j].x, Z[j].z);
                    if (dd < bestd) { bestd = dd; bz = j; }
                }
            if (bz >= 0 && s->cool <= 0) {
                float ex = Z[bz].x - s->x, ez = Z[bz].z - s->z, el = sqrtf(ex * ex + ez * ez);
                if (el > 0.1f && ray_bld(s->x, 1.4f, s->z, ex / el, 0, ez / el, el) >= el - 0.01f) {
                    s->yaw = atan2f(-ex, -ez);
                    if (frand() < 0.75f) zombie_damage(&Z[bz], 24.0f, 1);
                    s->flash = 0.08f; s->cool = frange(0.7f, 1.2f);
                }
            }
        }
    }
    (void)idx;
}

static void sim_play(const Input *in, float dt)
{
    int i, alive = 0, cap;
    G.time += dt;
    G.msg_t -= dt;
    P.dmg_flash = fmaxf(0.0f, P.dmg_flash - dt);
    if (P.in_car >= 0) {
        P.aim = 0.0f; P.hitmark = 0; P.pitch = clampf(P.pitch + in->ly * 1.0f * dt, -0.6f, 0.6f);
        G.prompt_kind = 0;
        car_update_driven(&C[P.in_car], in, dt);
        P.bob += P.speed * dt * 0.8f;
        if (in->p & PSP_CTRL_TRIANGLE) do_interact();
    } else {
        update_prompt();
        if (in->p & PSP_CTRL_TRIANGLE) do_interact();
        sim_player_foot(in, dt);
    }
    sim_zombies(dt);
    sim_survivors(dt);
    /* ondas */
    G.wave_t += dt;
    if (G.wave_t > 75.0f) { G.wave++; G.wave_t = 0; sv.money += 50 * G.wave; say("ONDA %d! +$%d", G.wave, 50 * G.wave); }
    for (i = 0; i < MAX_ZOMBIES; i++) {
        if (Z[i].alive && Z[i].hp > 0) {
            alive++;
            if (dist2(Z[i].x, Z[i].z, P.x, P.z) > 200.0f * 200.0f && alive > 6) { Z[i].alive = 0; alive--; }
        }
    }
    cap = 8 + G.wave * 4; if (cap > MAX_ZOMBIES - 2) cap = MAX_ZOMBIES - 2;
    G.spawn_t -= dt;
    if (G.spawn_t <= 0 && alive < cap) {
        spawn_near(P.x, P.z, 50.0f, 90.0f, pick_ztype());
        G.spawn_t = fmaxf(0.4f, 3.0f - 0.3f * (float)G.wave);
    }
    net_tick(dt);
    if (P.hp <= 0) P.hp = 0;
}

/*------------------------------ jogo: render ------------------------*/
typedef struct { unsigned int shirt, pants, skin; float wf, hf; int zombie; } Style;

static void person(float x, float z, float yaw, float anim, float squash, const Style *st, int flash)
{
    float sw = sinf(anim) * 0.22f, wf = st->wf, hf = st->hf * squash;
    unsigned int shirt = flash ? RGB(220, 60, 60) : st->shirt;
    box_rel(x, z, yaw, -0.14f * wf, 0, sw, 0.24f * wf, 0.8f * hf, 0.26f * wf, st->pants);
    box_rel(x, z, yaw,  0.14f * wf, 0, -sw, 0.24f * wf, 0.8f * hf, 0.26f * wf, st->pants);
    box_rel(x, z, yaw, 0, 0.8f * hf, 0, 0.56f * wf, 0.62f * hf, 0.30f * wf, shirt);
    box_rel(x, z, yaw, 0, 1.42f * hf, 0, 0.30f * wf, 0.30f * hf, 0.30f * wf, st->skin);
    if (st->zombie) {
        box_rel(x, z, yaw, -0.38f * wf, 1.12f * hf, -0.36f * wf, 0.16f * wf, 0.16f * hf, 0.70f * wf, st->skin);
        box_rel(x, z, yaw,  0.38f * wf, 1.12f * hf, -0.36f * wf, 0.16f * wf, 0.16f * hf, 0.70f * wf, st->skin);
    } else {
        box_rel(x, z, yaw, -0.38f * wf, 0.8f * hf, -sw * 0.6f, 0.16f * wf, 0.62f * hf, 0.18f * wf, shirt);
        box_rel(x, z, yaw,  0.38f * wf, 0.8f * hf,  sw * 0.6f, 0.16f * wf, 0.62f * hf, 0.18f * wf, shirt);
    }
}

static void marker(float x, float z, float h, unsigned int col)
{
    float t = G.time * 3.0f;
    box(x, h + sinf(t) * 0.12f, z, 0.22f, 0.22f, 0.22f, t * 0.5f, col);
}

static void car_bb(float x, float z, int type, float yaw, unsigned int col)
{
    float rx = cam_cs, rz = -cam_sn, hw = 3.0f, hh = 3.0f, fx = -sinf(yaw), fz = -cosf(yaw);
    int flip = (fx * rx + fz * rz) > 0.0f;
    float p[12];
    p[0] = x - rx * hw; p[1] = 0;  p[2] = z - rz * hw;
    p[3] = x + rx * hw; p[4] = 0;  p[5] = z + rz * hw;
    p[6] = x + rx * hw; p[7] = hh; p[8] = z + rz * hw;
    p[9] = x - rx * hw; p[10] = hh; p[11] = z - rz * hw;
    tex_bind(T_CAR + type, 0, 0);
    quad1(p, flip ? 1.0f : 0.0f, 0.0f, flip ? 0.0f : 1.0f, 1.0f, col);
}

static void begin3d(float fov)
{
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_FOG);
    sceGuFog(50.0f, 200.0f, FOG_COL);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fov, (float)SCR_W / (float)SCR_H, 0.25f, 400.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
}

static void draw_ground(void)
{
    int tx, tz, k, i;
    unsigned int white = RGB(255, 255, 255);
    sceGuDisable(GU_DEPTH_TEST);
    tex_bind(T_GROUND, 1, 1);
    for (tx = -11; tx < 11; tx++)
        for (tz = -11; tz < 11; tz++) {
            float x0 = tx * 40.0f, z0 = tz * 40.0f, p[12];
            if (dist2(x0 + 20, z0 + 20, cam_x, cam_z) > 200.0f * 200.0f || !visible(x0 + 20, z0 + 20, 30)) continue;
            p[0] = x0; p[1] = 0; p[2] = z0; p[3] = x0 + 40; p[4] = 0; p[5] = z0;
            p[6] = x0 + 40; p[7] = 0; p[8] = z0 + 40; p[9] = x0; p[10] = 0; p[11] = z0 + 40;
            quad1(p, 0, 0, 10, 10, white);
        }
    tex_bind(T_ROAD, 1, 1);
    for (k = -4; k <= 4; k++) {
        float L = k * 100.0f;
        for (i = 0; i < 16; i++) {
            float s0 = -400.0f + i * 50.0f, s1 = s0 + 50.0f, sc = (s0 + s1) * 0.5f, p[12];
            if (dist2(L, sc, cam_x, cam_z) < 200.0f * 200.0f && visible(L, sc, 40)) {
                p[0] = L - 7; p[1] = 0; p[2] = s0; p[3] = L + 7; p[4] = 0; p[5] = s0;
                p[6] = L + 7; p[7] = 0; p[8] = s1; p[9] = L - 7; p[10] = 0; p[11] = s1;
                quad1(p, 0, 0, 1, 50.0f / 16.0f, white);
            }
            if (dist2(sc, L, cam_x, cam_z) < 200.0f * 200.0f && visible(sc, L, 40)) {
                p[0] = s0; p[1] = 0; p[2] = L - 7; p[3] = s0; p[4] = 0; p[5] = L + 7;
                p[6] = s1; p[7] = 0; p[8] = L + 7; p[9] = s1; p[10] = 0; p[11] = L - 7;
                quad1(p, 0, 0, 1, 50.0f / 16.0f, white);
            }
        }
    }
    sceGuEnable(GU_DEPTH_TEST);
}

static void bld_sides(const Bld *b)
{
    float h = b->h, w = b->x1 - b->x0, d = b->z1 - b->z0, p[12];
    Vtx *v = (Vtx *)sceGuGetMemory(24 * sizeof(Vtx)), *q = v;
    p[0] = b->x1; p[1] = 0; p[2] = b->z0; p[3] = b->x0; p[4] = 0; p[5] = b->z0;
    p[6] = b->x0; p[7] = h; p[8] = b->z0; p[9] = b->x1; p[10] = h; p[11] = b->z0;
    q = put_quad(q, p, 0, 0, w / 8.0f, h / 8.0f, shade(b->col, 0.80f));
    p[0] = b->x1; p[1] = 0; p[2] = b->z1; p[3] = b->x1; p[4] = 0; p[5] = b->z0;
    p[6] = b->x1; p[7] = h; p[8] = b->z0; p[9] = b->x1; p[10] = h; p[11] = b->z1;
    q = put_quad(q, p, 0, 0, d / 8.0f, h / 8.0f, shade(b->col, 0.68f));
    p[0] = b->x0; p[1] = 0; p[2] = b->z1; p[3] = b->x1; p[4] = 0; p[5] = b->z1;
    p[6] = b->x1; p[7] = h; p[8] = b->z1; p[9] = b->x0; p[10] = h; p[11] = b->z1;
    q = put_quad(q, p, 0, 0, w / 8.0f, h / 8.0f, shade(b->col, 0.92f));
    p[0] = b->x0; p[1] = 0; p[2] = b->z0; p[3] = b->x0; p[4] = 0; p[5] = b->z1;
    p[6] = b->x0; p[7] = h; p[8] = b->z1; p[9] = b->x0; p[10] = h; p[11] = b->z0;
    put_quad(q, p, 0, 0, d / 8.0f, h / 8.0f, shade(b->col, 0.60f));
    sceGuDrawArray(GU_TRIANGLES, VT3D, 24, 0, v);
}

static void draw_world(void)
{
    static int visb[MAX_BLD];
    int i, nv = 0;
    draw_ground();
    /* predios e caixotes */
    for (i = 0; i < nbld; i++) {
        const Bld *b = &blds[i];
        float cx = (b->x0 + b->x1) * 0.5f, cz = (b->z0 + b->z1) * 0.5f;
        float r = sqrtf((b->x1 - b->x0) * (b->x1 - b->x0) + (b->z1 - b->z0) * (b->z1 - b->z0)) * 0.5f;
        if (dist2(cx, cz, cam_x, cam_z) > 210.0f * 210.0f || !visible(cx, cz, r)) continue;
        visb[nv++] = i;
    }
    tex_bind(T_WALL, 1, 1);
    for (i = 0; i < nv; i++) if (blds[visb[i]].kind == 0) bld_sides(&blds[visb[i]]);
    tex_bind(T_WHITE, 0, 0);
    for (i = 0; i < nv; i++) {
        const Bld *b = &blds[visb[i]];
        if (b->kind == 0) {
            float p[12];
            p[0] = b->x0; p[1] = b->h; p[2] = b->z0; p[3] = b->x1; p[4] = b->h; p[5] = b->z0;
            p[6] = b->x1; p[7] = b->h; p[8] = b->z1; p[9] = b->x0; p[10] = b->h; p[11] = b->z1;
            quad1(p, 0, 0, 1, 1, shade(b->col, 0.5f));
        } else {
            box((b->x0 + b->x1) * 0.5f, 0, (b->z0 + b->z1) * 0.5f, b->x1 - b->x0, b->h, b->z1 - b->z0, 0, b->col);
        }
    }
    /* arvores */
    for (i = 0; i < ntree; i++) {
        const Tree *t = &trees[i];
        if (dist2(t->x, t->z, cam_x, cam_z) > 120.0f * 120.0f || !visible(t->x, t->z, 3)) continue;
        box(t->x, 0, t->z, 0.35f, 2.2f * t->s, 0.35f, 0, RGB(90, 62, 40));
        box(t->x, 1.8f * t->s, t->z, 2.2f * t->s, 2.6f * t->s, 2.2f * t->s, (float)i, t->col);
    }
    /* muros do limite do mapa */
    box(0, 0, -WORLD_HALF - 25, 900, 6, 4, 0, RGB(70, 70, 76));
    box(0, 0,  WORLD_HALF + 25, 900, 6, 4, 0, RGB(70, 70, 76));
    box(-WORLD_HALF - 25, 0, 0, 4, 6, 900, 0, RGB(70, 70, 76));
    box( WORLD_HALF + 25, 0, 0, 4, 6, 900, 0, RGB(70, 70, 76));
    /* zumbis */
    for (i = 0; i < MAX_ZOMBIES; i++) {
        const Zombie *z = &Z[i];
        Style st;
        static const unsigned int shirts[5] = {RGB(90, 70, 60), RGB(60, 80, 100), RGB(100, 100, 70), RGB(80, 60, 90), RGB(110, 50, 50)};
        if (!z->alive || dist2(z->x, z->z, cam_x, cam_z) > 140.0f * 140.0f || !visible(z->x, z->z, 2)) continue;
        st.shirt = shirts[i % 5]; st.pants = RGB(50, 50, 66); st.zombie = 1;
        st.skin = z->type == 1 ? RGB(150, 165, 120) : (z->type == 2 ? RGB(95, 120, 85) : RGB(120, 150, 100));
        st.wf = z->type == 2 ? 1.4f : (z->type == 1 ? 0.92f : 1.0f);
        st.hf = z->type == 2 ? 1.2f : 1.0f;
        person(z->x, z->z, z->yaw, z->anim, z->hp <= 0 ? fmaxf(0.1f, 1.0f - z->deadt * 2.0f) : 1.0f, &st, z->hurt > 0);
    }
    /* sobreviventes */
    for (i = 0; i < nsurv; i++) {
        const Survivor *s = &S[i];
        Style st;
        if (s->state == 2 || s->inside || dist2(s->x, s->z, cam_x, cam_z) > 140.0f * 140.0f || !visible(s->x, s->z, 2)) continue;
        st.shirt = s->shirt; st.pants = RGB(60, 70, 90); st.skin = RGB(205, 160, 125); st.wf = 1; st.hf = 1; st.zombie = 0;
        person(s->x, s->z, s->yaw, s->state == 1 ? s->anim : 0.0f, 1.0f, &st, 0);
        marker(s->x, s->z, 2.1f, s->state == 0 ? RGB(255, 210, 40) : RGB(60, 230, 90));
        if (s->flash > 0) box(s->x - sinf(s->yaw) * 0.9f, 1.15f, s->z - cosf(s->yaw) * 0.9f, 0.18f, 0.18f, 0.18f, 0, RGB(255, 240, 120));
    }
    /* jogadores remotos (multiplayer / loopback) */
    for (i = 0; i < NET_MAX; i++)
        if (RP[i].active && visible(RP[i].x, RP[i].z, 2)) {
            Style st;
            st.shirt = RGB(230, 120, 30); st.pants = RGB(40, 40, 50); st.skin = RGB(205, 160, 125); st.wf = 1; st.hf = 1; st.zombie = 0;
            if (!RP[i].in_car) { person(RP[i].x, RP[i].z, RP[i].yaw, RP[i].anim, 1.0f, &st, 0); marker(RP[i].x, RP[i].z, 2.1f, RGB(255, 120, 30)); }
        }
    /* carros (sprites) */
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    for (i = 0; i < MAX_CARS; i++) {
        const Car *c = &C[i];
        if (c->hp == 0.0f || P.in_car == i || dist2(c->x, c->z, cam_x, cam_z) > 190.0f * 190.0f || !visible(c->x, c->z, 4)) continue;
        car_bb(c->x, c->z, c->type, c->yaw, c->state == 2 ? RGB(80, 80, 80) : RGB(255, 255, 255));
    }
    for (i = 0; i < NET_MAX; i++)
        if (RP[i].active && RP[i].in_car && visible(RP[i].x, RP[i].z, 4)) car_bb(RP[i].x, RP[i].z, RP[i].car_type, RP[i].yaw, RGB(255, 255, 255));
    sceGuDisable(GU_ALPHA_TEST);
}

/* arma em primeira pessoa (espaco de camera) */
static void draw_viewmodel(void)
{
    float bobx = sinf(P.bob) * 0.012f * (P.speed > 0.5f ? 1.0f : 0.15f);
    float boby = fabsf(cosf(P.bob)) * 0.010f * (P.speed > 0.5f ? 1.0f : 0.15f);
    float kick = P.recoil * 0.9f + (P.flash > 0 ? 0.03f : 0.0f);
    float ax = lerpf(0.20f, 0.0f, P.aim) + bobx, ay = lerpf(-0.24f, -0.17f, P.aim) - boby, az = -0.55f + kick;
    unsigned int steel = RGB(55, 58, 64), dark = RGB(28, 28, 32), wood = RGB(110, 70, 38), skin = RGB(205, 160, 125), sleeve = RGB(60, 90, 70);
    float reload_dip = P.reloading ? 0.10f : 0.0f;
    ay -= reload_dip;
    g_cs = 1;
    sceGuClearDepth(0);
    sceGuClear(GU_DEPTH_BUFFER_BIT);
    sceGuDisable(GU_FOG);
    tex_bind(T_WHITE, 0, 0);
    switch (P.weapon) {
    case 0:
        box(ax, ay + 0.00f, az - 0.12f, 0.05f, 0.06f, 0.26f, 0, steel);
        box(ax, ay - 0.11f, az - 0.02f, 0.045f, 0.12f, 0.07f, 0, dark);
        box(ax, ay - 0.15f, az + 0.02f, 0.07f, 0.07f, 0.09f, 0, skin);
        box(ax + 0.02f, ay - 0.30f, az + 0.25f, 0.10f, 0.10f, 0.50f, 0, sleeve);
        break;
    case 1:
        box(ax, ay + 0.02f, az - 0.35f, 0.045f, 0.045f, 0.62f, 0, steel);
        box(ax, ay - 0.025f, az - 0.20f - (P.flash > 0 ? 0.0f : 0.04f), 0.07f, 0.05f, 0.20f, 0, wood);
        box(ax, ay - 0.04f, az + 0.10f, 0.06f, 0.10f, 0.28f, 0, wood);
        box(ax - 0.01f, ay - 0.08f, az - 0.20f, 0.08f, 0.06f, 0.10f, 0, skin);
        box(ax + 0.02f, ay - 0.30f, az + 0.30f, 0.10f, 0.10f, 0.50f, 0, sleeve);
        break;
    default:
        box(ax, ay + 0.00f, az - 0.20f, 0.06f, 0.09f, 0.60f, 0, steel);
        box(ax, ay + 0.03f, az - 0.62f, 0.03f, 0.03f, 0.30f, 0, dark);
        box(ax, ay - 0.14f, az - 0.10f, 0.04f, 0.15f, 0.07f, 0, dark);
        box(ax, ay + 0.09f, az - 0.15f, 0.04f, 0.05f, 0.18f, 0, dark);
        box(ax, ay - 0.02f, az + 0.25f, 0.06f, 0.10f, 0.28f, 0, wood);
        box(ax - 0.01f, ay - 0.07f, az - 0.28f, 0.08f, 0.06f, 0.10f, 0, skin);
        box(ax + 0.02f, ay - 0.30f, az + 0.40f, 0.10f, 0.10f, 0.50f, 0, sleeve);
        break;
    }
    if (P.flash > 0) {
        float tip = P.weapon == 0 ? -0.28f : (P.weapon == 1 ? -0.68f : -0.80f);
        box(ax, ay + 0.005f, az + tip, 0.07f, 0.07f, 0.12f, 0, RGB(255, 235, 120));
        box(ax, ay + 0.015f, az + tip - 0.08f, 0.03f, 0.03f, 0.08f, 0, RGB(255, 255, 220));
    }
    g_cs = 0;
}

static void draw_crosshair(void)
{
    float cx = SCR_W * 0.5f, cy = SCR_H * 0.5f;
    float gap = (float)CROSS_GAP[P.weapon] + P.bloom * 14.0f + (P.speed > 0.5f ? 4.0f : 0.0f);
    unsigned int col = G.aim_hit ? RGBA(255, 50, 40, 255) : RGBA(255, 255, 255, 230);
    unsigned int sh = RGBA(0, 0, 0, 160);
    float len = P.weapon == 1 ? 6.0f : 7.0f;
    if (P.aim > 0.5f) gap *= 0.5f;
    if (P.reloading) col = RGBA(255, 200, 40, 230);
    /* sombra */
    rect(cx - gap - len + 1, cy - 0.5f + 1, len, 2, sh); rect(cx + gap + 1, cy - 0.5f + 1, len, 2, sh);
    rect(cx - 0.5f + 1, cy - gap - len + 1, 2, len, sh); rect(cx - 0.5f + 1, cy + gap + 1, 2, len, sh);
    rect(cx - gap - len, cy - 0.5f, len, 2, col); rect(cx + gap, cy - 0.5f, len, 2, col);
    if (P.weapon != 2) rect(cx - 0.5f, cy - gap - len, 2, len, col);
    rect(cx - 0.5f, cy + gap, 2, len, col);
    rect(cx - 1, cy - 1, 2, 2, col);
    if (P.weapon == 1) { /* espingarda: cantos do circulo de dispersao */
        float g2 = gap + len + 4.0f;
        rect(cx - g2, cy - g2, 4, 2, col); rect(cx - g2, cy - g2, 2, 4, col);
        rect(cx + g2 - 4, cy - g2, 4, 2, col); rect(cx + g2 - 2, cy - g2, 2, 4, col);
        rect(cx - g2, cy + g2 - 2, 4, 2, col); rect(cx - g2, cy + g2 - 4, 2, 4, col);
        rect(cx + g2 - 4, cy + g2 - 2, 4, 2, col); rect(cx + g2 - 2, cy + g2 - 4, 2, 4, col);
    }
    if (P.hitmark > 0) { /* marcador de acerto */
        unsigned int hm = RGBA(255, 60, 40, 255);
        int k;
        for (k = 4; k < 10; k += 2) {
            rect(cx - k - 1, cy - k - 1, 2, 2, hm); rect(cx + k - 1, cy - k - 1, 2, 2, hm);
            rect(cx - k - 1, cy + k - 1, 2, 2, hm); rect(cx + k - 1, cy + k - 1, 2, 2, hm);
        }
    }
}

static void draw_radar(void)
{
    float cx = 436.0f, cy = 50.0f, R = 38.0f, sc = R / 70.0f;
    int i;
    rect(cx - R - 2, cy - R - 2, R * 2 + 4, R * 2 + 4, RGBA(0, 0, 0, 150));
    rect(cx - R, cy - R, R * 2, R * 2, RGBA(30, 40, 30, 150));
    rect(cx - R, cy - 0.5f, R * 2, 1, RGBA(90, 110, 90, 120));
    rect(cx - 0.5f, cy - R, 1, R * 2, RGBA(90, 110, 90, 120));
#define RDOT(wx, wz, col, sz)                                                                 \
    do {                                                                                      \
        float ddx = (wx) - P.x, ddz = (wz) - P.z;                                             \
        float rr = ddx * cam_cs - ddz * cam_sn, ff = -ddx * cam_sn - ddz * cam_cs;            \
        float px_ = cx + rr * sc, py_ = cy - ff * sc;                                         \
        if (fabsf(px_ - cx) < R - 1 && fabsf(py_ - cy) < R - 1) rect(px_ - (sz) * 0.5f, py_ - (sz) * 0.5f, (sz), (sz), (col)); \
    } while (0)
    for (i = 0; i < MAX_CARS; i++) if (C[i].hp > 0) RDOT(C[i].x, C[i].z, RGBA(240, 200, 40, 255), 3.0f);
    for (i = 0; i < nsurv; i++) if (S[i].state < 2 && !S[i].inside) RDOT(S[i].x, S[i].z, S[i].state == 0 ? RGBA(70, 160, 255, 255) : RGBA(70, 255, 110, 255), 3.0f);
    for (i = 0; i < MAX_ZOMBIES; i++) if (Z[i].alive && Z[i].hp > 0) RDOT(Z[i].x, Z[i].z, RGBA(255, 50, 50, 255), 2.0f);
#undef RDOT
    rect(cx - 2, cy - 2, 4, 4, RGBA(255, 255, 255, 255));
    rect(cx - 1, cy - 6, 2, 3, RGBA(255, 255, 255, 255));
}

static void draw_hud(void)
{
    const WeaponDef *w = &WEAP[P.weapon];
    int i;
    begin2d();
    if (P.dmg_flash > 0) rect(0, 0, SCR_W, SCR_H, RGBA(200, 0, 0, (int)(P.dmg_flash * 170)));
    if (P.in_car < 0) draw_crosshair();
    /* barras */
    rect(8, 238, 104, 12, RGBA(0, 0, 0, 150));
    rect(10, 240, P.hp, 8, P.hp > 35 ? RGBA(210, 40, 40, 230) : RGBA(255, 60, 20, 255));
    rect(8, 254, 104, 8, RGBA(0, 0, 0, 150));
    rect(10, 256, P.stamina, 4, RGBA(230, 200, 50, 220));
    if (P.in_car >= 0) {
        const Car *c = &C[P.in_car];
        float hpn = clampf(c->hp / (float)CARS[c->type].hp, 0, 1);
        rect(0, 226, SCR_W, 46, RGBA(30, 30, 34, 235));
        rect(0, 224, SCR_W, 3, RGBA(90, 90, 96, 255));
        for (i = 0; i < 12; i++) { /* volante */
            float a = i * (2 * PI / 12.0f);
            rect(240 + cosf(a) * 30 - 3, 276 + sinf(a) * 24 - 3, 7, 7, RGBA(15, 15, 18, 255));
        }
        rect(236, 262, 8, 10, RGBA(60, 60, 70, 255));
        spr2d(T_CAR + c->type, 330, 214, 128, 64, 0, RGBA(255, 255, 255, 255));
        begin2d();
        rect(330, 262, 100, 5, RGBA(0, 0, 0, 160));
        rect(330, 262, 100 * hpn, 5, hpn > 0.35f ? RGBA(70, 200, 90, 255) : RGBA(230, 60, 40, 255));
        text(130, 240, 0xFFFFFFFF, "%3d KM/H", (int)(fabsf(c->speed) * 3.6f));
        text(130, 252, 0xFFB0B0B0, "%s", CARS[c->type].name);
    } else {
        rect(366, 238, 108, 28, RGBA(0, 0, 0, 140));
        text(370, 240, 0xFFFFFFFF, "%s", w->name);
        text(370, 252, P.reloading ? 0xFF40E0FF : 0xFFFFFFFF, "%2d / %3d", P.mag[P.weapon], P.reserve[P.weapon]);
    }
    draw_radar();
    rect(4, 4, 150, 38, RGBA(0, 0, 0, 130));
    text(8, 6, 0xFF60FF60, "$ %d", sv.money);
    text(8, 16, 0xFFFFFFFF, "ONDA %d   MORTES %d", G.wave, G.kills);
    text(8, 26, 0xFFFFE060, "GRUPO %d/%d  KIT %d", count_follow(), MAX_FOLLOW, P.medkits);
    if (G.msg_t > 0) textc(60, 0xFF40E0FF, "%s", G.msg);
    if (G.prompt_kind == 1) textc(190, 0xFF60FF60, "[TRIANGULO] Recrutar %s", SURV_NAMES[S[G.prompt_idx].name]);
    if (G.prompt_kind == 2) textc(190, 0xFF40E0FF, "[TRIANGULO] Entrar: %s", CARS[C[G.prompt_idx].type].name);
    if (P.in_car >= 0) textc(206, 0xFFB0B0B0, "TRIANGULO sai do carro");
    if (P.reloading) textc(150, 0xFF40E0FF, "RECARREGANDO...");
    if (net.mode == 1) text(8, 44, 0xFFFFA040, "MP: loopback ativo");
}

static void draw_play(void)
{
    float eye, yaw, pitch, fov;
    float horizon, f;
    if (P.in_car >= 0) {
        const Car *c = &C[P.in_car];
        eye = 1.35f + sinf(G.time * 40.0f) * 0.004f * fabsf(c->speed);
        yaw = c->yaw; pitch = P.pitch;
        P.fov = lerpf(P.fov, 76.0f + fabsf(c->speed) * 0.25f, 0.1f);
    } else {
        eye = 1.65f + fabsf(cosf(P.bob)) * 0.05f * (P.speed > 0.5f ? 1.0f : 0.0f);
        yaw = P.yaw; pitch = P.pitch + P.recoil;
        P.fov = lerpf(P.fov, P.aim > 0.5f ? 50.0f : 68.0f, 0.2f);
    }
    if (P.dmg_flash > 0.2f) { yaw += frange(-0.01f, 0.01f); pitch += frange(-0.01f, 0.01f); }
    fov = P.fov;
    cam_set(P.x, eye, P.z, yaw, pitch);
    /* ceu */
    f = (SCR_H * 0.5f) / tanf(fov * PI / 360.0f);
    horizon = clampf(SCR_H * 0.5f + f * tanf(pitch), 0, SCR_H);
    begin2d();
    gradient(0, horizon > 1 ? horizon : 1, RGB(38, 48, 70), FOG_COL);
    if (horizon < SCR_H) rect(0, horizon, SCR_W, SCR_H - horizon, FOG_COL);
    begin3d(fov);
    draw_world();
    if (P.in_car < 0) draw_viewmodel();
    draw_hud();
}

/*------------------------------ menus -------------------------------*/
enum { ST_MENU, ST_SHOP, ST_LOBBY, ST_PLAY, ST_PAUSE, ST_DEAD };
static int state = ST_MENU;
static int menu_sel = 0, shop_tab = 0, shop_sel[3] = {0, 0, 0}, lobby_loop = 0, pause_sel = 0;
static float menu_t = 0;
static int running = 1;

static void bg_menu(void)
{
    begin2d();
    gradient(0, SCR_H, RGB(16, 18, 24), RGB(48, 28, 28));
    rect(0, 0, SCR_W, 3, RGBA(150, 20, 20, 255));
    rect(0, SCR_H - 3, SCR_W, 3, RGBA(150, 20, 20, 255));
}

static void draw_menu(void)
{
    static const char *items[4] = {"JOGAR", "LOJA", "MULTIPLAYER", "SAIR"};
    int i, car = (int)(menu_t / 2.5f) % CAR_COUNT;
    const char *t1 = "SOBREVIVENDO", *t2 = "EM GS";
    bg_menu();
    big_text((SCR_W - big_w(t1, 6)) / 2, 14, 6, RGBA(225, 225, 225, 255), t1);
    big_text((SCR_W - big_w(t2, 8)) / 2, 54, 8, RGBA(200, 30, 30, 255), t2);
    spr2d(T_CAR + car, 250, 110, 224, 112, 0, RGBA(255, 255, 255, 255));
    begin2d();
    textc(222, 0xFFB0B0B0, "%s", CARS[car].name);
    for (i = 0; i < 4; i++) {
        text(48, 120 + i * 24, i == menu_sel ? 0xFF40E0FF : 0xFFB0B0B0, "%s %s", i == menu_sel ? ">" : " ", items[i]);
    }
    text(8, 254, 0xFF60FF60, "$ %d   Melhor onda: %d   Mortes: %d", sv.money, sv.best_wave, sv.best_kills);
    text(8, 262, 0xFF909090, "Cima/Baixo = escolher   X = confirmar");
}

static int shop_count(int tab) { return tab == 0 ? 3 : (tab == 1 ? 4 : CAR_COUNT); }

static void shop_buy(void)
{
    int sel = shop_sel[shop_tab];
    if (shop_tab == 0) {
        static const int price[3] = {0, 1500, 3500};
        if (sv.wown[sel]) { say("Voce ja tem %s", WEAP[sel].name); return; }
        if (sv.money < price[sel]) { say("Dinheiro insuficiente"); return; }
        sv.money -= price[sel]; sv.wown[sel] = 1; sv.total[sel] += WEAP[sel].mag; say("Comprou %s!", WEAP[sel].name);
    } else if (shop_tab == 1) {
        static const int price[4] = {120, 200, 300, 150};
        if (sv.money < price[sel]) { say("Dinheiro insuficiente"); return; }
        if (sel == 3 && sv.medkits >= 5) { say("Maximo de 5 kits"); return; }
        sv.money -= price[sel];
        if (sel == 0) sv.total[0] += 36;
        else if (sel == 1) sv.total[1] += 16;
        else if (sel == 2) sv.total[2] += 60;
        else sv.medkits++;
        say("Compra realizada!");
    } else {
        if (sv.cown[sel]) { sv.selcar = sel; say("%s selecionado", CARS[sel].name); }
        else if (sv.money < CARS[sel].price) say("Dinheiro insuficiente");
        else { sv.money -= CARS[sel].price; sv.cown[sel] = 1; sv.selcar = sel; say("Comprou %s!", CARS[sel].name); }
    }
    save_write();
}

static void draw_shop(void)
{
    static const char *tabs[3] = {"ARMAS", "ITENS", "CARROS"};
    static const char *wn[3] = {"Pistola", "Espingarda", "Rifle"};
    static const int wp[3] = {0, 1500, 3500};
    static const char *in_[4] = {"Municao 9mm  x36", "Cartuchos     x16", "Municao rifle x60", "Kit medico     x1"};
    static const int ip[4] = {120, 200, 300, 150};
    int i, n = shop_count(shop_tab), sel = shop_sel[shop_tab], first = 0;
    bg_menu();
    big_text(16, 10, 5, RGBA(225, 225, 225, 255), "LOJA");
    text(300, 12, 0xFF60FF60, "$ %d", sv.money);
    for (i = 0; i < 3; i++) text(16 + i * 96, 44, i == shop_tab ? 0xFF40E0FF : 0xFF909090, "%s%s%s", i == shop_tab ? "[" : " ", tabs[i], i == shop_tab ? "]" : " ");
    text(16, 56, 0xFF707070, "L/R muda de aba");
    if (sel > 8) first = sel - 8;
    for (i = first; i < n && i < first + 9; i++) {
        int y = 72 + (i - first) * 16;
        unsigned int col = i == sel ? 0xFF40E0FF : 0xFFC0C0C0;
        if (shop_tab == 0)
            text(16, y, col, "%s %-11s %s", i == sel ? ">" : " ", wn[i], sv.wown[i] ? "COMPRADO" : "");
        else if (shop_tab == 1)
            text(16, y, col, "%s %s", i == sel ? ">" : " ", in_[i]);
        else
            text(16, y, col, "%s %-20s%s", i == sel ? ">" : " ", CARS[i].name, sv.selcar == i ? "*" : (sv.cown[i] ? "+" : " "));
    }
    if (shop_tab == 0) {
        text(16, 224, 0xFFB0B0B0, "Preco: %s", wp[sel] ? "" : "inicial");
        if (wp[sel]) text(16 + 7 * 8, 224, 0xFF60FF60, "$%d", wp[sel]);
        text(260, 80, 0xFFB0B0B0, "DANO %d x%d", (int)WEAP[sel].dmg, WEAP[sel].pellets);
        text(260, 92, 0xFFB0B0B0, "CARREGADOR %d", WEAP[sel].mag);
        text(260, 104, 0xFFB0B0B0, "TIRO %s", WEAP[sel].automatic ? "AUTOMATICO" : "SIMPLES");
        text(260, 116, 0xFFB0B0B0, "ALCANCE %dm", (int)WEAP[sel].range);
    } else if (shop_tab == 1) {
        text(16, 224, 0xFFB0B0B0, "Preco:");
        text(16 + 7 * 8, 224, 0xFF60FF60, "$%d", ip[sel]);
        text(260, 80, 0xFFB0B0B0, "Estoque 9mm ..... %d", sv.total[0]);
        text(260, 92, 0xFFB0B0B0, "Estoque cartucho. %d", sv.total[1]);
        text(260, 104, 0xFFB0B0B0, "Estoque rifle ... %d", sv.total[2]);
        text(260, 116, 0xFFB0B0B0, "Kits medicos .... %d/5", sv.medkits);
    } else {
        spr2d(T_CAR + sel, 250, 56, 224, 112, 0, RGBA(255, 255, 255, 255));
        begin2d();
        text(260, 176, 0xFFFFFFFF, "%s", CARS[sel].name);
        text(260, 188, 0xFFB0B0B0, "VEL %d KM/H   VIDA %d", (int)(CARS[sel].vmax * 3.6f), CARS[sel].hp);
        if (sv.selcar == sel) text(260, 200, 0xFF40E0FF, "SELECIONADO");
        else if (sv.cown[sel]) text(260, 200, 0xFF60FF60, "COMPRADO - X seleciona");
        else text(260, 200, 0xFF60FF60, "PRECO $%d", CARS[sel].price);
    }
    if (G.msg_t > 0) textc(236, 0xFF40E0FF, "%s", G.msg);
    text(8, 262, 0xFF909090, "X = comprar/equipar   O = voltar   (mais itens em breve)");
}

static void draw_lobby(void)
{
    int i;
    bg_menu();
    big_text(16, 10, 5, RGBA(225, 225, 225, 255), "MULTIPLAYER");
    text(16, 52, 0xFFFFA040, "Wi-Fi local (Ad-Hoc) ate 4 jogadores: EM BREVE");
    text(16, 64, 0xFF909090, "Protocolo v%d - pacotes de %d bytes a 20 Hz", NET_VERSION, (int)sizeof(NetPacket));
    for (i = 0; i < NET_MAX; i++) {
        int y = 96 + i * 18;
        if (i == 0) text(32, y, 0xFF60FF60, "Slot 1: VOCE (host)");
        else if (i == 1 && lobby_loop) text(32, y, 0xFFFFA040, "Slot 2: JOGADOR-FANTASMA (loopback)");
        else text(32, y, 0xFF707070, "Slot %d: aguardando...", i + 1);
    }
    text(32, 180, lobby_loop ? 0xFF40E0FF : 0xFFB0B0B0, "[Quadrado] Teste loopback: %s", lobby_loop ? "LIGADO" : "DESLIGADO");
    text(32, 200, 0xFFFFFFFF, "[X] Iniciar jogo   [O] Voltar");
}

static void end_run(void)
{
    int i;
    for (i = 0; i < 3; i++) sv.total[i] = P.reserve[i] + P.mag[i];
    sv.medkits = P.medkits;
    if (G.wave > sv.best_wave) sv.best_wave = G.wave;
    if (G.kills > sv.best_kills) sv.best_kills = G.kills;
    save_write();
}

static void game_start(void)
{
    int i;
    memset(&P, 0, sizeof(P));
    memset(&G, 0, sizeof(G));
    memset(RP, 0, sizeof(RP));
    memset(&net, 0, sizeof(net));
    net.mode = lobby_loop ? 1 : 0;
    world_gen_entities();
    P.x = 50.0f; P.z = 53.0f; P.yaw = -PI / 2; P.hp = 100; P.stamina = 100; P.in_car = -1; P.fov = 68.0f;
    for (i = 0; i < 3; i++) {
        int total = sv.total[i], m;
        if (i == 0 && total < 48) total = 48;
        m = total < WEAP[i].mag ? total : WEAP[i].mag;
        P.mag[i] = m; P.reserve[i] = total - m;
    }
    P.medkits = sv.medkits;
    G.wave = 1; G.spawn_t = 2.0f;
    say("Sobreviva! Seu carro esta ao lado.");
}

static void draw_overlay_text(const char *big, unsigned int col, int sc)
{
    begin2d();
    rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 150));
    big_text((SCR_W - big_w(big, sc)) / 2, 50, sc, col, big);
}

/*------------------------------ main --------------------------------*/
static void gfx_init(void)
{
    pspDebugScreenInit();
    pspDebugScreenEnableBackColor(0);
    sceGuInit();
    sceGuStart(GU_DIRECT, dlist);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FRAME_SIZE, BUF_W);
    sceGuDepthBuffer((void *)(FRAME_SIZE * 2), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void frame_begin(void)
{
    sceGuStart(GU_DIRECT, dlist);
    sceGuClearColor(FOG_COL);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);
}

static void frame_end(void)
{
    sceGuFinish();
    sceGuSync(0, 0);
    text_flush();
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    draw_off = draw_off ? 0 : FRAME_SIZE;
}

int main(int argc, char *argv[])
{
    SceInt64 last;
    Input in;
    (void)argc; (void)argv;
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    gen_textures();
    gfx_init();
    save_load();
    world_gen_static();
    memset(&G, 0, sizeof(G));
    memset(&P, 0, sizeof(P));
    P.in_car = -1;
    last = sceKernelGetSystemTimeWide();

    while (running) {
        SceInt64 now = sceKernelGetSystemTimeWide();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.001f) dt = 0.001f;
        read_input(&in);
        menu_t += dt;
        if (G.msg_t > 0 && state != ST_PLAY) G.msg_t -= dt;

        switch (state) {
        case ST_MENU:
            if (in.p & PSP_CTRL_DOWN) menu_sel = (menu_sel + 1) % 4;
            if (in.p & PSP_CTRL_UP) menu_sel = (menu_sel + 3) % 4;
            if (in.p & PSP_CTRL_CROSS) {
                if (menu_sel == 0) { lobby_loop = 0; game_start(); state = ST_PLAY; }
                else if (menu_sel == 1) state = ST_SHOP;
                else if (menu_sel == 2) state = ST_LOBBY;
                else running = 0;
            }
            frame_begin(); draw_menu(); frame_end();
            break;
        case ST_SHOP:
            if (in.p & PSP_CTRL_RTRIGGER) shop_tab = (shop_tab + 1) % 3;
            if (in.p & PSP_CTRL_LTRIGGER) shop_tab = (shop_tab + 2) % 3;
            if (in.p & PSP_CTRL_DOWN) shop_sel[shop_tab] = (shop_sel[shop_tab] + 1) % shop_count(shop_tab);
            if (in.p & PSP_CTRL_UP) shop_sel[shop_tab] = (shop_sel[shop_tab] + shop_count(shop_tab) - 1) % shop_count(shop_tab);
            if (in.p & PSP_CTRL_CROSS) shop_buy();
            if (in.p & PSP_CTRL_CIRCLE) { state = ST_MENU; save_write(); }
            frame_begin(); draw_shop(); frame_end();
            break;
        case ST_LOBBY:
            if (in.p & PSP_CTRL_SQUARE) lobby_loop = !lobby_loop;
            if (in.p & PSP_CTRL_CROSS) { game_start(); state = ST_PLAY; }
            if (in.p & PSP_CTRL_CIRCLE) { lobby_loop = 0; state = ST_MENU; }
            frame_begin(); draw_lobby(); frame_end();
            break;
        case ST_PLAY:
            if (in.p & PSP_CTRL_START) { state = ST_PAUSE; pause_sel = 0; }
            else sim_play(&in, dt);
            if (P.hp <= 0) { end_run(); state = ST_DEAD; }
            frame_begin(); draw_play(); frame_end();
            break;
        case ST_PAUSE:
            if (in.p & (PSP_CTRL_UP | PSP_CTRL_DOWN)) pause_sel ^= 1;
            if (in.p & PSP_CTRL_START) state = ST_PLAY;
            if (in.p & PSP_CTRL_CROSS) {
                if (pause_sel == 0) state = ST_PLAY; else { end_run(); state = ST_MENU; }
            }
            frame_begin(); draw_play();
            draw_overlay_text("PAUSA", RGBA(225, 225, 225, 255), 8);
            text(176, 140, pause_sel == 0 ? 0xFF40E0FF : 0xFFB0B0B0, "%s Continuar", pause_sel == 0 ? ">" : " ");
            text(176, 156, pause_sel == 1 ? 0xFF40E0FF : 0xFFB0B0B0, "%s Sair para o menu", pause_sel == 1 ? ">" : " ");
            frame_end();
            break;
        case ST_DEAD:
            if (in.p & PSP_CTRL_CROSS) state = ST_MENU;
            frame_begin(); draw_play();
            draw_overlay_text("VOCE MORREU", RGBA(200, 30, 30, 255), 7);
            textc(130, 0xFFFFFFFF, "Onda %d   Mortes %d   Recrutados %d", G.wave, G.kills, G.recruited);
            textc(146, 0xFF60FF60, "Dinheiro: $%d", sv.money);
            textc(170, 0xFF40E0FF, "Pressione X para voltar ao menu");
            frame_end();
            break;
        }
    }
    save_write();
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
