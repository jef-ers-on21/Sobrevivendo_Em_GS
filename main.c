/*
 * SOBREVIVENDO EM GS - PSP (PSPSDK) - acao e sobrevivencia em mundo aberto 3D
 * Desenvolvido por MECANIC ENGINE GAMER
 *
 * Controles:
 *   Analogico  : andar            L / R : girar camera
 *   X          : interagir (pegar arma, revistar, beber, acampamento, helicoptero)
 *   Quadrado   : atacar (segure para armas automaticas)   Circulo : rolar (esquiva)
 *   Triangulo  : trocar de arma   Esq/Dir : escolher item   Cima : usar item
 *   Start      : menu (salvar/sair)       Select : ajuda
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspaudio.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("SobrevivendoEmGS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define BUF_W   512
#define SCR_W   480
#define SCR_H   272
#define FRAME   0x88000
#define PI_F    3.14159265f
#define WORLD   130.0f

typedef struct { float nx, ny, nz, x, y, z; } Vtx;
#define VTX_TYPE (GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static unsigned int __attribute__((aligned(16))) list[262144];
static Vtx __attribute__((aligned(16))) cube[36];
#define GTILE 12.0f
#define GN    22
static Vtx __attribute__((aligned(16))) groundA[GN * GN / 2 * 6];
static Vtx __attribute__((aligned(16))) groundB[GN * GN / 2 * 6];

/* ---------- callbacks de saida ---------- */
static volatile int running = 1;
static int exit_cb(int a, int b, void *c) { running = 0; return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

/* ---------- cubo unitario (base em y=0, centrado em x/z) ---------- */
static void build_cube(void) {
    static const float F[6][9] = {
        { 1, 0, 0,   0, 1, 0,   0, 0, 1},   /* +X */
        {-1, 0, 0,   0, 0, 1,   0, 1, 0},   /* -X */
        { 0, 1, 0,   0, 0, 1,   1, 0, 0},   /* +Y */
        { 0,-1, 0,   1, 0, 0,   0, 0, 1},   /* -Y */
        { 0, 0, 1,   1, 0, 0,   0, 1, 0},   /* +Z */
        { 0, 0,-1,   0, 1, 0,   1, 0, 0}    /* -Z */
    };
    static const int S[6][2] = {{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    int i = 0;
    for (int f = 0; f < 6; f++) {
        const float *d = F[f];
        for (int k = 0; k < 6; k++) {
            float su = S[k][0] * 0.5f, sv = S[k][1] * 0.5f;
            cube[i].nx = d[0]; cube[i].ny = d[1]; cube[i].nz = d[2];
            cube[i].x = d[0]*0.5f + d[3]*su + d[6]*sv;
            cube[i].y = 0.5f + d[1]*0.5f + d[4]*su + d[7]*sv;
            cube[i].z = d[2]*0.5f + d[5]*su + d[8]*sv;
            i++;
        }
    }
    sceKernelDcacheWritebackAll();
}

/* chao: ladrilhos pequenos (pecas gigantes bugam no PSP) */
static void build_ground(void) {
    int na = 0, nb = 0;
    for (int i = 0; i < GN; i++) {
        for (int j = 0; j < GN; j++) {
            float x0 = -(GN * GTILE) / 2.0f + i * GTILE, z0 = -(GN * GTILE) / 2.0f + j * GTILE;
            float x1 = x0 + GTILE, z1 = z0 + GTILE;
            float xs[6] = { x0, x1, x1, x0, x1, x0 };
            float zs[6] = { z0, z0, z1, z0, z1, z1 };
            Vtx *v = ((i + j) & 1) ? &groundB[nb] : &groundA[na];
            for (int k = 0; k < 6; k++) {
                v[k].nx = 0; v[k].ny = 1; v[k].nz = 0;
                v[k].x = xs[k]; v[k].y = 0; v[k].z = zs[k];
            }
            if ((i + j) & 1) nb += 6; else na += 6;
        }
    }
    sceKernelDcacheWritebackAll();
}

static unsigned int groundC1 = 0xff3c9a3c, groundC2 = 0xff328a34;

static void draw_ground(void) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuColor(groundC1);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, GN * GN / 2 * 6, 0, groundA);
    sceGuColor(groundC2);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, GN * GN / 2 * 6, 0, groundB);
}

/* desenha caixa: posicao (base), escala, rotacao Y, cor ABGR */
static void box(float x, float y, float z, float sx, float sy, float sz,
                float ry, unsigned int color) {
    ScePspFVector3 p = {x, y, z}, s = {sx, sy, sz};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&p);
    sceGumRotateY(ry);
    sceGumScale(&s);
    sceGumUpdateMatrix();
    sceGuColor(color);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, 36, 0, cube);
}


/* ---------- audio (musica + efeitos) ---------- */
static volatile int musicOn = 1;
static volatile int sfxQ = 0;
static volatile int songId = 0;   /* 0 menu, 1 dia, 2 noite, 3 combate, 4 final */

static const unsigned char MEL0[][2] = {
    {69,2},{72,2}, {76,2},{72,2}, {65,2},{69,2}, {72,2},{69,2},
    {67,2},{71,2}, {74,2},{71,2}, {64,2},{68,2}, {69,4}
};
static const unsigned char MEL1[][2] = {
    {69,1},{76,1},{72,1},{76,1}, {65,1},{72,1},{69,1},{72,1},
    {67,1},{74,1},{71,1},{74,1}, {64,1},{71,1},{68,1},{71,1},
    {69,1},{76,1},{72,1},{76,1}, {65,1},{72,1},{69,1},{72,1},
    {67,1},{71,1},{74,1},{71,1}, {69,4}
};
static const unsigned char MEL2[][2] = {
    {57,4},{60,4},{53,4},{55,4},{57,4},{64,4},{56,4},{57,4}
};
static const unsigned char MEL3[][2] = {
    {69,1},{69,1},{72,1},{69,1}, {70,1},{70,1},{74,1},{70,1},
    {69,1},{69,1},{72,1},{76,1}, {75,1},{72,1},{70,1},{69,1},
    {69,1},{69,1},{72,1},{69,1}, {70,1},{70,1},{74,1},{70,1},
    {76,1},{75,1},{74,1},{72,1}, {69,4}
};
static const unsigned char MEL4[][2] = {
    {72,1},{76,1},{79,1},{76,1}, {71,1},{74,1},{79,1},{74,1},
    {69,1},{72,1},{76,1},{72,1}, {69,1},{72,1},{77,1},{72,1},
    {72,1},{76,1},{79,1},{84,1}, {83,1},{79,1},{74,1},{79,1},
    {77,1},{76,1},{74,1},{71,1}, {72,4}
};
static const unsigned char BASS_[5][8] = {
    { 45, 45, 41, 41, 43, 43, 40, 45 },
    { 45, 41, 43, 40, 45, 41, 43, 45 },
    { 45, 48, 41, 43, 45, 40, 44, 45 },
    { 45, 46, 45, 43, 45, 46, 40, 45 },
    { 48, 43, 45, 41, 48, 43, 43, 48 }
};
typedef struct { const unsigned char (*mel)[2]; int n; int step; int bassMode; float vol; } Song;
#define NM(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const Song SONGS[5] = {
    { MEL0, NM(MEL0), 18000, 0, 0.20f },
    { MEL1, NM(MEL1), 12500, 0, 0.21f },
    { MEL2, NM(MEL2), 22000, 0, 0.18f },
    { MEL3, NM(MEL3),  7500, 1, 0.24f },
    { MEL4, NM(MEL4), 14000, 0, 0.22f }
};
/* frequencia negativa = ruido */
static const float SFXF[13][3]  = { {0,0,0}, {-1,150,0}, {400,250,0}, {-1,0,0}, {880,1175,0}, {440,0,0}, {120,90,0},
                                    {80,70,0}, {300,450,0}, {523,659,784}, {160,0,0}, {1000,800,1000}, {-1,0,0} };
static const int   SFXMS[13][3] = { {0,0,0}, {60,50,0}, {30,45,0}, {55,0,0}, {60,100,0}, {40,0,0}, {90,90,0},
                                    {200,200,0}, {80,80,0}, {60,60,90}, {180,0,0}, {90,90,90}, {300,0,0} };

/* 1 tiro 2 golpe 3 acerto 4 coleta 5 clique 6 dano 7 gemido 8 rolar 9 abrir 10 erro 11 radio 12 helice */
static void play_sfx(int t) { sfxQ = t; }

static float midi_freq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

static int audio_thread(SceSize args, void *argp) {
    static short buf[1024 * 2] __attribute__((aligned(64)));
    static float mfreq[5][40], bfreq[5][8];
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    for (int sg = 0; sg < 5; sg++) {
        for (int i = 0; i < SONGS[sg].n && i < 40; i++) mfreq[sg][i] = midi_freq(SONGS[sg].mel[i][0]);
        for (int i = 0; i < 8; i++) bfreq[sg][i] = midi_freq(BASS_[sg][i]);
    }
    int cs = 0, mi = 0, mpos = 0, songStep = 0, sType = 0, sPos = 0;
    float mph = 0, bph = 0, sph = 0;
    unsigned int rng = 12345u;
    while (running) {
        int sid = songId;
        if (sid < 0 || sid > 4) sid = 0;
        if (sid != cs) { cs = sid; mi = 0; mpos = 0; songStep = 0; }
        const Song *sg = &SONGS[cs];
        for (int i = 0; i < 1024; i++) {
            float out = 0;
            if (musicOn) {
                int nlen = sg->mel[mi][1] * sg->step;
                float env = 1.0f - 0.6f * ((float)mpos / (float)nlen);
                float att = (mpos < 400) ? (mpos / 400.0f) : 1.0f;
                float bf = bfreq[cs][(songStep / 4) % 8];
                if (sg->bassMode && (songStep & 1)) bf *= 1.5f;
                mph += mfreq[cs][mi] / 44100.0f; if (mph >= 1.0f) mph -= 1.0f;
                bph += bf / 44100.0f; if (bph >= 1.0f) bph -= 1.0f;
                out += (4.0f * fabsf(mph - 0.5f) - 1.0f) * env * att * sg->vol;
                out += (4.0f * fabsf(bph - 0.5f) - 1.0f) * 0.14f;
                if (++mpos >= nlen) {
                    mpos = 0; songStep += sg->mel[mi][1];
                    if (++mi >= sg->n) { mi = 0; songStep = 0; }
                }
            }
            if (sfxQ) { sType = sfxQ; sfxQ = 0; sPos = 0; }
            if (sType) {
                int ms = sPos * 1000 / 44100, acc = 0, seg = -1;
                for (int k = 0; k < 3; k++) {
                    int d = SFXMS[sType][k];
                    if (d == 0) break;
                    if (ms < acc + d) { seg = k; break; }
                    acc += d;
                }
                if (seg < 0) sType = 0;
                else {
                    float f = SFXF[sType][seg];
                    float dec = 1.0f - 0.5f * (float)(ms - acc) / (float)SFXMS[sType][seg];
                    float v;
                    if (f < 0) { rng = rng * 1664525u + 1013904223u; v = ((rng >> 16) & 1) ? 1.0f : -1.0f; }
                    else { sph += f / 44100.0f; if (sph >= 1.0f) sph -= 1.0f; v = (sph < 0.5f) ? 1.0f : -1.0f; }
                    out += v * 0.15f * dec;
                    sPos++;
                }
            }
            float o = out * 30000.0f;
            if (o > 32000.0f) o = 32000.0f;
            if (o < -32000.0f) o = -32000.0f;
            buf[i * 2] = buf[i * 2 + 1] = (short)o;
        }
        sceAudioOutputBlocking(ch, 0x5000, buf);
    }
    sceAudioChRelease(ch);
    return 0;
}

/* ---------- dados do mundo ---------- */
#define NITEMS  11
#define NITEMS_OLD 8
#define NW      8
#define NWP     10
#define NLOOT   80
#define NTREES  220
#define NZ      40
#define NZREG   32
#define NB_BASE 13
#define NB      31
#define NROADS  8
#define NCARS   10
#define DAY_LEN 360.0f
#define SAFE_R  13.0f
#define BASE_X  0.0f
#define BASE_Z  0.0f
#define HELI_X  11.0f
#define HELI_Z  7.0f
#define LAKE_X  (-48.0f)
#define LAKE_Z  32.0f
#define LAKE_HX 14.0f
#define LAKE_HZ 10.0f

enum { I_MAD, I_SUC, I_PANO, I_COM, I_AGUA, I_BAND, I_MUN, I_PECA, I_CART, I_RIFLE, I_GAS };
static const char *itemName[NITEMS] = { "Madeira", "Sucata", "Pano", "Comida", "Agua", "Bandagem", "Municao", "Peca radio", "Cartuchos", "Balas rifle", "Gasolina" };
static const char *wName[NW] = { "Punhos", "Bastao", "Machado", "Facao", "Pistola", "SMG", "Escopeta", "Rifle" };
static const float wDmg[NW]   = { 8.0f, 20.0f, 38.0f, 28.0f, 28.0f, 16.0f, 14.0f, 70.0f };
static const float wRange[NW] = { 2.0f, 2.7f, 2.9f, 2.8f, 26.0f, 24.0f, 12.0f, 40.0f };
static const float wCD[NW]    = { 0.35f, 0.45f, 0.70f, 0.38f, 0.40f, 0.13f, 0.85f, 1.00f };
static const int   wAmmo[NW]  = { -1, -1, -1, -1, I_MUN, I_MUN, I_CART, I_RIFLE };
static const int   wAuto[NW]  = { 0, 0, 0, 0, 0, 1, 0, 0 };
static const int   wGive[NW]  = { 0, 0, 0, 0, 12, 30, 8, 6 };
static const char *survNames[8] = { "Rafa", "Bia", "Leo", "Ana", "Dudu", "Nina", "Caio", "Lia" };
static const char *diffNames[3] = { "Facil", "Normal", "Dificil" };
static const float diffDmg[3]   = { 0.6f, 1.0f, 1.5f };
static const float diffSpd[3]   = { 0.9f, 1.0f, 1.1f };

typedef struct { float x, z, hw, hd, h; unsigned int wall, roof; int door; } Bld;
static const Bld BLD[NB] = {
    { 22, -30, 4.0f, 3.0f, 4.0f, 0xff8a9aa8, 0xff303050, -1 },
    { 34, -30, 3.5f, 3.5f, 5.0f, 0xff7080a0, 0xff202830, -1 },
    { 46, -30, 4.0f, 3.0f, 4.0f, 0xff909098, 0xff303050, -1 },
    { 58, -30, 3.5f, 3.0f, 4.5f, 0xff8a9aa8, 0xff202830, -1 },
    { 22, -44, 3.5f, 3.5f, 5.0f, 0xff5a6a88, 0xff202830, 1 },
    { 34, -44, 4.5f, 3.0f, 4.0f, 0xff8090a0, 0xff303050, 1 },
    { 46, -44, 3.5f, 3.5f, 6.0f, 0xff707080, 0xff202830, 1 },
    { 58, -44, 4.0f, 3.0f, 4.0f, 0xff7080a0, 0xff303050, 1 },
    { -29, -55, 3.0f, 2.5f, 3.5f, 0xffc0c8d0, 0xff2020b0, 1 },
    { 60, 20, 4.5f, 3.5f, 5.5f, 0xff2828b0, 0xff303030, 1 },
    { -65, -15, 1.6f, 1.6f, 16.0f, 0xff707070, 0xff2020d0, 1 },
    { 62, 50, 4.0f, 3.0f, 4.0f, 0xff506040, 0xff304028, 1 },
    { 74, 56, 3.0f, 3.0f, 4.0f, 0xff506040, 0xff304028, 1 }
,
/* cidades distantes (zona sem zumbis) */
    { 84.0f, -68.0f, 3.5f, 3.0f, 5.0f, 0xff8a9aa8, 0xff303050, 1 },
    { 95.0f, -68.0f, 3.5f, 3.0f, 4.0f, 0xff7080a0, 0xff303050, 1 },
    { 106.0f, -68.0f, 3.5f, 3.0f, 5.0f, 0xff909098, 0xff303050, 1 },
    { 84.0f, -52.0f, 3.5f, 3.0f, 4.0f, 0xff8a9aa8, 0xff303050, -1 },
    { 95.0f, -52.0f, 3.5f, 3.0f, 5.0f, 0xff7080a0, 0xff303050, -1 },
    { 106.0f, -52.0f, 3.5f, 3.0f, 4.0f, 0xff909098, 0xff303050, -1 },
    { -111.0f, 44.0f, 3.5f, 3.0f, 5.0f, 0xff8a9aa8, 0xff303050, 1 },
    { -100.0f, 44.0f, 3.5f, 3.0f, 4.0f, 0xff7080a0, 0xff303050, 1 },
    { -89.0f, 44.0f, 3.5f, 3.0f, 5.0f, 0xff909098, 0xff303050, 1 },
    { -111.0f, 60.0f, 3.5f, 3.0f, 4.0f, 0xff8a9aa8, 0xff303050, -1 },
    { -100.0f, 60.0f, 3.5f, 3.0f, 5.0f, 0xff7080a0, 0xff303050, -1 },
    { -89.0f, 60.0f, 3.5f, 3.0f, 4.0f, 0xff909098, 0xff303050, -1 },
    { -41.0f, -116.0f, 3.5f, 3.0f, 5.0f, 0xff8a9aa8, 0xff303050, 1 },
    { -30.0f, -116.0f, 3.5f, 3.0f, 4.0f, 0xff7080a0, 0xff303050, 1 },
    { -19.0f, -116.0f, 3.5f, 3.0f, 5.0f, 0xff909098, 0xff303050, 1 },
    { -41.0f, -100.0f, 3.5f, 3.0f, 4.0f, 0xff8a9aa8, 0xff303050, -1 },
    { -30.0f, -100.0f, 3.5f, 3.0f, 5.0f, 0xff7080a0, 0xff303050, -1 },
    { -19.0f, -100.0f, 3.5f, 3.0f, 4.0f, 0xff909098, 0xff303050, -1 }
};
static const float ROADS[NROADS][4] = {
    { 0, 0, 0, -37 }, { 0, -37, 70, -37 }, { 0, 0, 0, 70 }, { -70, 0, 0, 0 },
    { 0, 0, 50, 0 }, { 50, 0, 50, 50 }, { 50, 50, 70, 50 }, { -35, 0, -35, -55 }
};
static const float CARS[NCARS][3] = {
    { 26, -37, 1.57f }, { 42, -36.5f, 1.4f }, { 58, -37.5f, 1.7f }, { 0, -22, 0.1f }, { 1, 35, 0 },
    { -1, 55, 0.2f }, { -25, 0.5f, 1.57f }, { -52, -0.5f, 1.5f }, { 22, 0, 1.57f }, { 50, 28, 0.1f }
};

typedef struct { float x, z; int kind, looted; float t; } Loot;
typedef struct { float x, z, hx, hz, hr, tx, tz, hp, ang, t, atk, hurt, dead, corpse; int type, state, active, horde; } Zomb;
typedef struct { float x, z; int w, taken; } WPick;
typedef struct { float x, z, r; int n, brutes; } Zone;
static const Zone ZONES[5] = {
    { 40, -37, 22, 10, 1 }, { 68, 53, 12, 5, 3 }, { -29, -55, 10, 3, 0 }, { 60, 20, 10, 3, 0 }, { -65, -15, 10, 3, 0 }
};
static const float zHP[3]    = { 60.0f, 40.0f, 170.0f };
static const float zSpeed[3] = { 3.0f, 6.0f, 2.3f };
static const float zDmg[3]   = { 9.0f, 7.0f, 22.0f };

static Loot loot[NLOOT]; static int nloot = 0;
static Zomb zomb[NZ];
static float treeX[NTREES], treeZ[NTREES], treeH[NTREES];
static int inv[NITEMS];
static int hasW[NW] = { 1, 0, 0, 0, 0, 0, 0, 0 };
static WPick wpick[NWP];
#define NSURV 10
#define MAXFOL 5
#define SV_RECRUIT_FOOD 2
typedef struct { float x, z, ang, hp, shootCD, hurt, corpse; int state, name; } Surv; /* state: 0 selvagem, 1 seguindo, 2 morto */
static Surv surv[NSURV];
static void init_survivors(void);
static void do_sleep(void);
#define CH_X (-9.0f)
#define CH_Z (4.5f)
static int inCar = -1;
static float carX[NCARS], carZ[NCARS], carAng[NCARS], carSpd[NCARS];
static int camMode = 1;
#define NCITY 3
#define CITY_R 24.0f
#define STN_X (-29.0f)
#define STN_Z (-52.0f)
static const float CITY[NCITY][2] = { { 95.0f, -60.0f }, { -100.0f, 52.0f }, { -30.0f, -108.0f } };
static const char *CITY_NAME[NCITY] = { "Vale Leste", "Morro Oeste", "Forte Sul" };
static int cityVis[NCITY];
static float carFuel[NCARS];
static int stnSel = 0;
static int near_station(void);
static float camSpeed = 1.0f;
static int curW = 0, selUse = 0, kills = 0, parts = 0, rescue = 0, deaths = 0, chasers = 0;
static int mode = 0, menuSel = 0, showHelp = 1, endingSeen = 0;
static float px = 3.0f, pz = 5.0f, cam = 0, pang = 0;
static float hp = 100, hunger = 100, thirst = 100, stam = 100;
static float gameTime = 0, rescueT = 0, hordeT = 0, attackCD = 0, swingT = 0, rollT = 0, rollX = 0, rollZ = 1;
static float hurtT = 0, shotT = 0, shotX1 = 0, shotZ1 = 0, shotX2 = 0, shotZ2 = 0, moanCD = 0, drinkCD = 0;
static float msgTimer = 0, deathT = 0;
static int facingRight = 1, moving = 0;
static int firstPerson = 0, coins = 0, shopSel = 0;
#define FIRE_X 0.0f
#define FIRE_Z 3.2f
static int fireLvl = 1, hasLant = 0, lantOn = 0;
static float fire_radius(void) { return fireLvl == 1 ? 8.0f : (fireLvl == 2 ? 13.0f : 20.0f); }
static char shopMsg[48] = "";
static float walkPhase = 0;
static char msg[64] = "", menuMsg[48] = "";
static unsigned int seed_rng = 12345;
static int worldSeed = 0, difficulty = 1, nameIdx = 0, curSlot = 0;

#define APP_SPLASH  0
#define APP_TITLE   1
#define APP_NEW     2
#define APP_LOAD    3
#define APP_CREDITS 4
#define APP_POST    5
#define APP_PLAY    6
static int appState = APP_SPLASH;
static float appT = 0, creditsY = 272.0f;
static int titleSel = 0, nwSel = 0, nwSlot = 0, nwName = 0, nwSeed = 0, nwDiff = 1;
static int slotSel = 0, delConfirm = -1, creditsFromGame = 0;
typedef struct { int used, nameIdx, day, parts, kills; } SlotInfo;
static SlotInfo slotInfos[3];

static float frand(void) {
    seed_rng = seed_rng * 1664525u + 1013904223u;
    return (float)((seed_rng >> 8) & 0xFFFF) / 65535.0f;
}
static float dist2d(float ax, float az, float bx, float bz) { float dx = ax - bx, dz = az - bz; return sqrtf(dx * dx + dz * dz); }
#define NEAR(x, z, r) (dist2d(px, pz, (x), (z)) < (r))
static unsigned int seed_for(int ws) { return ws <= 0 ? 12345u : (unsigned int)ws * 2654435761u + 12345u; }

static void say(const char *s) { strncpy(msg, s, sizeof(msg) - 1); msg[sizeof(msg) - 1] = 0; msgTimer = 3.0f; }

/* ---------- tempo ---------- */
static float hour_now(void) { return fmodf(gameTime / DAY_LEN * 24.0f + 8.0f, 24.0f); }
static float daylight(void) { return 0.5f + 0.5f * cosf((hour_now() - 13.0f) * (2.0f * PI_F / 24.0f)); }
static float nightness(void) { float n = (0.45f - daylight()) / 0.3f; return n < 0 ? 0 : (n > 1 ? 1 : n); }
static int day_number(void) { return (int)((gameTime / DAY_LEN * 24.0f + 8.0f) / 24.0f) + 1; }

/* ---------- geometria ---------- */
static int near_road(float x, float z, float m) {
    for (int i = 0; i < NROADS; i++) {
        float x1 = ROADS[i][0], z1 = ROADS[i][1], x2 = ROADS[i][2], z2 = ROADS[i][3];
        float minx = (x1 < x2 ? x1 : x2) - m - 2.5f, maxx = (x1 > x2 ? x1 : x2) + m + 2.5f;
        float minz = (z1 < z2 ? z1 : z2) - m - 2.5f, maxz = (z1 > z2 ? z1 : z2) + m + 2.5f;
        if (x >= minx && x <= maxx && z >= minz && z <= maxz) return 1;
    }
    return 0;
}

static void push_out_rect(float *x, float *z, float r, float cx, float cz, float hw, float hd) {
    float dx = *x - cx, dz = *z - cz;
    float ox = hw + r - fabsf(dx), oz = hd + r - fabsf(dz);
    if (ox > 0 && oz > 0) {
        if (ox < oz) *x += (dx < 0 ? -ox : ox); else *z += (dz < 0 ? -oz : oz);
    }
}
static void collide(float *x, float *z, float r) {
    for (int i = 0; i < NB; i++) {
        if (fabsf(*x - BLD[i].x) < 0.9f) continue;   /* vao da porta: da para entrar na casa */
        push_out_rect(x, z, r, BLD[i].x, BLD[i].z, BLD[i].hw, BLD[i].hd);
    }
    push_out_rect(x, z, r, LAKE_X, LAKE_Z, LAKE_HX, LAKE_HZ);
    push_out_rect(x, z, r, CH_X, CH_Z, 2.2f, 2.2f);
    for (int c = 0; c < NCARS; c++)
        if (c != inCar) push_out_rect(x, z, r, carX[c], carZ[c], 1.2f, 2.2f);
}
static void collide_trees(float *x, float *z) {
    for (int i = 0; i < NTREES; i++) {
        float dx = *x - treeX[i], dz = *z - treeZ[i];
        if (dx > 1.5f || dx < -1.5f || dz > 1.5f || dz < -1.5f) continue;
        float d = sqrtf(dx * dx + dz * dz);
        if (d < 0.9f && d > 0.001f) { *x = treeX[i] + dx / d * 0.9f; *z = treeZ[i] + dz / d * 0.9f; }
    }
}
static float lake_dist(float x, float z) {
    float dx = fabsf(x - LAKE_X) - LAKE_HX, dz = fabsf(z - LAKE_Z) - LAKE_HZ;
    if (dx < 0) dx = 0;
    if (dz < 0) dz = 0;
    return sqrtf(dx * dx + dz * dz);
}
static int blocked(float x, float z, float margin) {
    for (int i = 0; i < NB; i++)
        if (fabsf(x - BLD[i].x) < BLD[i].hw + margin && fabsf(z - BLD[i].z) < BLD[i].hd + margin) return 1;
    if (lake_dist(x, z) < margin + 1.0f) return 1;
    if (near_road(x, z, margin - 2.0f)) return 1;
    if (dist2d(x, z, BASE_X, BASE_Z) < SAFE_R + 4.0f) return 1;
    if (dist2d(x, z, 68, 53) < 19.0f) return 1;
    for (int c = 0; c < NCITY; c++) if (dist2d(x, z, CITY[c][0], CITY[c][1]) < CITY_R + margin) return 1;
    return 0;
}

/* ---------- mundo ---------- */
static void add_loot(float x, float z, int kind) {
    if (nloot >= NLOOT) return;
    loot[nloot].x = x; loot[nloot].z = z; loot[nloot].kind = kind; loot[nloot].looted = 0; loot[nloot].t = 0;
    nloot++;
}

static void respawn_zombie(Zomb *z) {
    float x, z2; int tries = 0;
    do {
        x = z->hx + (frand() * 2 - 1) * z->hr;
        z2 = z->hz + (frand() * 2 - 1) * z->hr;
    } while (++tries < 20 && (dist2d(x, z2, BASE_X, BASE_Z) < SAFE_R + 3.0f || lake_dist(x, z2) < 1.5f));
    z->x = x; z->z = z2; z->tx = x; z->tz = z2;
    z->hp = zHP[z->type]; z->state = 0; z->dead = 0; z->hurt = 0; z->atk = 0; z->t = frand() * 3.0f;
}

static void init_world(void) {
    nloot = 0;
    init_survivors();
    for (int c = 0; c < NCARS; c++) { carX[c] = CARS[c][0]; carZ[c] = CARS[c][1]; carAng[c] = CARS[c][2]; carSpd[c] = 0; carFuel[c] = 25.0f + frand() * 45.0f; }
    for (int c = 0; c < NCITY; c++) cityVis[c] = 0;
    inCar = -1;
    for (int i = 0; i < 8; i++) add_loot(BLD[i].x, BLD[i].z + BLD[i].door * (BLD[i].hd + 1.3f), 0);
    add_loot(-29, -51.2f, 0);                       /* posto */
    add_loot(60, 24.8f, 0);                         /* fazenda */
    add_loot(62, 53.5f, 2); add_loot(74, 59.5f, 2); add_loot(66, 60, 2);   /* militar */
    for (int i = 0; i < NCARS; i++) add_loot(CARS[i][0] + 2.2f, CARS[i][1] + 1.0f, 1);
    add_loot(-33, -58, 3); add_loot(66.5f, 20, 3); add_loot(-65, -11.5f, 3); add_loot(70, 48, 3);  /* pecas de radio */
    for (int i = 0; i < 8; i++) {
        float x, z; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 6); z = (frand() * 2 - 1) * (WORLD - 6); } while (++tries < 30 && blocked(x, z, 3.0f));
        add_loot(x, z, 0);
    }
    for (int i = 0; i < 14; i++) {
        float x, z; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 6); z = (frand() * 2 - 1) * (WORLD - 6); } while (++tries < 30 && (blocked(x, z, 3.0f) || dist2d(x, z, BASE_X, BASE_Z) < 16.0f));
        add_loot(x, z, 4);
    }
    for (int i = 0; i < NB_BASE; i++)   /* saque dentro de cada casa */
        add_loot(BLD[i].x, BLD[i].z - BLD[i].door * (BLD[i].hd * 0.35f), 5);
    for (int c = 0; c < NCITY; c++) add_loot(CITY[c][0], CITY[c][1], 2);   /* cache militar em cada cidade */
    for (int i = 0; i < NTREES; i++) {
        float x, z; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 3); z = (frand() * 2 - 1) * (WORLD - 3); } while (++tries < 40 && blocked(x, z, 4.0f));
        treeX[i] = x; treeZ[i] = z; treeH[i] = 3.5f + frand() * 3.0f;
    }
    memset(zomb, 0, sizeof(zomb));
    int zi = 0;
    for (int zn = 0; zn < 5; zn++)
        for (int k = 0; k < ZONES[zn].n && zi < NZREG; k++, zi++) {
            Zomb *z = &zomb[zi];
            z->hx = ZONES[zn].x; z->hz = ZONES[zn].z; z->hr = ZONES[zn].r; z->active = 1;
            z->type = (k < ZONES[zn].brutes) ? 2 : (frand() < 0.3f ? 1 : 0);
            respawn_zombie(z);
        }
    for (; zi < NZREG; zi++) {
        Zomb *z = &zomb[zi];
        float x, zz; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 8); zz = (frand() * 2 - 1) * (WORLD - 8); } while (++tries < 30 && (blocked(x, zz, 4.0f) || dist2d(x, zz, 0, 0) < 28.0f));
        z->hx = x; z->hz = zz; z->hr = 10.0f; z->active = 1; z->type = (frand() < 0.3f) ? 1 : 0;
        respawn_zombie(z);
    }
    /* armas coletaveis espalhadas pelo mundo */
    static const float WP[7][3] = { { -8, -2, 1 }, { 56, 14.5f, 2 }, { -31, -52, 3 }, { 30, -37, 4 }, { 64, 44, 5 }, { -62, -11, 6 }, { 78.5f, 43.5f, 7 } };
    for (int i = 0; i < 7; i++) { wpick[i].x = WP[i][0]; wpick[i].z = WP[i][1]; wpick[i].w = (int)WP[i][2]; wpick[i].taken = 0; }
    for (int i = 7; i < NWP; i++) {
        float x, zz; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 8); zz = (frand() * 2 - 1) * (WORLD - 8); } while (++tries < 30 && blocked(x, zz, 3.0f));
        wpick[i].x = x; wpick[i].z = zz; wpick[i].w = 1 + (int)(frand() * 4.0f); if (wpick[i].w > 4) wpick[i].w = 4; wpick[i].taken = 0;
    }
}

static void reset_state(void) {
    memset(inv, 0, sizeof(inv));
    for (int c = 0; c < NCITY; c++) cityVis[c] = 0;
    inv[I_COM] = 2; inv[I_AGUA] = 2; inv[I_BAND] = 1; inv[I_MAD] = 2; inv[I_MUN] = 0;
    for (int i = 0; i < NW; i++) hasW[i] = 0;
    hasW[0] = 1; curW = 0; selUse = 0;
    kills = 0; parts = 0; rescue = 0; deaths = 0; chasers = 0; mode = 0; menuSel = 0; showHelp = 1; endingSeen = 0;
    px = 3.0f; pz = 5.0f; cam = 0; pang = 0;
    hp = 100; hunger = 100; thirst = 100; stam = 100;
    gameTime = 0; rescueT = 0; hordeT = 0; attackCD = 0; swingT = 0; rollT = 0; hurtT = 0; shotT = 0;
    moanCD = 0; drinkCD = 0; msgTimer = 0; deathT = 0; msg[0] = 0; menuMsg[0] = 0;
    facingRight = 1; moving = 0; walkPhase = 0;
    coins = 0; firstPerson = 0;
    fireLvl = 1; hasLant = 0; lantOn = 0;
}

/* ---------- salvar / carregar (3 slots) ---------- */
#define SAVE_MAGIC  0x53564753u
#define SAVE_MAGIC2 0x32565347u
#define SAVE_DIR    "ms0:/PSP/SAVEDATA/SOBREVIVENDOGS"
#define SAVE_NLOOT 64
#define SAVE_NSURV 6
typedef struct {
    unsigned int magic; int version;
    int worldSeed, difficulty, nameIdx, kills, parts, rescue, curW, selUse, deaths, endingSeen;
    int inv[NITEMS_OLD], hasW[3];
    float px, pz, cam, gameTime, hp, hunger, thirst, stam, rescueT;
    unsigned char looted[SAVE_NLOOT]; float lootT[SAVE_NLOOT];
} SaveData;
typedef struct { unsigned int magic; int inv2[2]; int hasW[NW]; int curW; unsigned char taken[NWP]; } SaveExtra;
#define SAVE_MAGIC3 0x33565347u
typedef struct { unsigned int magic; unsigned char state[SAVE_NSURV]; float hp[SAVE_NSURV], x[SAVE_NSURV], z[SAVE_NSURV]; } SaveExtra3;
#define SAVE_MAGIC4 0x34565347u
typedef struct { unsigned int magic; int coins, firstPerson; } SaveExtra4;
static SaveExtra4 gExtra4; static int gHasExtra4 = 0;
#define SAVE_MAGIC5 0x35565347u
typedef struct { unsigned int magic; int fireLvl, hasLant, lantOn; } SaveExtra5;
static SaveExtra5 gExtra5; static int gHasExtra5 = 0;
#define SAVE_MAGIC6 0x36565347u
typedef struct { unsigned int magic; int gas; int cityVis[NCITY]; float carFuel[NCARS]; unsigned char looted2[16]; float lootT2[16]; } SaveExtra6;
static SaveExtra6 gExtra6; static int gHasExtra6 = 0;
static SaveExtra3 gSurv; static int gHasSurv = 0;

static void slot_path(int slot, char *out) { sprintf(out, SAVE_DIR "/save%d.dat", slot + 1); }

static int save_game(void) {
    SaveData d; memset(&d, 0, sizeof(d));
    d.magic = SAVE_MAGIC; d.version = 1;
    d.worldSeed = worldSeed; d.difficulty = difficulty; d.nameIdx = nameIdx; d.kills = kills; d.parts = parts;
    d.rescue = rescue; d.selUse = selUse; d.deaths = deaths; d.endingSeen = endingSeen;
    d.curW = (curW == 2) ? 1 : (curW == 4) ? 2 : 0;
    for (int i = 0; i < NITEMS_OLD; i++) d.inv[i] = inv[i];
    d.hasW[0] = hasW[1]; d.hasW[1] = hasW[2]; d.hasW[2] = hasW[4];
    d.px = px; d.pz = pz; d.cam = cam; d.gameTime = gameTime; d.hp = hp; d.hunger = hunger; d.thirst = thirst;
    d.stam = stam; d.rescueT = rescueT;
    for (int i = 0; i < nloot && i < SAVE_NLOOT; i++) { d.looted[i] = (unsigned char)loot[i].looted; d.lootT[i] = loot[i].t; }
    SaveExtra e; memset(&e, 0, sizeof(e));
    e.magic = SAVE_MAGIC2; e.inv2[0] = inv[I_CART]; e.inv2[1] = inv[I_RIFLE]; e.curW = curW;
    for (int i = 0; i < NW; i++) e.hasW[i] = hasW[i];
    for (int i = 0; i < NWP; i++) e.taken[i] = (unsigned char)wpick[i].taken;
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    char path[96]; slot_path(curSlot, path);
    int fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return 0;
    int w = sceIoWrite(fd, &d, sizeof(d));
    int w2 = sceIoWrite(fd, &e, sizeof(e));
    SaveExtra3 f3; memset(&f3, 0, sizeof(f3)); f3.magic = SAVE_MAGIC3;
    for (int i = 0; i < SAVE_NSURV; i++) { f3.state[i] = (unsigned char)surv[i].state; f3.hp[i] = surv[i].hp; f3.x[i] = surv[i].x; f3.z[i] = surv[i].z; }
    sceIoWrite(fd, &f3, sizeof(f3));
    SaveExtra4 f4; memset(&f4, 0, sizeof(f4)); f4.magic = SAVE_MAGIC4; f4.coins = coins; f4.firstPerson = firstPerson;
    sceIoWrite(fd, &f4, sizeof(f4));
    SaveExtra5 f5; memset(&f5, 0, sizeof(f5)); f5.magic = SAVE_MAGIC5; f5.fireLvl = fireLvl; f5.hasLant = hasLant; f5.lantOn = lantOn;
    sceIoWrite(fd, &f5, sizeof(f5));
    SaveExtra6 f6; memset(&f6, 0, sizeof(f6)); f6.magic = SAVE_MAGIC6; f6.gas = inv[I_GAS];
    for (int c = 0; c < NCITY; c++) f6.cityVis[c] = cityVis[c];
    for (int c = 0; c < NCARS; c++) f6.carFuel[c] = carFuel[c];
    for (int i = SAVE_NLOOT; i < nloot && i < SAVE_NLOOT + 16; i++) { f6.looted2[i - SAVE_NLOOT] = (unsigned char)loot[i].looted; f6.lootT2[i - SAVE_NLOOT] = loot[i].t; }
    sceIoWrite(fd, &f6, sizeof(f6));
    sceIoClose(fd);
    return w == (int)sizeof(d) && w2 == (int)sizeof(e);
}

static int read_slot(int slot, SaveData *d, SaveExtra *e, int *hasE) {
    gHasSurv = 0; gHasExtra4 = 0; gHasExtra5 = 0; gHasExtra6 = 0;
    char p[96]; slot_path(slot, p);
    int fd = sceIoOpen(p, PSP_O_RDONLY, 0777);
    if (fd < 0) return 0;
    int r = sceIoRead(fd, d, sizeof(*d));
    int r2 = sceIoRead(fd, e, sizeof(*e));
    SaveExtra3 f3; int r3 = sceIoRead(fd, &f3, sizeof(f3));
    gHasSurv = (r3 == (int)sizeof(f3) && f3.magic == SAVE_MAGIC3);
    if (gHasSurv) gSurv = f3;
    SaveExtra4 f4; int r4b = sceIoRead(fd, &f4, sizeof(f4));
    gHasExtra4 = (r4b == (int)sizeof(f4) && f4.magic == SAVE_MAGIC4);
    if (gHasExtra4) gExtra4 = f4;
    SaveExtra5 f5; int r5 = sceIoRead(fd, &f5, sizeof(f5));
    gHasExtra5 = (r5 == (int)sizeof(f5) && f5.magic == SAVE_MAGIC5);
    if (gHasExtra5) gExtra5 = f5;
    SaveExtra6 f6; int r6 = sceIoRead(fd, &f6, sizeof(f6));
    gHasExtra6 = (r6 == (int)sizeof(f6) && f6.magic == SAVE_MAGIC6);
    if (gHasExtra6) gExtra6 = f6;
    sceIoClose(fd);
    *hasE = (r2 == (int)sizeof(*e) && e->magic == SAVE_MAGIC2);
    return r == (int)sizeof(*d) && d->magic == SAVE_MAGIC && d->version == 1;
}

static void refresh_slots(void) {
    for (int s = 0; s < 3; s++) {
        SaveData d; SaveExtra e; int he; SlotInfo *si = &slotInfos[s]; memset(si, 0, sizeof(*si));
        if (!read_slot(s, &d, &e, &he)) continue;
        si->used = 1; si->nameIdx = (d.nameIdx >= 0 && d.nameIdx < 8) ? d.nameIdx : 0;
        si->day = (int)((d.gameTime / DAY_LEN * 24.0f + 8.0f) / 24.0f) + 1; si->parts = d.parts; si->kills = d.kills;
    }
}

static int load_game(void) {
    SaveData d; SaveExtra e; int he = 0;
    if (!read_slot(curSlot, &d, &e, &he)) return 0;
    reset_state();
    worldSeed = d.worldSeed; difficulty = (d.difficulty >= 0 && d.difficulty < 3) ? d.difficulty : 1;
    nameIdx = (d.nameIdx >= 0 && d.nameIdx < 8) ? d.nameIdx : 0;
    seed_rng = seed_for(worldSeed);
    init_world();
    kills = d.kills; parts = d.parts; rescue = (d.rescue == 1 || d.rescue == 2) ? 0 : d.rescue; selUse = d.selUse;
    deaths = d.deaths; endingSeen = d.endingSeen ? 1 : 0;
    for (int i = 0; i < NITEMS_OLD; i++) inv[i] = d.inv[i];
    if (he) {
        inv[I_CART] = e.inv2[0]; inv[I_RIFLE] = e.inv2[1];
        for (int i = 0; i < NW; i++) hasW[i] = e.hasW[i] ? 1 : 0;
        curW = e.curW;
        for (int i = 0; i < NWP; i++) wpick[i].taken = e.taken[i] ? 1 : 0;
    } else {
        static const int mp[3] = { 1, 2, 4 };
        hasW[1] = d.hasW[0]; hasW[2] = d.hasW[1]; hasW[4] = d.hasW[2];
        curW = (d.curW >= 0 && d.curW < 3) ? mp[d.curW] : 0;
    }
    hasW[0] = 1;
    if (curW < 0 || curW >= NW || !hasW[curW]) curW = 0;
    if (selUse < 0 || selUse > 2) selUse = 0;
    px = d.px; pz = d.pz; cam = d.cam; gameTime = d.gameTime; hp = d.hp; hunger = d.hunger; thirst = d.thirst;
    stam = d.stam; rescueT = d.rescueT;
    if (hp < 20) hp = 20;
    for (int i = 0; i < nloot && i < SAVE_NLOOT; i++) { loot[i].looted = d.looted[i] ? 1 : 0; loot[i].t = d.lootT[i]; }
    coins = gHasExtra4 ? gExtra4.coins : 0;
    firstPerson = gHasExtra4 ? (gExtra4.firstPerson ? 1 : 0) : 0;
    fireLvl = gHasExtra5 ? gExtra5.fireLvl : 1; if (fireLvl < 1 || fireLvl > 3) fireLvl = 1;
    hasLant = gHasExtra5 ? (gExtra5.hasLant ? 1 : 0) : 0;
    lantOn = gHasExtra5 ? (gExtra5.lantOn ? 1 : 0) : 0;
    inv[I_GAS] = gHasExtra6 ? gExtra6.gas : 0;
    for (int c = 0; c < NCITY; c++) cityVis[c] = gHasExtra6 ? gExtra6.cityVis[c] : 0;
    if (gHasExtra6) {
        for (int c = 0; c < NCARS; c++) carFuel[c] = gExtra6.carFuel[c];
        for (int i = 0; i < 16 && SAVE_NLOOT + i < nloot; i++) { loot[SAVE_NLOOT + i].looted = gExtra6.looted2[i]; loot[SAVE_NLOOT + i].t = gExtra6.lootT2[i]; }
    }
    if (gHasSurv) {
        int nf = 0;
        for (int i = 0; i < SAVE_NSURV; i++) {
            surv[i].state = gSurv.state[i] <= 2 ? gSurv.state[i] : 0;
            surv[i].hp = gSurv.hp[i] > 0 ? gSurv.hp[i] : 80;
            surv[i].x = gSurv.x[i]; surv[i].z = gSurv.z[i];
            if (surv[i].state == 1 && ++nf > MAXFOL) surv[i].state = 0;
        }
    }
    return 1;
}

static void create_world(void) {
    reset_state();
    worldSeed = nwSeed ? nwSeed : (int)(sceKernelGetSystemTimeLow() % 99u) + 1;
    nameIdx = nwName; difficulty = nwDiff; curSlot = nwSlot;
    seed_rng = seed_for(worldSeed);
    init_world();
    save_game();
}

static void check_ending(void) {
    endingSeen = 1; creditsFromGame = 1; appState = APP_CREDITS; creditsY = 272.0f; appT = 0; mode = 0;
    rescue = 0; save_game();
}

/* ---------- jogador ---------- */
static void open_loot(int i) {
    static const int wt[7] = { 3, 3, 3, 2, 2, 1, 2 };   /* madeira sucata pano comida agua bandagem municao */
    Loot *L = &loot[i]; int got[NITEMS]; memset(got, 0, sizeof(got)); int wdrop = -1, gotLant = 0;
    if (L->kind == 0) {
        int rolls = 2 + (frand() < 0.5f ? 1 : 0) + (dist2d(L->x, L->z, BASE_X, BASE_Z) > 45.0f ? 1 : 0);
        for (int r = 0; r < rolls; r++) {
            float tot = 16.0f, v = frand() * tot, acc = 0; int it = 0;
            for (int k = 0; k < 7; k++) { acc += wt[k]; if (v <= acc) { it = k; break; } it = k; }
            got[it] += 1 + (it < 3 ? (int)(frand() * 2) : 0);
        }
    } else if (L->kind == 1) {
        got[I_SUC] = 1 + (int)(frand() * 3);
        if (frand() < 0.4f) got[I_MUN] = 2 + (int)(frand() * 3);
        if (frand() < 0.25f) got[I_COM] = 1;
        if (frand() < 0.5f) got[I_GAS] = 1 + (int)(frand() * 2.0f);
    } else if (L->kind == 5) {
        int rolls = 2 + (frand() < 0.5f ? 1 : 0);
        for (int r = 0; r < rolls; r++) {
            float v = frand();
            if (v < 0.25f) got[I_COM] += 1;
            else if (v < 0.45f) got[I_AGUA] += 1;
            else if (v < 0.6f) got[I_BAND] += 1;
            else if (v < 0.8f) got[I_MUN] += 2 + (int)(frand() * 3.0f);
            else if (v < 0.9f) got[I_PANO] += 1 + (int)(frand() * 2.0f);
            else got[I_SUC] += 1 + (int)(frand() * 2.0f);
        }
        if (!hasLant && frand() < 0.12f) { hasLant = 1; gotLant = 1; }
    } else if (L->kind == 4) {
        int rolls = 1 + (frand() < 0.5f ? 1 : 0);
        for (int r = 0; r < rolls; r++) {
            float v = frand();
            if (v < 0.35f) got[I_COM] += 1;
            else if (v < 0.6f) got[I_AGUA] += 1;
            else if (v < 0.8f) got[I_BAND] += 1;
            else if (v < 0.92f) got[I_MUN] += 3 + (int)(frand() * 3.0f);
            else got[I_CART] += 2 + (int)(frand() * 2.0f);
        }
    } else if (L->kind == 2) {
        got[I_MUN] = 5 + (int)(frand() * 6);
        if (frand() < 0.6f) got[I_BAND] = 1;
        got[frand() < 0.5f ? I_COM : I_AGUA] = 1; got[I_SUC] += 1;
        if (frand() < 0.15f) { int c = 4 + (int)(frand() * 4.0f); if (c > 7) c = 7; if (!hasW[c]) wdrop = c; }
    } else {
        got[I_PECA] = 1; got[I_MUN] = 2 + (int)(frand() * 3); got[I_BAND] = 1;
    }
    char b[64]; b[0] = 0; int n = 0;
    for (int k = 0; k < NITEMS; k++) if (got[k]) {
        inv[k] += got[k]; if (k == I_PECA) parts++;
        char t[24]; sprintf(t, "%s%d %s", n ? ", " : "", got[k], itemName[k]);
        if (strlen(b) + strlen(t) < 40) strcat(b, t);
        n++;
    }
    if (wdrop >= 0) {
        hasW[wdrop] = 1; curW = wdrop; if (wAmmo[wdrop] >= 0) inv[wAmmo[wdrop]] += wGive[wdrop];
        char t[24]; sprintf(t, "%s%s", n ? ", " : "", wName[wdrop]); if (strlen(b) + strlen(t) < 40) strcat(b, t);
    }
    char m[64]; sprintf(m, "Achou: %s", b[0] ? b : "nada");
    say(m); if (gotLant) say("Achou uma Lanterna! SELECT liga e desliga."); L->looted = 1; L->t = (L->kind == 3) ? 1e9f : 600.0f; play_sfx(L->kind == 3 ? 11 : 9);
}

static void player_die(void) {
    mode = 3; deathT = 0; deaths++; play_sfx(10);
}

static void hurt_player(float dmg) {
    if (rollT > 0 || mode == 3) return;
    hp -= dmg * diffDmg[difficulty]; hurtT = 0.4f; play_sfx(6);
    if (hp <= 0) { hp = 0; player_die(); }
}

static void kill_zombie(Zomb *z) {
    z->state = 2; z->dead = 90.0f + frand() * 60.0f; z->corpse = 3.5f; kills++; coins += (z->type == 2) ? 5 : 2; play_sfx(3);
    if (z->type == 2) { int it = (frand() < 0.5f) ? I_MUN : I_CART; inv[it] += 3; char mm[48]; sprintf(mm, "Brutamontes caiu! +3 %s", itemName[it]); say(mm); }
    if (frand() < 0.45f) {
        int it = (int)(frand() * 5); static const int tbl[5] = { I_PANO, I_SUC, I_COM, I_MUN, I_MAD };
        inv[tbl[it]]++; char m[48]; sprintf(m, "Zumbi derrubado! +1 %s", itemName[tbl[it]]); say(m);
    }
    if (z->horde) z->active = 0;
}

static void damage_zombie(Zomb *z, float dmg, float fx, float fz) {
    z->hp -= dmg; z->hurt = 0.3f; z->x += fx * 0.8f; z->z += fz * 0.8f; z->state = 1;
    if (z->hp <= 0) kill_zombie(z); else play_sfx(3);
}

static int nearest_zombie(float maxd, float *outd) {
    int best = -1; float bd = maxd;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active || z->state == 2) continue;
        float d = dist2d(px, pz, z->x, z->z);
        if (d < bd) { bd = d; best = i; }
    }
    if (outd) *outd = bd;
    return best;
}

static int aim_target(float maxd, float maxAng) {
    int best = -1; float bd = maxd;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active || z->state == 2) continue;
        float dx = z->x - px, dz = z->z - pz, d = sqrtf(dx * dx + dz * dz);
        float da = atan2f(dx, dz) - pang;
        while (da > PI_F) da -= 2.0f * PI_F;
        while (da < -PI_F) da += 2.0f * PI_F;
        if (d < bd && fabsf(da) < maxAng) { bd = d; best = i; }
    }
    return best;
}

static float ray_hit(float fx, float fz, float rng, int *hit) {
    float bt = rng; *hit = -1;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active || z->state == 2) continue;
        float dx = z->x - px, dz = z->z - pz, along = dx * fx + dz * fz, perp = fabsf(dx * fz - dz * fx);
        if (along > 0 && along < bt && perp < 0.9f) { bt = along; *hit = i; }
    }
    return bt;
}

static void player_attack(void) {
    if (attackCD > 0 || rollT > 0 || mode != 0) return;
    int w = curW, ai = wAmmo[w];
    float rng = wRange[w];
    if (ai >= 0 && inv[ai] <= 0) { say("Sem municao!"); play_sfx(10); attackCD = 0.3f; return; }
    int t = aim_target(ai >= 0 ? rng : 3.8f, ai >= 0 ? 1.1f : 3.2f);
    if (t >= 0) pang = atan2f(zomb[t].x - px, zomb[t].z - pz);
    float fx = sinf(pang), fz = cosf(pang);
    attackCD = wCD[w];
    if (ai >= 0) {
        inv[ai]--; play_sfx(1); shotT = 0.1f;
        int hit; float bt;
        if (w == 6) {
            static const float off[5] = { -0.16f, -0.08f, 0.0f, 0.08f, 0.16f };
            bt = rng;
            for (int k = 0; k < 5; k++) {
                float a2 = pang + off[k], gx = sinf(a2), gz = cosf(a2); int h2;
                float t2 = ray_hit(gx, gz, rng, &h2);
                if (k == 2) { bt = t2; fx = gx; fz = gz; }
                if (h2 >= 0) damage_zombie(&zomb[h2], wDmg[w], gx, gz);
            }
        } else {
            bt = ray_hit(fx, fz, rng, &hit);
            if (hit >= 0) damage_zombie(&zomb[hit], wDmg[w], fx, fz);
        }
        shotX1 = px + fx * 0.8f; shotZ1 = pz + fz * 0.8f; shotX2 = px + fx * bt; shotZ2 = pz + fz * bt;
    } else {
        swingT = 0.18f; play_sfx(2);
        for (int i = 0; i < NZ; i++) {
            Zomb *z = &zomb[i];
            if (!z->active || z->state == 2) continue;
            float dx = z->x - px, dz = z->z - pz, d = sqrtf(dx * dx + dz * dz);
            if (d < rng + 0.6f && (d < 0.3f || (dx * fx + dz * fz) / d > 0.2f)) damage_zombie(z, wDmg[w], fx, fz);
        }
    }
}

static int nearest_wpick(void) {
    int best = -1; float bd = 2.6f;
    for (int i = 0; i < NWP; i++) {
        if (wpick[i].taken) continue;
        float d = dist2d(px, pz, wpick[i].x, wpick[i].z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void take_weapon(int i) {
    int w = wpick[i].w; char m[64];
    wpick[i].taken = 1;
    if (wAmmo[w] >= 0) inv[wAmmo[w]] += wGive[w];
    if (!hasW[w]) { hasW[w] = 1; curW = w; sprintf(m, "Pegou: %s!", wName[w]); }
    else sprintf(m, "Ja tem %s: +%d %s", wName[w], wGive[w], wAmmo[w] >= 0 ? itemName[wAmmo[w]] : "nada");
    say(m); play_sfx(9);
}

static void use_item(void) {
    if (selUse == 0) { if (inv[I_COM] > 0) { inv[I_COM]--; hunger += 35; if (hunger > 100) hunger = 100; say("Comeu. Fome restaurada."); play_sfx(4); } else { say("Sem comida!"); play_sfx(10); } }
    else if (selUse == 1) { if (inv[I_AGUA] > 0) { inv[I_AGUA]--; thirst += 40; if (thirst > 100) thirst = 100; say("Bebeu agua."); play_sfx(4); } else { say("Sem agua!"); play_sfx(10); } }
    else { if (inv[I_BAND] > 0) { inv[I_BAND]--; hp += 40; if (hp > 100) hp = 100; say("Usou bandagem. Vida restaurada."); play_sfx(4); } else { say("Sem bandagem!"); play_sfx(10); } }
}

static int near_camp(void) { return NEAR(BASE_X, BASE_Z, 4.5f); }
static int near_lake(void) { return lake_dist(px, pz) < 3.0f; }
static int near_heli(void) { return rescue == 2 && NEAR(HELI_X, HELI_Z, 6.0f); }
static int nearest_loot(void) {
    int best = -1; float bd = 2.6f;
    for (int i = 0; i < nloot; i++) {
        if (loot[i].looted) continue;
        float d = dist2d(px, pz, loot[i].x, loot[i].z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* ---------- sobreviventes (recrutar e seguir) ---------- */
static void init_survivors(void) {
    for (int i = 0; i < NSURV; i++) {
        Surv *sv = &surv[i]; float x, z; int tries = 0;
        do { x = (frand() * 2 - 1) * (WORLD - 10); z = (frand() * 2 - 1) * (WORLD - 10); }
        while (++tries < 40 && (blocked(x, z, 3.0f) || dist2d(x, z, BASE_X, BASE_Z) < 30.0f));
        sv->x = x; sv->z = z; sv->ang = frand() * 6.28f; sv->hp = 80; sv->state = 0; sv->name = i % 8;
        sv->hurt = 0; sv->corpse = 0; sv->shootCD = 0;
    }
}

static int surv_followers(void) { int n = 0; for (int i = 0; i < NSURV; i++) if (surv[i].state == 1) n++; return n; }

static int nearest_wild_surv(float maxd) {
    int best = -1; float bd = maxd;
    for (int i = 0; i < NSURV; i++) {
        if (surv[i].state != 0) continue;
        float d = dist2d(px, pz, surv[i].x, surv[i].z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_follower(float x, float z, float maxd) {
    int best = -1; float bd = maxd;
    for (int i = 0; i < NSURV; i++) {
        if (surv[i].state != 1) continue;
        float d = dist2d(x, z, surv[i].x, surv[i].z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void surv_hurt(int i, float dmg) {
    Surv *sv = &surv[i];
    sv->hp -= dmg * diffDmg[difficulty]; sv->hurt = 0.3f;
    if (sv->hp <= 0) {
        sv->hp = 0; sv->state = 2; sv->corpse = 3.0f;
        char m[48]; sprintf(m, "%s caiu em combate!", survNames[sv->name]); say(m); play_sfx(6);
    }
}

static void recruit(int i) {
    char m[64];
    if (surv_followers() >= MAXFOL) { say("Grupo cheio (3/3)."); play_sfx(10); return; }
    if (inv[I_COM] < SV_RECRUIT_FOOD) {
        sprintf(m, "%s pede %d comida para entrar.", survNames[surv[i].name], SV_RECRUIT_FOOD); say(m); play_sfx(10); return;
    }
    inv[I_COM] -= SV_RECRUIT_FOOD;
    surv[i].state = 1; surv[i].shootCD = 0.5f;
    sprintf(m, "%s entrou para o grupo!", survNames[surv[i].name]); say(m); play_sfx(9);
}

static void update_survivors(float dt) {
    int slot = 0;
    for (int i = 0; i < NSURV; i++) {
        Surv *sv = &surv[i];
        if (sv->hurt > 0) sv->hurt -= dt;
        if (sv->corpse > 0) sv->corpse -= dt;
        if (sv->state != 1 || mode == 3) continue;
        /* posicao no grupo: atras e aos lados do jogador */
        float side = (slot % 2) ? 1.3f : -1.3f, back = 1.8f + (slot / 2) * 1.4f;
        slot++;
        float tx = px - sinf(pang) * back + cosf(pang) * side;
        float tz = pz - cosf(pang) * back - sinf(pang) * side;
        float dx = tx - sv->x, dz = tz - sv->z, d = sqrtf(dx * dx + dz * dz);
        if (d > 18.0f) { sv->x = tx; sv->z = tz; }
        else if (d > 0.7f) {
            float sp = 7.0f * dt; if (sp > d) sp = d;
            sv->x += dx / d * sp; sv->z += dz / d * sp; sv->ang = atan2f(dx, dz);
        }
        collide(&sv->x, &sv->z, 0.5f);
        /* combate: atira (pistola) no zumbi mais perto dentro de 10 */
        if (sv->shootCD > 0) sv->shootCD -= dt;
        int zt = -1; float zb = 10.0f;
        for (int k = 0; k < NZ; k++) {
            Zomb *z = &zomb[k];
            if (!z->active || z->state == 2) continue;
            float zd = dist2d(sv->x, sv->z, z->x, z->z);
            if (zd < zb) { zb = zd; zt = k; }
        }
        if (zt >= 0 && sv->shootCD <= 0) {
            Zomb *z = &zomb[zt];
            sv->ang = atan2f(z->x - sv->x, z->z - sv->z);
            sv->shootCD = 0.9f;
            damage_zombie(z, 14.0f, sinf(sv->ang), cosf(sv->ang));
        }
    }
}

static int near_car_idx(void) {
    for (int c = 0; c < NCARS; c++) if (inCar != c && dist2d(px, pz, carX[c], carZ[c]) < 3.5f) return c;
    return -1;
}

static void interact(void) {
    int l;
    if (near_heli()) { play_sfx(12); check_ending(); return; }
    if (near_camp()) { mode = 2; menuSel = 0; menuMsg[0] = 0; play_sfx(5); return; }
    if (dist2d(px, pz, CH_X, CH_Z) < 3.5f) { do_sleep(); return; }
    if (near_station()) { mode = 8; stnSel = 0; menuMsg[0] = 0; play_sfx(4); return; }
    for (int c = 0; c < NCARS; c++)
        if (dist2d(px, pz, carX[c], carZ[c]) < 3.5f) {
            inCar = c; px = carX[c]; pz = carZ[c]; pang = carAng[c]; carSpd[c] = 0;
            say("Entrou no carro! X: sair. Sobreviventes vao junto."); play_sfx(9);
            return;
        }
    l = nearest_wild_surv(4.5f);
    if (l >= 0) { recruit(l); return; }
    l = nearest_wpick();
    if (l >= 0) { take_weapon(l); return; }
    l = nearest_loot();
    if (l >= 0) { open_loot(l); return; }
    if (near_lake()) {
        if (drinkCD > 0) return;
        thirst += 30; if (thirst > 100) thirst = 100; drinkCD = 1.0f; say("Bebeu agua do lago."); play_sfx(4); return;
    }
    say("Nada para fazer aqui."); play_sfx(10);
}

/* ---------- zumbis ---------- */
static void zombies_update(float dt) {
    float night = nightness();
    float aggro = 14.0f + 12.0f * night;
    int inSafe = (rescue == 0) && dist2d(px, pz, BASE_X, BASE_Z) < SAFE_R;
    chasers = 0;
    if (moanCD > 0) moanCD -= dt;
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active) continue;
        if (z->state == 2) {
            if (z->corpse > 0) z->corpse -= dt;
            z->dead -= dt;
            if (z->dead <= 0 && i < NZREG) { z->state = 0; respawn_zombie(z); }
            continue;
        }
        if (z->hurt > 0) z->hurt -= dt;
        if (z->atk > 0) z->atk -= dt;
        float dx = px - z->x, dz = pz - z->z, d = sqrtf(dx * dx + dz * dz);
        float sp = zSpeed[z->type] * diffSpd[difficulty] * (1.0f + 0.15f * night);
        if (z->hurt > 0.15f) continue;
        if (z->state == 0) {
            z->t -= dt;
            if (z->t <= 0) { z->tx = z->hx + (frand() * 2 - 1) * z->hr; z->tz = z->hz + (frand() * 2 - 1) * z->hr; z->t = 3.0f + frand() * 4.0f; }
            float tx = z->tx - z->x, tz = z->tz - z->z, td = sqrtf(tx * tx + tz * tz);
            if (td > 0.6f) { z->x += tx / td * 1.1f * dt; z->z += tz / td * 1.1f * dt; z->ang = atan2f(tx, tz); }
            if (!inSafe && d < aggro * (z->type == 1 ? 1.2f : 1.0f)) {
                z->state = 1;
                if (moanCD <= 0 && d < 20.0f) { play_sfx(7); moanCD = 4.0f; }
            }
        } else {
            if (inSafe || (!z->horde && d > aggro * 2.0f)) z->state = 0;
            else {
                if (d > 1.0f) { z->x += dx / d * sp * dt; z->z += dz / d * sp * dt; } if (d > 0.01f) z->ang = atan2f(dx, dz);
                if (z->atk <= 0) {
                    if (d < 1.7f) { z->atk = 1.0f; hurt_player(zDmg[z->type]); }
                    else {
                        int fo = nearest_follower(z->x, z->z, 1.7f);
                        if (fo >= 0) { z->atk = 1.0f; surv_hurt(fo, zDmg[z->type]); }
                    }
                }
                if (d < 22.0f) chasers++;
            }
        }
        collide(&z->x, &z->z, 0.5f);
        if (rescue == 0) {
            float bd = dist2d(z->x, z->z, BASE_X, BASE_Z);
            if (bd < SAFE_R && bd > 0.001f) { z->x = BASE_X + (z->x - BASE_X) / bd * SAFE_R; z->z = BASE_Z + (z->z - BASE_Z) / bd * SAFE_R; }
        }
    }
}

static void spawn_horde(int n) {
    for (int i = NZREG; i < NZ && n > 0; i++) {
        Zomb *z = &zomb[i];
        if (z->active) continue;
        float a = frand() * 6.283f;
        z->x = BASE_X + cosf(a) * 38.0f; z->z = BASE_Z + sinf(a) * 38.0f;
        z->hx = BASE_X; z->hz = BASE_Z; z->hr = 8.0f;
        z->type = (frand() < 0.3f) ? 1 : 0; z->hp = zHP[z->type]; z->state = 1; z->active = 1; z->horde = 1; z->hurt = 0; z->atk = 0;
        n--;
    }
}

static void do_sleep(void) {
    float hrs = gameTime / DAY_LEN * 24.0f + 8.0f;
    float target = (floorf((hrs - 7.0f) / 24.0f) + 1.0f) * 24.0f + 7.0f;
    float skip = (target - hrs) / 24.0f * DAY_LEN;
    gameTime += skip;
    hp += 60; if (hp > 100) hp = 100;
    for (int i = 0; i < NSURV; i++) if (surv[i].state == 1) surv[i].hp = 100;
    hunger -= 15; if (hunger < 5) hunger = 5;
    thirst -= 20; if (thirst < 5) thirst = 5;
    for (int i = 0; i < nloot; i++) if (loot[i].looted && loot[i].kind != 3) { loot[i].t -= skip; if (loot[i].t <= 0) loot[i].looted = 0; }
    for (int i = 0; i < NZREG; i++) if (zomb[i].active && zomb[i].state == 2) { zomb[i].state = 0; respawn_zombie(&zomb[i]); }
    say(save_game() ? "Dormiu. Novo dia! Jogo salvo." : "Dormiu. Novo dia! (erro ao salvar)");
    play_sfx(4);
}


static void camp_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 7) % 8; play_sfx(5); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 8; play_sfx(5); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) {
            if (inv[I_PANO] >= 2) { inv[I_PANO] -= 2; inv[I_BAND]++; strcpy(menuMsg, "Fabricou 1 Bandagem!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 1) {
            if (inv[I_SUC] >= 2) { inv[I_SUC] -= 2; inv[I_MUN] += 6; strcpy(menuMsg, "Fabricou 6 Municao!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 2) {
            if (inv[I_SUC] >= 3) { inv[I_SUC] -= 3; inv[I_CART] += 4; strcpy(menuMsg, "Fabricou 4 Cartuchos!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 3) {
            if (inv[I_SUC] >= 4) { inv[I_SUC] -= 4; inv[I_RIFLE] += 3; strcpy(menuMsg, "Fabricou 3 Balas de rifle!"); play_sfx(4); }
            else { strcpy(menuMsg, "Faltam materiais!"); play_sfx(10); }
        } else if (menuSel == 4) {
            do_sleep(); mode = 0;
        } else if (menuSel == 5) {
            if (rescue != 0) { strcpy(menuMsg, "O resgate ja foi chamado!"); play_sfx(10); }
            else if (parts < 4) { sprintf(menuMsg, "Faltam %d pecas de radio!", 4 - parts); play_sfx(10); }
            else { rescue = 1; rescueT = 100.0f; hordeT = 3.0f; mode = 0; say("Resgate chamado! Aguente 100 segundos!"); play_sfx(11); }
        } else if (menuSel == 6) {
            if (fireLvl >= 3) { strcpy(menuMsg, "Fogueira no nivel maximo!"); play_sfx(10); }
            else if (fireLvl == 1 && inv[I_MAD] >= 6) { inv[I_MAD] -= 6; fireLvl = 2; strcpy(menuMsg, "Fogueira nivel 2: luz maior!"); play_sfx(4); }
            else if (fireLvl == 2 && inv[I_MAD] >= 12 && inv[I_SUC] >= 3) { inv[I_MAD] -= 12; inv[I_SUC] -= 3; fireLvl = 3; strcpy(menuMsg, "Fogueira nivel 3: luz maxima!"); play_sfx(4); }
            else { strcpy(menuMsg, fireLvl == 1 ? "Faltam 6 madeira!" : "Faltam 12 madeira e 3 sucata!"); play_sfx(10); }
        } else mode = 0;
    }
}

/* ---------- loja (moedas ganhas ao derrotar zumbis) ---------- */
#define NSHOP 6
static const char *shopName[NSHOP] = { "Bandagem x3", "Municao x12", "Cartuchos x6", "Comida x3", "Agua x3", "Balas de rifle x4" };
static const int   shopCost[NSHOP] = { 5, 8, 10, 4, 4, 12 };
static const int   shopQty[NSHOP]  = { 3, 12, 6, 3, 3, 4 };
static const int   shopItem[NSHOP] = { I_BAND, I_MUN, I_CART, I_COM, I_AGUA, I_RIFLE };

static void shop_buy(int i) {
    if (coins < shopCost[i]) { strcpy(shopMsg, "Moedas insuficientes."); play_sfx(10); return; }
    coins -= shopCost[i]; inv[shopItem[i]] += shopQty[i];
    sprintf(shopMsg, "Comprou %s!", shopName[i]); play_sfx(4);
}

static void coin_shop_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { shopSel = (shopSel + NSHOP + 1) % (NSHOP + 2); play_sfx(5); }
    if (pressed & PSP_CTRL_DOWN) { shopSel = (shopSel + 1) % (NSHOP + 2); play_sfx(5); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) { mode = 1; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (shopSel < NSHOP) shop_buy(shopSel);
        else if (shopSel == NSHOP) strcpy(shopMsg, "Em breve: multijogador!");
        else mode = 1;
    }
}

static void pause_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 9) % 10; play_sfx(5); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 10; play_sfx(5); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) mode = 0;
        else if (menuSel == 1) { musicOn = !musicOn; play_sfx(5); }
        else if (menuSel == 2) { firstPerson = !firstPerson; play_sfx(5); }
        else if (menuSel == 3) { showHelp = !showHelp; play_sfx(5); }
        else if (menuSel == 4) {
            camMode = (camMode + 1) % 3;
            camSpeed = (camMode == 0) ? 0.6f : (camMode == 1 ? 1.0f : 1.6f);
            play_sfx(5);
        }
        else if (menuSel == 5) { mode = 7; shopSel = 0; shopMsg[0] = 0; play_sfx(4); }
        else if (menuSel == 6) strcpy(menuMsg, save_game() ? "Jogo salvo!" : "Erro ao salvar!");
        else if (menuSel == 7) {
            if (save_game()) { mode = 0; appState = APP_TITLE; titleSel = 0; }
            else strcpy(menuMsg, "Erro ao salvar!");
        } else if (menuSel == 8) { mode = 0; appState = APP_TITLE; titleSel = 0; }
        else running = 0;
    }
}

static void death_update(float dt, unsigned int pressed) {
    deathT += dt;
    if (deathT > 1.2f && (pressed & PSP_CTRL_CROSS)) {
        for (int i = 0; i < NITEMS; i++) inv[i] = inv[i] / 2;
        hp = 60; hunger = hunger < 40 ? 40 : hunger; thirst = thirst < 40 ? 40 : thirst; stam = 100;
        inCar = -1; px = 3.0f; pz = 5.0f; rollT = 0; hurtT = 0; mode = 0; say("Voce renasceu no acampamento.");
        for (int i = 0; i < NZ; i++) if (zomb[i].active && zomb[i].state == 1 && dist2d(px, pz, zomb[i].x, zomb[i].z) < 20.0f) zomb[i].state = 0;
    }
}

/* ---------- multijogador: preparacao (pacote de rede; transporte ainda nao ligado) ---------- */
#define MP_MAGIC 0x4D
#define MP_MAX   4
typedef struct { unsigned char magic, id, state, pad; float x, z, ang; short hp; short pad2; } NetPacket;
typedef struct { int used, id, state, hp; float x, z, ang, age; } NetPlayer;
static NetPlayer netRemote[MP_MAX];
static int mpEnabled = 0;
static void net_pack_me(NetPacket *pk) {
    memset(pk, 0, sizeof(*pk));
    pk->magic = MP_MAGIC; pk->id = 0; pk->state = (mode == 3) ? 2 : 1;
    pk->x = px; pk->z = pz; pk->ang = pang; pk->hp = (short)hp;
}
static int net_unpack(const NetPacket *pk, NetPlayer *out) {
    if (pk->magic != MP_MAGIC || pk->id >= MP_MAX) return 0;
    out->used = 1; out->id = pk->id; out->state = pk->state; out->hp = pk->hp;
    out->x = pk->x; out->z = pk->z; out->ang = pk->ang;
    return 1;
}

static int near_station(void) { return dist2d(px, pz, STN_X, STN_Z) < 6.0f; }

static void refuel_nearby(void) {
    int c = inCar;
    if (c < 0) {
        float bd = 8.0f;
        for (int k = 0; k < NCARS; k++) {
            float d = dist2d(carX[k], carZ[k], STN_X, STN_Z);
            if (d < bd) { bd = d; c = k; }
        }
    }
    if (c < 0) { strcpy(menuMsg, "Leve um carro ate o posto."); play_sfx(10); return; }
    int need = (int)ceilf((100.0f - carFuel[c]) / 25.0f);
    if (need <= 0) { strcpy(menuMsg, "Tanque cheio."); play_sfx(4); return; }
    if (inv[I_GAS] < need) { sprintf(menuMsg, "Faltam %d gasolina.", need - inv[I_GAS]); play_sfx(10); return; }
    inv[I_GAS] -= need; carFuel[c] = 100.0f;
    strcpy(menuMsg, "Carro abastecido!"); play_sfx(3);
}

static void station_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { stnSel = (stnSel + 2) % 3; play_sfx(5); }
    if (pressed & PSP_CTRL_DOWN) { stnSel = (stnSel + 1) % 3; play_sfx(5); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (stnSel == 0) {
            if (coins >= 6) { coins -= 6; inv[I_GAS] += 3; strcpy(menuMsg, "Comprou 3 gasolina!"); play_sfx(3); }
            else { strcpy(menuMsg, "Moedas insuficientes (6)."); play_sfx(10); }
        } else if (stnSel == 1) refuel_nearby();
        else mode = 0;
    }
}

static void leave_car(void) {
    int c = inCar;
    if (c < 0) return;
    inCar = -1; carSpd[c] = 0;
    px = carX[c] + cosf(carAng[c]) * 2.4f; pz = carZ[c] - sinf(carAng[c]) * 2.4f;
    collide(&px, &pz, 0.5f);
    say("Saiu do carro.");
}

static void car_update(float dt, const SceCtrlData *pad, unsigned int pressed) {
    int c = inCar;
    gameTime += dt;
    if (msgTimer > 0) msgTimer -= dt;
    if (attackCD > 0) attackCD -= dt;
    if (swingT > 0) swingT -= dt;
    if (shotT > 0) shotT -= dt;
    if (hurtT > 0) hurtT -= dt;
    if (pad->Buttons & PSP_CTRL_LTRIGGER) cam -= 1.8f * camSpeed * dt;
    if (pad->Buttons & PSP_CTRL_RTRIGGER) cam += 1.8f * camSpeed * dt;
    /* analogico: para cima acelera, para baixo freia/ré, para os lados vira */
    float steer = (pad->Lx - 128) / 128.0f, thr = (128 - pad->Ly) / 128.0f;
    if (fabsf(steer) < 0.12f) steer = 0;
    if (fabsf(thr) < 0.12f) thr = 0;
    if (carFuel[c] <= 0.0f) thr = 0.0f;
    carSpd[c] += thr * 14.0f * dt;
    if (thr == 0) carSpd[c] *= (1.0f - 0.8f * dt);
    if (carSpd[c] > 22.0f) carSpd[c] = 22.0f;
    if (carSpd[c] < -10.0f) carSpd[c] = -10.0f;
    if (fabsf(carSpd[c]) > 0.2f) carAng[c] -= steer * 1.8f * dt * (carSpd[c] / 12.0f);   /* direita do analogico = direita do carro */
    carX[c] += sinf(carAng[c]) * carSpd[c] * dt;
    carZ[c] += cosf(carAng[c]) * carSpd[c] * dt;
    if (carX[c] < -WORLD) carX[c] = -WORLD;
    if (carX[c] > WORLD) carX[c] = WORLD;
    if (carZ[c] < -WORLD) carZ[c] = -WORLD;
    if (carZ[c] > WORLD) carZ[c] = WORLD;
    float ox = carX[c], oz = carZ[c];
    collide(&carX[c], &carZ[c], 1.6f);
    if (dist2d(ox, oz, carX[c], carZ[c]) > 0.01f) carSpd[c] *= 0.3f;   /* bateu: perde velocidade */
    carFuel[c] -= fabsf(carSpd[c]) * dt * 0.05f; if (carFuel[c] < 0.0f) carFuel[c] = 0.0f;
    px = carX[c]; pz = carZ[c]; pang = carAng[c];
    /* atropela zumbis em alta velocidade */
    if (fabsf(carSpd[c]) > 6.0f)
        for (int i = 0; i < NZ; i++) {
            Zomb *z = &zomb[i];
            if (!z->active || z->state == 2) continue;
            if (dist2d(px, pz, z->x, z->z) < 2.6f) damage_zombie(z, 300.0f, sinf(carAng[c]), cosf(carAng[c]));
        }
    zombies_update(dt);
    update_survivors(dt);
    if (pressed & PSP_CTRL_CROSS) { leave_car(); return; }
    if ((pressed & PSP_CTRL_SQUARE) || ((pad->Buttons & PSP_CTRL_SQUARE) && wAuto[curW])) player_attack();
    if ((pressed & PSP_CTRL_TRIANGLE) && dist2d(px, pz, STN_X, STN_Z) < 9.0f) { mode = 8; stnSel = 0; menuMsg[0] = 0; play_sfx(4); }
    if (pressed & PSP_CTRL_START) { mode = 1; menuSel = 0; menuMsg[0] = 0; }
}

static void city_check(void) {
    for (int c = 0; c < NCITY; c++) {
        if (cityVis[c] || dist2d(px, pz, CITY[c][0], CITY[c][1]) > 9.0f) continue;
        cityVis[c] = 1;
        int n = cityVis[0] + cityVis[1] + cityVis[2];
        char m[64]; sprintf(m, "Chegou em %s! (%d/3)", CITY_NAME[c], n); say(m); play_sfx(9);
        if (n == NCITY && !endingSeen) { say("Todas as cidades visitadas! Voce zerou!"); check_ending(); }
    }
}

/* transporte: por enquanto nao envia nem recebe nada (ver nota); o protocolo ja esta pronto */
static int mp_send(const NetPacket *pk) { (void)pk; return 0; }
static int mp_recv(NetPacket *pk) { (void)pk; return 0; }

static void mp_apply(const NetPacket *pk) {
    NetPlayer np;
    if (!net_unpack(pk, &np) || np.id == 0) return;
    netRemote[np.id] = np;
    netRemote[np.id].age = 0;
}

static float mpSendT = 0;
static void mp_update(float dt) {
    if (!mpEnabled) return;
    for (int i = 1; i < MP_MAX; i++) if (netRemote[i].used) { netRemote[i].age += dt; if (netRemote[i].age > 3.0f) netRemote[i].used = 0; }
    mpSendT -= dt;
    if (mpSendT <= 0.0f) { mpSendT = 0.1f; NetPacket pk; net_pack_me(&pk); mp_send(&pk); }
    NetPacket in;
    while (mp_recv(&in)) mp_apply(&in);
}

static void update(float dt, const SceCtrlData *pad, unsigned int pressed) {
    gameTime += dt;
    if (msgTimer > 0) msgTimer -= dt;
    if (attackCD > 0) attackCD -= dt;
    if (swingT > 0) swingT -= dt;
    if (shotT > 0) shotT -= dt;
    if (hurtT > 0) hurtT -= dt;
    if (drinkCD > 0) drinkCD -= dt;

    if (pad->Buttons & PSP_CTRL_LTRIGGER) cam -= 1.8f * camSpeed * dt;
    if (pad->Buttons & PSP_CTRL_RTRIGGER) cam += 1.8f * camSpeed * dt;

    float dx = (pad->Lx - 128) / 128.0f, dy = (pad->Ly - 128) / 128.0f;
    if (fabsf(dx) < 0.12f) dx = 0;
    if (fabsf(dy) < 0.12f) dy = 0;
    float mx = 0, mz = 0;
    moving = (dx != 0 || dy != 0);
    if (moving) {
        float fx = sinf(cam), fz = cosf(cam), rx = -cosf(cam), rz = sinf(cam);
        mx = fx * (-dy) + rx * dx; mz = fz * (-dy) + rz * dx;
        float l = sqrtf(mx * mx + mz * mz); if (l > 1.0f) { mx /= l; mz /= l; }
    }
    if ((pressed & PSP_CTRL_CIRCLE) && rollT <= 0 && stam >= 25.0f) {
        float l = sqrtf(mx * mx + mz * mz);
        if (l > 0.1f) { rollX = mx / l; rollZ = mz / l; } else { rollX = sinf(pang); rollZ = cosf(pang); }
        rollT = 0.32f; stam -= 25.0f; play_sfx(8);
    }
    if (rollT > 0) { rollT -= dt; mx = rollX * 1.9f; mz = rollZ * 1.9f; }
    if (mx != 0 || mz != 0) {
        float sp = 7.5f * dt;
        px += mx * sp; pz += mz * sp;
        if (px < -WORLD) px = -WORLD;
        if (px > WORLD) px = WORLD;
        if (pz < -WORLD) pz = -WORLD;
        if (pz > WORLD) pz = WORLD;
        collide(&px, &pz, 0.5f); collide_trees(&px, &pz);
        pang = atan2f(mx, mz);
        float rx = -cosf(cam), rz = sinf(cam), side = mx * rx + mz * rz;
        if (side > 0.15f) facingRight = 1; else if (side < -0.15f) facingRight = 0;
        walkPhase += dt * 8.0f;
    } else walkPhase = 0;
    if (firstPerson) pang = cam;
    stam += 16.0f * dt; if (stam > 100) stam = 100;

    hunger -= 0.10f * dt; thirst -= 0.15f * dt;
    if (hunger < 0) hunger = 0;
    if (thirst < 0) thirst = 0;
    if (hunger <= 0 || thirst <= 0) {
        hp -= 1.5f * dt;
        if (hp <= 0) { hp = 0; player_die(); return; }
    } else if (hunger > 40 && thirst > 40 && hp < 100) { hp += 0.5f * dt; if (hp > 100) hp = 100; }

    for (int i = 0; i < nloot; i++) if (loot[i].looted && loot[i].kind != 3) { loot[i].t -= dt; if (loot[i].t <= 0) loot[i].looted = 0; }

    if (rescue == 1) {
        rescueT -= dt; hordeT -= dt;
        if (hordeT <= 0) { spawn_horde(2); hordeT = 7.0f; }
        if (rescueT <= 0) { rescue = 2; rescueT = 90.0f; say("O helicoptero chegou! Va ate ele (X)!"); play_sfx(12); }
    } else if (rescue == 2) {
        rescueT -= dt;
        if (rescueT <= 0) { rescue = 0; say("O helicoptero foi embora... chame de novo."); }
    }
    zombies_update(dt);
    update_survivors(dt);

    if (pressed & PSP_CTRL_LEFT)  { selUse = (selUse + 2) % 3; play_sfx(5); }
    if (pressed & PSP_CTRL_RIGHT) { selUse = (selUse + 1) % 3; play_sfx(5); }
    if (pressed & PSP_CTRL_UP) use_item();
    if (pressed & PSP_CTRL_SELECT) {
        if (hasLant) { lantOn = !lantOn; say(lantOn ? "Lanterna ligada." : "Lanterna desligada."); play_sfx(5); }
        else say("Voce ainda nao tem lanterna.");
    }
    if (pressed & PSP_CTRL_TRIANGLE) {
        for (int k = 1; k <= NW; k++) { int w = (curW + k) % NW; if (hasW[w]) { curW = w; break; } }
        char m[40]; sprintf(m, "Arma: %s", wName[curW]); say(m); play_sfx(5);
    }
    if ((pressed & PSP_CTRL_SQUARE) || ((pad->Buttons & PSP_CTRL_SQUARE) && wAuto[curW])) player_attack();
    if (pressed & PSP_CTRL_CROSS) interact();
    if (pressed & PSP_CTRL_START) { mode = 1; menuSel = 0; menuMsg[0] = 0; }
}

static const unsigned char font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x56,0x20,0x50},
    {0x00,0x08,0x07,0x03,0x00},
    {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},
    {0x2A,0x1C,0x7F,0x1C,0x2A},
    {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00},
    {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x00,0x60,0x60,0x00},
    {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},
    {0x72,0x49,0x49,0x49,0x46},
    {0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x31},
    {0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36},
    {0x46,0x49,0x49,0x29,0x1E},
    {0x00,0x00,0x14,0x00,0x00},
    {0x00,0x40,0x34,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},
    {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},
    {0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E},
    {0x7C,0x12,0x11,0x12,0x7C},
    {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E},
    {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x1C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32},
    {0x03,0x01,0x7F,0x01,0x03},
    {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},
    {0x61,0x59,0x49,0x4D,0x43},
    {0x00,0x7F,0x41,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},
    {0x00,0x41,0x41,0x41,0x7F},
    {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x03,0x07,0x08,0x00},
    {0x20,0x54,0x54,0x78,0x40},
    {0x7F,0x28,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x28},
    {0x38,0x44,0x44,0x28,0x7F},
    {0x38,0x54,0x54,0x54,0x18},
    {0x00,0x08,0x7E,0x09,0x02},
    {0x18,0xA4,0xA4,0x9C,0x78},
    {0x7F,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x40,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},
    {0x7C,0x04,0x78,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},
    {0x38,0x44,0x44,0x44,0x38},
    {0xFC,0x18,0x24,0x24,0x18},
    {0x18,0x24,0x24,0x18,0xFC},
    {0x7C,0x08,0x04,0x04,0x08},
    {0x48,0x54,0x54,0x54,0x24},
    {0x04,0x04,0x3F,0x44,0x24},
    {0x3C,0x40,0x40,0x20,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},
    {0x4C,0x90,0x90,0x90,0x7C},
    {0x44,0x64,0x54,0x4C,0x44},
    {0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x77,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},
    {0x08,0x04,0x08,0x10,0x08}
};

typedef struct { unsigned int color; short x, y, z; } V2;

static void rect2d(int x0, int y0, int x1, int y1, unsigned int color) {
    V2 *v = (V2 *)sceGuGetMemory(2 * sizeof(V2));
    v[0].color = color; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].color = color; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

/* texto desenhado pela GPU (fonte 5x7), funciona em qualquer emulador */
static void ui_text(int x, int y, int sc, unsigned int color, const char *s) {
    int n = 0;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p; if (c < 32 || c > 126) c = 32;
        for (int col = 0; col < 5; col++) {
            unsigned char b = font5x7[c - 32][col]; int in = 0;
            for (int r = 0; r < 8; r++) {
                int bit = (b >> r) & 1;
                if (bit && !in) n++;
                in = bit;
            }
        }
    }
    if (n == 0) return;
    V2 *v = (V2 *)sceGuGetMemory(n * 2 * sizeof(V2));
    int k = 0, cx = x;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p; if (c < 32 || c > 126) c = 32;
        for (int col = 0; col < 5; col++) {
            unsigned char b = font5x7[c - 32][col]; int start = -1;
            for (int r = 0; r <= 8; r++) {
                int bit = (r < 8) ? ((b >> r) & 1) : 0;
                if (bit && start < 0) start = r;
                else if (!bit && start >= 0) {
                    v[k].color = color; v[k].x = cx + col * sc; v[k].y = y + start * sc; v[k].z = 0; k++;
                    v[k].color = color; v[k].x = cx + (col + 1) * sc; v[k].y = y + r * sc; v[k].z = 0; k++;
                    start = -1;
                }
            }
        }
        cx += 6 * sc;
    }
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, k, 0, v);
}

static void ui_shadow(int x, int y, int sc, unsigned int color, const char *s) {
    ui_text(x, y, sc, color, s);
}

/* ---------- personagens e armas em 3D ---------- */
typedef struct { float ox, oy, oz, sx, sy, sz; unsigned int col; } WPart;
static const WPart WPARTS[NW][4] = {
    { { 0, 0, 0, 0, 0, 0, 0 } },
    { { 0, -0.04f, 0.5f, 0.09f, 0.09f, 1.0f, 0xff3a6a9a }, { 0, -0.07f, 0.95f, 0.14f, 0.14f, 0.3f, 0xff3a6a9a } },
    { { 0, -0.035f, 0.5f, 0.07f, 0.07f, 1.0f, 0xff3a6a9a }, { 0, -0.17f, 0.92f, 0.05f, 0.34f, 0.24f, 0xffa0a8a8 } },
    { { 0, -0.06f, 0.55f, 0.03f, 0.12f, 0.9f, 0xffd0d8d8 }, { 0, -0.04f, 0.05f, 0.05f, 0.08f, 0.22f, 0xff202830 } },
    { { 0, -0.05f, 0.25f, 0.10f, 0.14f, 0.45f, 0xff484848 }, { 0, -0.2f, 0.08f, 0.09f, 0.2f, 0.12f, 0xff202020 } },
    { { 0, -0.07f, 0.3f, 0.12f, 0.16f, 0.7f, 0xff404040 }, { 0, -0.3f, 0.25f, 0.08f, 0.24f, 0.1f, 0xff202020 },
      { 0, -0.05f, -0.2f, 0.08f, 0.1f, 0.3f, 0xff303030 } },
    { { 0, -0.04f, 0.6f, 0.09f, 0.09f, 1.2f, 0xff606870 }, { 0, -0.12f, -0.2f, 0.11f, 0.18f, 0.55f, 0xff3a6a9a },
      { 0, -0.1f, 0.55f, 0.13f, 0.12f, 0.3f, 0xff2a4a6a } },
    { { 0, -0.03f, 0.7f, 0.07f, 0.07f, 1.3f, 0xff505860 }, { 0, -0.09f, 0.05f, 0.11f, 0.16f, 0.65f, 0xff2a4a6a },
      { 0, 0.06f, 0.35f, 0.06f, 0.06f, 0.3f, 0xff101010 }, { 0, -0.1f, -0.35f, 0.1f, 0.2f, 0.4f, 0xff2a4a6a } }
};
static const int WPN[NW] = { 0, 2, 2, 2, 2, 3, 3, 4 };

/* arma presa na mao: pivo no ombro, gira com o braco; com pitch=-90 graus o eixo z da arma aponta para frente */
static void weapon_part(float x, float y, float z, float yaw, float pitch, float armLen, const WPart *p) {
    ScePspFVector3 t1 = { x, y, z }, t2 = { 0, -armLen, 0 }, t3 = { p->ox, p->oy, p->oz }, s = { p->sx, p->sy, p->sz };
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t1); sceGumRotateY(yaw); sceGumRotateX(pitch); sceGumTranslate(&t2);
    sceGumRotateX(1.5708f); sceGumTranslate(&t3); sceGumScale(&s);
    sceGumUpdateMatrix();
    sceGuColor(p->col);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, 36, 0, cube);
}
static void draw_weapon(int w, float x, float y, float z, float yaw, float pitch, float armLen) {
    for (int k = 0; k < WPN[w]; k++) weapon_part(x, y, z, yaw, pitch, armLen, &WPARTS[w][k]);
}

/* parte do corpo: origem no chao, gira com o personagem; hang=1 faz o bloco pender do pivo (bracos e pernas) */
static void body_part(float x, float z, float yaw, float ox, float oy, float oz, float pitch, int hang,
                      float sx, float sy, float sz, unsigned int col) {
    ScePspFVector3 t1 = { x, 0, z }, t2 = { ox, oy, oz }, t3 = { 0, -sy, 0 }, s = { sx, sy, sz };
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t1); sceGumRotateY(yaw); sceGumTranslate(&t2);
    if (pitch != 0.0f) sceGumRotateX(pitch);
    if (hang) sceGumTranslate(&t3);
    sceGumScale(&s);
    sceGumUpdateMatrix();
    sceGuColor(col);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, 36, 0, cube);
}

typedef struct { unsigned int skin, shirt, pants, hair, eye; } HPal;
static const HPal HPAL_PLAYER = { 0xff90b8f0, 0xffb06020, 0xff604838, 0xff2040a0, 0xff202020 };
static const HPal HPAL_Z[3] = {
    { 0xff60b080, 0xff506890, 0xff404858, 0xff203030, 0xff2020e0 },
    { 0xff70c090, 0xff3030a0, 0xff303038, 0xff202040, 0xff30d0ff },
    { 0xff508060, 0xff403060, 0xff303038, 0xff182018, 0xff2020e0 }
};
static const HPal HPAL_HIT = { 0xffd0d0ff, 0xffc0c0ff, 0xffb0b0ff, 0xffd0d0ff, 0xff20e0ff };
static const float Z_K[3] = { 1.0f, 0.95f, 1.45f };
static const HPal HPAL_SURV[3] = {
    { 0xff90b8f0, 0xff3050a0, 0xff403020, 0xff202020, 0xff202020 },
    { 0xffa0c0e0, 0xffa04040, 0xff304050, 0xff606060, 0xff202020 },
    { 0xffe0c0a0, 0xff408040, 0xff504030, 0xffc0a060, 0xff202020 }
};

static void draw_humanoid(float x, float z, float yaw, float k, const HPal *p, float legSw, float armL, float armR,
                          int weapon, int lod, int player) {
    float c = cosf(yaw), s = sinf(yaw);
    if (lod) {
        body_part(x, z, yaw, 0, 0.0f, 0, 0, 0, 0.55f * k, 1.55f * k, 0.32f * k, p->shirt);
        body_part(x, z, yaw, 0, 1.5f * k, 0, 0, 0, 0.42f * k, 0.42f * k, 0.42f * k, p->skin);
        return;
    }
    body_part(x, z, yaw, -0.16f * k, 0.85f * k, 0, legSw, 1, 0.24f * k, 0.85f * k, 0.26f * k, p->pants);
    body_part(x, z, yaw,  0.16f * k, 0.85f * k, 0, -legSw, 1, 0.24f * k, 0.85f * k, 0.26f * k, p->pants);
    body_part(x, z, yaw, 0, 0.85f * k, 0, 0, 0, 0.62f * k, 0.7f * k, 0.34f * k, p->shirt);
    body_part(x, z, yaw, 0, 1.55f * k, 0, 0, 0, 0.42f * k, 0.4f * k, 0.4f * k, p->skin);
    body_part(x, z, yaw, 0, 1.93f * k, 0, 0, 0, 0.46f * k, 0.12f * k, 0.46f * k, p->hair);
    body_part(x, z, yaw, -0.1f * k, 1.72f * k, 0.2f * k, 0, 0, 0.07f * k, 0.07f * k, 0.05f * k, p->eye);
    body_part(x, z, yaw,  0.1f * k, 1.72f * k, 0.2f * k, 0, 0, 0.07f * k, 0.07f * k, 0.05f * k, p->eye);
    if (player) {
        body_part(x, z, yaw, 0, 1.86f * k, 0.25f * k, 0, 0, 0.32f * k, 0.05f * k, 0.18f * k, p->hair);
        body_part(x, z, yaw, 0, 0.95f * k, -0.26f * k, 0, 0, 0.44f * k, 0.52f * k, 0.2f * k, 0xff304048);
        body_part(x, z, yaw, 0, 1.98f * k, 0, 0, 0, 0.52f * k, 0.14f * k, 0.52f * k, 0xffc04020);
        body_part(x, z, yaw, 0, 1.97f * k, 0.32f * k, 0, 0, 0.5f * k, 0.04f * k, 0.22f * k, 0xff202020);
        body_part(x, z, yaw, 0, 0.8f * k, 0, 0, 0, 0.6f * k, 0.09f * k, 0.34f * k, 0xff203040);
        body_part(x, z, yaw, 0.3f * k, 0.8f * k, 0.2f * k, 0, 0, 0.12f * k, 0.14f * k, 0.12f * k, 0xff203040);
    }
    body_part(x, z, yaw, -0.40f * k, 1.5f * k, 0, armR, 1, 0.18f * k, 0.7f * k, 0.18f * k, p->shirt);
    body_part(x, z, yaw,  0.40f * k, 1.5f * k, 0, armL, 1, 0.18f * k, 0.7f * k, 0.18f * k, p->shirt);
    if (weapon > 0) draw_weapon(weapon, x - 0.40f * k * c, 1.5f * k, z + 0.40f * k * s, yaw, armR, 0.7f * k);
}

static void draw_corpse(float x, float z, float yaw, float k, const HPal *p) {
    body_part(x, z, yaw, 0, 0.2f * k, 0, -1.5708f, 0, 0.6f * k, 1.55f * k, 0.34f * k, p->shirt);
    body_part(x, z, yaw, 0, 0.05f * k, -1.75f * k, 0, 0, 0.42f * k, 0.42f * k, 0.42f * k, p->skin);
}

/* ---------- cena ---------- */
static void fence_line(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1, dz = z2 - z1, len = sqrtf(dx * dx + dz * dz);
    float mx = (x1 + x2) * 0.5f, mz = (z1 + z2) * 0.5f;
    if (dist2d(px, pz, mx, mz) > len * 0.5f + 45.0f) return;
    float ang = atan2f(dx, dz); int n = (int)(len / 6.0f); if (n < 1) n = 1;
    box(mx, 0.5f, mz, 0.12f, 0.12f, len, ang, 0xff505860);
    box(mx, 1.1f, mz, 0.12f, 0.12f, len, ang, 0xff505860);
    for (int i = 0; i <= n; i++) box(x1 + dx * i / n, 0, z1 + dz * i / n, 0.25f, 1.5f, 0.25f, 0, 0xff303840);
}

/* atlas 256x256 (T8) com 4 veiculos: CLUT (1024 bytes) + pixels com RLE de zeros, em base64 */
#define TEXRAW_LEN 32090
static const char TEX_B64[] =
    "AAAAAMO8oP+7lEj/yY4o/6+OS//Eiib/wYYl/7mGLv+Jhmr/uH4h/696JP+mejD/pXMk/5B3Tf97eHL/cXNw/1J3gP+naiT/mmgk"
    "/5BqMP+PYyn/bm1s/2xpaP9/Z0b/bWZZ/2dnZf9caGj/ZWNg/19iX/8/aHz/PWN8/zFmkP8wZIX/l1ok/4daJv9/XC3/f1Ql/2xc"
    "Tf90WDL/blE1/3RRIv9hX17/W1xd/2FdU/9aXFL/W1hW/1tVU/9fVUf/Xk9A/1VZWv9UWUn/VFZR/1NTVf9SU0n/U1FR/09PUP9T"
    "UEj/TlBG/z9cdv9DWF7/RlBW/0tPQf8yXHz/KVqA/y5XZf8tUGT/h0Ic/3BHIv9nSSj/ZkEh/1xJPP9eRif/Wz4s/1w9Gf9OTUz/"
    "T0pK/1FKQP9SRjv/UEI9/1JAKf9QOi//Ujoc/0lKTf9JSkf/SEdI/0VGSP9ISz//R0g+/0dHP/9ERjv/RURF/0NDRf9GRD3/Q0Q6"
    "/0ZBP/9GPTr/Rzwv/0c4K/9ARkr/QUJE/0FFN/9AQjf/QEFE/z9BOP9AQEL/Pj9C/0BAOf89Pzb/Pz49/z09Pv88PDz/PDk8/z89"
    "NP87PDT/Pzky/zs5M/8ySFn/JEhh/zE/Tv8kPlT/ND84/zg7Nf86Ojj/NTo0/zg4O/84ODf/ODgz/zg4L/82ODL/LDg+/28wF/9S"
    "Mhv/STQi/0ktGv9ANC//QTIh/0AtH/9AJhj/OTY0/zY2Nf84MzP/ODQs/zkyK/84Lir/OS0g/zkpHP84Ixj/NDU3/zMzN/80NTH/"
    "MzMy/zIyNP8yMTH/NDUt/zI1Kv8yMyz/MjEt/zMxKf8yMC3/Mi0s/zMuJv8zKiX/Mykc/zMkGf8uMzn/LzAy/y8vMP8uLjD/LjIp"
    "/y4wKf8uLyn/Li4o/y0tL/8uLSv/LCwv/ywsLP8uLSj/Liwl/ywsKP8rLCX/LCst/ywqKf8qKiz/Kioo/y4qJv8uKiL/Kiom/yoq"
    "I/8sJyf/LCci/y0mHv8sIxz/LB8W/yUwPv8bL0H/KSsn/yUrKv8pKSj/Jikp/ygoKf8oKCT/JCgo/ycnKf8oJyP/JCcn/yYmJ/8l"
    "JSj/JiUl/yclIf8lJSH/JyYe/yElLf8kJSH/JiQm/yQkJf8lIyL/IyMn/yMjIv8mIx//IyMf/yUjHP8hIy3/ISMp/yIiIv8hIxz/"
    "HyIs/x8iKP8cIyv/IyEh/yEhI/8fISj/ICAg/yUhG/8iIRz/ISAc/yAgG/8iHx//Hx8f/yMfGP8fHhr/Ih0Y/yIaFP8eISr/HiAj"
    "/x4eH/8eHxr/Hh0a/x0cGf8cHRz/Gh0f/xwbGv8cGhX/GRoZ/xsYFf8YFxb/AAAA/wBUsaKSJxMTJxVEDQACFqMTE0lRhABYytbW"
    "fCobGVttlJMAh8uRkkULCwcDAlEOh0eOLBaIEwcHBxIkKIQAUsrl+KkpLTZSXioqLTHNAIW/o4UECwwCAiIoRIQOj6CMbBuOURQH"
    "BgUFBQwjhABL9Pq2rrSsKRkZKi0xSjF8mK16VDk1erUAf8CSEwsLAwoTSU8lKytMJV82aJNubS4lhRQLBgYGB0mSkoWRAEeXaFI3"
    "KjQtKjZ6k5eXrNWmepZ+qnpY2gB8oJFRCwsHAwMDJEcrGyspFg5GKxkVFhkZFhkpTYUjCwcHCgwMDBJFUaMARLVu+pitKS3ZzfTl"
    "zc6n1paesLLPi1i+AHmHiUkUIgwKBgUDAwMkkqOGKTYZFSkbFQ8VFRUVFRUZLZCJiSgKCQkJCQcLg09gAEH49npqlf26stG0pbas"
    "p4vHnM+yla2VWL8AdsCESQsCAwMDChERCgUFBQoSQ0VPTCsVFRkqDw8VFRUVFRUVk40YJ0MiEQwKEhEHCyODjgA7r9loMxkODxZ6"
    "99DQxW1SbXx8lteeuq1KmZlY3AB0wERJCwcDAwMDAwMDBgwSCgoGBgkMJElRT04PDg4PDxUVFRUWFQ8bLyWDQyEhEhEKBxIREihR"
    "ADalpXxaWg4ZFlyy29Gy0dHQsn19fG199PzIy9F6rZaLsgBwv8BRRCgHBwUDAwMDAwMDAwMDBQwMCgkFBgkMChJEh4cPDg8PFQ4O"
    "DhVYh5KJKAoGBQMDAwMHBwcHE4VwADGsl1o3Sg4ONlXR0atqy2pcy9TQsrqepqdublRKSsjNpqbNAG+RUSQLBwUHBwsLBwMDAwMD"
    "AwMDAwMDAwkJDAoFBQUFDBImUS8pDg4VLmCIRyMUDAcFBQMDAwMDAwMHDAoLR70ALZdulzEPFhlal5enliotU5Xp9/v+6dyxXH6V"
    "Gxk0U5V5bufKAG7AUUMiBwMDBQoLg4nAhUMHBwMDAwMDAwMDAwMDCQoRCgUGBQYKDEVROE5NRxQSDAwSCgMFBQUDAwMDAwMHEQoH"
    "E0cALWkxGRmLmLKYSjetusjI0enQ2sub0um7Z6twOViyy5t+nOelAAyc0ujx7gBZucCSIxMHAwMDBQwThYVISC9MRlEoCwcDAwMD"
    "AwMDAwMDAwUJEhEGBgYJDCIoJCIMBgUDBQUMCQMDBQUFAwMDBiEhBAQHQ+gALG/5W1TIljdTsuqysrOzu+lrcXBx0ve7cLl/nbqq"
    "NrOAqdl8AAmmxciYi5yVutsAVqKJRQsHBwUFAwcHI5KJSEhERhgpGysvwCYiBwMDAwMDAwMDAwMDBQkhDAoJBQUFBgUFAwMDAwUF"
    "CQkFAwMDBQMFByEkQyQLE8AALGZmbO36/Pv+++ssOVdZZcjLu7vS3O/Qc7mZm55TnNKpqduUAAeXsptxcWtra3GAqqjI2dYAT6KF"
    "SSMHBwYGCQcSE0mJkoVQLy8vLw8ZGxkZG1+HRCQCAwMDAwMDAwUFBQUGCgwMCgYFBQUFBQUFBQUFBQkJBQMDAwMJAkJFRUUkJMAA"
    "LJSXla33/NFrf39kXWRZWbG7Zbvw8O7cf7GZmdLS3H9/q/WviwABk6anlqioe3uocdGzu7ObV1xrfpXbAEu8hEdJCwMFBQYGBhIm"
    "kpKFSINIUCsbGykVGRwZGRkZGThOUUkHAwMDAwMFBQUFBQUFCQoRBQUFBQUFBQUFBQUFCQkDBQkJCQoMFENFQ0OFACyXr/ibWFhd"
    "a11lZWVkZF2zq12dvdrw63Grm5m7a3F/f6vL0dTP2dnt1JmAqOD9f9yzy7O7q7mNc3N+lqbD1q4AQ6OSwA4mAgMDAwMFBQUFFImE"
    "iIRGSEhGKykbGRkOKSkbFhYVFhkbL4UTCwcFBQUFBQUFBQUFBgkRBQUFBQUFBQUFAwMDCgoJCgkKCgwkhYVFQ4MALNRdZ1ldWWRd"
    "ZWRdZGRkWbPScbPQve77f7Gdjbl/f39xnMipy8jgyJyy6vv78Y5gnb7cuLi7udKxsJWcmZmdqZgAP6NRhRQLTwgnAgMCIiQiBwcG"
    "ChIog4WHYBgrNl5TU0oVGSsvGBYVFSkbGS1OTyQHBQUFBQUFBQUFBQUJCgkDBQUDAwMFBgkJDBEMDAwMISIoJ4SFSYPuACine3tr"
    "V1lkZGRkZGVdWV1dZF1xu/v1m42x+3+xoH+xf3+dqrOzmVlka9L1/dvSVw4OCBgY6L24y8jL1NHIs6txZ2d5bouqADtRKAcHBQcN"
    "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"
    "9enb24Bra2RZWVdZVj01OWvS4MvIyMupm7FxV3/gXTIzc6qys6rL0dvIy9KZOX+bmV3Lvw4IGBgIF0+IkQgICCWiL01yX43x8Ks5"
    "Mjk9Vn9/f4CaeaYAHkdDQxMSCwwKDAoKBwILDSMoFAcHBwokhVEvGwgOGzhGYKAPDg4YgwoMCwtDCwUDAwMFCRIJBgkJCQkKCiEU"
    "IkVDRYMTExMjJ4ejKCOiNcv69Pn8a44mJoc2vmFIREdHh0kjwAAZt1gbM+B6aplcq6tzf11cXXPr6er1qrN/a2VXVllZVjI5NStl"
    "s7PQs6mry7txXanLy+DRu8u70pkparvIm1Z/OTIyu79hTYcYCAgIDaMlJSUlokxg8fHoXXBWWWtxmpqZmpmAa3CgAB5EFBRJUUlD"
    "KBIMCgwKDAsEDUREEgcGCQoLKEdNTE9HKCaIFQ4ODkQSChQEQwcDAwUFCQkhCQYGCQkKISIkQ4NFSRMLE1FPYFYzjSQjkDPI+vj0"
    "/JugR4XxuU9IR0dIkqNJI6MAGbZ5ZWuzu5kbZdzp2+mZf2dXcF2r0mV/4OvSmYBdXV1da1lZPRsZf6mxs3Fdqdzr1LrRy5rSVhsy"
    "OTh/27JcWV0yObG+YCVNkScnJ0bAJSUlJcC+uX9XOVlXa3+ZmX+aqJpxVmVyhAAUfils0I4ABUQiQ0mFUUlJQxQMCwwMIQsNMGCE"
    "RQsHBQMKDCgiEwsLC4MlGA4IF0kLBINDAgMGCQoKCiIMEQcKEiJDRYNEEwsiR6Nga3kzKyuHJia+7v2/j46OkIeEhIeQT0+Eor+/"
    "kEkm6AAZsnF/cLPSPSxWqbOe6uvq26tra1xWWZnb6+v156mbgF1lXVldWVk9GxlxmWuz4PXr251dLBUyV1ZWWZnbu2tlWTVMVo6H"
    "JSWHRicnRvEwYMC+uVxWNVZZcX9/f39/mX9dXX5rmaJIABN6SxYICDSU360AAkMSIkOJUUVJSSgLgyQSEkmiajiNjYUjBwUFBQcH"
    "BwMFBwuJMBcXEw0TBAsLCgkKDAwkFEMiIiIkQ0VEKA0NKIMjkCuVzMrJXSuhhZLxYFBHRkdHSEhPT5CikKK/7+pwjodH6AAZt3tx"
    "mqhxZLGrq2pcam7R7+/s29Bxc1lqasj39ff128izf11dWVlXPVd/mnHLs39rOT0yPVlkWXB/ZXHU1Jllf39cXJ3AMCeHTTAwTfHx"
    "3HBdXVxWZH9/f39/cWtxPVOZcauzu6NDABOvUzMbMw4OD1uevkQSEhIUQ0NJRUkThUkiSVG9jH8tKzhGRQcFCQcHBwMDBgeFUSeD"
    "KIVJSQoKCgoRIRQkJENFRUlFKBMTDYWSwCgTkCzU39TEeCydok9PSEdHR0lJSVFRhKLxwPHq9OWxjriNABrNlZvSqVdl7+np27qZ"
    "c2tzc+n3nJnguruValyV2tDq6eq6nKhxWVY9cLGrc7uoWT0yWWVlZX9/u9J/a7Pgq3CZnXBqncBNMIdhwL/c3FlWOVdXf5maf5l/"
    "f3FrNVeom7qysbO7o4IAE9kuLjg1WkoODggpkIlEIhMSEhRDSRSJSSOJhNy9sZhqMzVggwsMCxMTEwcGBxMjDSMigxeDDAcKChEh"
    "IiRFg4ODIxMkJpGHODKiExOQOO3lzMx9LI+HR0dIR0hPhIWIhYSjwL9M6OqmysN9fJMAGuXb69xYZWRnsdLS6eDLq6lzampdWcv9"
    "+/W6sHFYU6rR2+nq9bNxPTWanbu73HAsZFddXZnS3PWzmWpYa/XSa5maZWec6GBgv7++jVcsOVZxf3+ampp/f3FlPWeonampqrCw"
    "s9HAggAT7V9eKy42LTRKMQ8OD1yRR0kiFBMUFENFI4VPvpGSobNxSjVwiRMLJjAlJAcHKIlRiRODI0kSEhIhJChJSSgoEhNET2Bl"
    "mTksLJATE4c47fPi5XEsnY6SiJGSkoiIiIiFhIRgoNydppOTlp/ovQAZW5Tv79x/cGQ5OTIsq+Dr6/XIgGtWPXGos/f779u7nlM4"
    "U2rL6ff1PT31q6vQ3H85ZVZ/sdz9639qWFhnZ+DUa3FxcGWd3Pu+uZ0sKz1rcX9/f5mZmZpra3+b68u4nKioqaqroLq/RYOKABK9"
    "nVhcUzQxNktLN1QVDypMT0RFFBISEhJFSImCRZGwsJxrXk1DBA1GMDBHRSNRF4UTg0MiIiIoSUkoIhMLFBJJhz2Z1ufUmRyQExOH"
    "TPbz8/M5K7juh4hPRkhHREdHREREhI18k5e06KOIg4S/ABdol7Ky69x/mql+f3FZU1OAmbrr97Ozq3NrapyV2urp2rPInHpqamtx"
    "PT3gqrLamRudZFad7plWal1/m6trceDUa2tnZXCwsNxrVjJWcXF/f39/f3+ZWVizq/f99vfcuqqoqamqnamhicBtABLbcZm7qlpK"
    "ODhLNksuUg4PFS8vhUkUFBITRIlCgoWIhMDSX0hJJAIkhF9XUSOShYMiFCRDQ4VJIgsTKESJwChJMEzc9PTtrymQExOHL13k9OfQ"
    "K7CiT09PSElHSEhISEdHYbCfvb+SUUlDFCbAABaLfoB/cXHS9evb4NuZa3BqXFhrc3Fc0vf19ZVcanOVcdDp6enQnpllNTk53Kq6"
    "6XBXs2VkZFlwa3+bu/W7OVjr4HlrZXGZnH7UlXF/f5p/mX+Zf3FZWICq2+vLenpzjbKqqaicqLGrnbH1bQAS6l5rq7Lk751oMRwq"
    "LVMtXi4vLhkbkIgjFEejhUiJg4KFuXAnAgsHC0VgjlEkJCgoJENFSUUTEyNJhb9xOJCFkjBc9vf8+JMphxMThys12ufF1yu4wJKR"
    "kZCIwMDAwMDAwKOjUYODSRQSEgwjwAAUp5yZa1dWa5rLZ2dTObPS0tK7gGtqXVlWZZXr9evpu3Nrapl+lev26ul/NTWz0qu791xx"
    "s3BkZXGgs9Lr3Lt/OTnr4Jyzs+vrqGvb1Jl/f3+Zmn9Ya12Zq8i6qpt6U1M4XHOr0Kuos9K7n7PsbQAS7DZTfrj9yGyYxVtUSjYz"
    "YFBhTEobDg5Moo/ouaGjhYKDvrlRAgUKEkWHjoVDJChFg0RJCxMjksC+qyxWh0gnUUbu7fn6/IstohMThCs429/l21y5h0/A/f7Z"
    "rZ2djY2QiIVJEhISDBISEgwjwAARfKd+gGtWV3Gzu2VZNTk1Mjk5PWXr97uzf2VXOTk9ObPp6e/p0Jx+c3FcnbuoVj3Sm5na2nNz"
    "mWtxqNL39Zt/a1ZdcV3r69vr0nFna2ep4IB/f39xPV2Zu9vU22teXlxTLS0tWGqN0NHIw9S7sLjsbgAS7TMzyPv89jNs/PrwbFpK"
    "TkZOTEozNjcODxlurdDakYmRvZ1RAgoSISRFiGGJg4NFFCMiDIWQnT04YIZhJ0dHREbw7fT3/FOVhyMihEza5+fl34qhopGGnot+"
    "lpWhkkhJgiIMChERCkJCIQwjwAAQi5V6ZVlZa3+7ZWs5MjU9OTUyOT1Wf5mZu9J/u39rV1dZXGqqu/f39+nLqXFcVll/9Zub0Fw5"
    "mnFxcev3qpldOVxxfpmz9/XbeYB7eXFxVvWAf2U9Vpmo4ODggG5aU1M1LjZMcrC4f7j168Ozs6uf66YAEu0zNsX8/Potlfzv9eqy"
    "nIpeS1NKSko2NDQcDxktetDQuK2+JgchJCRDRIiRRyYTI4XAhROJVz1dokYnRFFIT0+Hiuf4+vyvnmAjImFc6ufs5OXn139wcIuy"
    "uZCFhUNCQkIhCgpCIQohEQwMI8AADpaqcVk9U11xqFlWNTk5OTk5OTk5PT09PTVWf6iz0ruxmWVdWVY9cJ2d6+/11LtrXV2A6X6z"
    "u12amWuAmX9zWFZlZ2tqq9G7m5vIWX+ZmX9xWfWZV2d7gKqzs6lcSjU2Mys4XHJy3O/byLPL9bq4q42enFMAEuQ2Nrf8/Pwxe+3q"
    "9eBKbH2qr6xcXDU2NkpeXksZKRU0fOyjDQskQ0mFgyYmUVFJiUlIiSiSh2EnJ1FJR0hIT5HA7/j4+vz8lWAjJIdd7+3t7aVjbICV"
    "vb+IUYMhIoJCQhEMDBFCEQkKCgwiJrkADJa3czk9Vlara3FXNTU9OTk5OT09OTk9PT09NT1/ZT09OVbcsXNrf2tdWVddWV3b9Wtc"
    "gOvbnsuzOdLS0tydc3Fna3Fxqrvru6mAXeCZf5maf2tT9ZuZqMizssurSjMzNjM2jb3p7vD39/f30br1y7BwsJ0pGQAS6lOf2/z8"
    "7VszmOrXzzNufbLWepmrelpKU2xeWF5MOBUZG8ATC0lDKBMXUYeITyYmRCeFiYQnJkRFREhIUIejwE3w+vj8/PyWYCMo8e/st31a"
    "eZmdkJKFRYITQiFCQkKCQiERESERERIoJoSGmAAKlamAOT09V1d/4FYsNT09OTk5OT09PTk5PT09PTk1PVZWPT1Wcdzr28hncGVl"
    "WV1dWVllWYD3sqvpu1l/MjIyceCra36rq9KZmWtZXVPbmZmaf3FZVvvIqsu7u+Cym1M2MzNz9vvv+/v79/f39cuy29GqnNpwNTYA"
    "EtBw2uz8+vjDKpbt7c0qfW6y5Hp6maqvr8VoVDZOTkxMWmruRCMoIyOJiYSEUURITYUmg1EnREVERUeEYcCihqOj8ff5/Pz8mHJE"
    "RL+ynm99raCHT0cUgkJCIUIhIYIREoJCERESE4NHTl96i84ACIuVfzksV1dWa3DrcVc9GywyOTU5OTk9PTk5PTk9MiwsOVZlcHN/"
    "m9Dv9ffv3NK7ZXBxcX9xXXH7yMvg67trPTI1GzOzy8urq6pnWVlcmVi6cXFrZWSacffUnr29u+CVbEo1Srr39/v7/fv7+/f79+mb"
    "u8uenuiNc2wAEt9cb8b4+PztnOr158c051rF5/XZbliV5PXqyHNeU1NeXl/awImFkogmJidHUVFyLIQmSYNRRUVFSITAho5hR0hQ"
    "8fr4/Pz8/NqOhmqVrbi/o0gkEwwRgiEhQkIhIUISIYJCQiSJh3Jwip2iorQABVVbrVYsPT1ZPTVwmpvgf3FlazksLDIsMjk5OT05"
    "OTI1GyxWWY2dnX+ZnMvLyNHv+/frq6uZmZlxceDUnOC6cWVdcX9xOTmxu5uZmYCbf5qzsVPIc1dnmpqoZ+rUoKGivdtubFp+2vv7"
    "+/v37/b7/fv7+++eu8udnuiPjZUAEr+56Dnc/PyvnNDv5N+1njfXmNG3Y1Ns+vX36bqwulhYXr34TDAvFmBGJ1FHg09/M4cmUYNR"
    "RUhPiL+QhEhHSE+E8fr4+ee4jYqKf7yhkoMkQ0IRERERQkKCIUIhIUJCgkNJhYeMnsW9kYUmJ6EABFWlnD0sK1ZWPVZXMmVr4JuZ"
    "cXFlcT01GywyLDkyKysYMn9/sZ2dsKqrurq7mX9/m6rQ9vf29/ebf3P3qnnRnGpxcX9/f4BYu7OAfoBz4PWxm55Yu3OZmqioqGvr"
    "u46Ikr67lWxasv378Pvw0Li50Nzw+/v7q7vQqo3on43QABKiR4S/+5xblmil9OftpotK14uc+qpTbO3699F+bHmpq7ufq3KHckxM"
    "oGGjR4VP6HOIJlGFhFGEwL9QSERFRU+EkPvrz2+LgJWNuZGJRIKCQiGCEREREYIhIUIhgoJCI1GRj3qcs76RiSgiFCOiAANolLep"
    "PSwyPVZZZF09XVma1NF+c2trcHBlWSsrKywsMn+NjZ2dsbDQ0tDSs39XWWSZyMjR0cvL0evc3OqbgLrRnJlzgICblVjSq39/f3G7"
    "XTNYq3HvmX+amqialfXSoZGRvbp9enrv+/vwv7mgoKCgoLnw/f2zy9q4n7ieuJYAEvFERCZHkb+5U6/zVd/n+FPUi3rFeUpT9/b3"
    "1FxKSjdKXbGjo4RPjkx+q6JHhUjxmYgmSSOSh8BPSEdER0hQh6O/q3lrmX+Nh4VJI4ITQoIhIYIRERERgiFCIRIiQ4mRjYy4uaKJ"
    "hSMUEhISI6IAA7T76unS3GssWVZWWVlZV1dZgNGygJlxcHBwcGUsWWt/nZ2NmZy7u7vSu39WOVY9mqmzmZmZqJup29DLu2ebuuDb"
    "69rQqpybXdydcWVkPTmNf5uxgPWZf5qampuZ69K7ob27w5x9fu/u7u6+oKCPoKCguLzs/unR2tDon62MaAARc4iSwFGFhSdHkPv8"
    "W9+WplrI7dn1aErb7/X3yH43MzMzM41RIydhfzh+qcBHhUfoWIgmSSOSkkZHRUdHhKK+vZ+VgJuhooiDRUKCC0ISghNCIYIRERER"
    "goKDQ0SJYF9zn6CEgyQUFBIUJCQiJr8AA1oVXpX19eDUy3E9Kyw9VlY9WV1n0tGrm39/f39xf3+ZnX+Zy8jSu39dWFZZVn+amXFr"
    "WVdWU5vSm3Fqm8OyuuCz0NzruqqrcdydVzV/mpmrqKuogOuZmZubmpuZ69vLoL26mXl+gOnovr6Nj6C5ub29uLi8/Pvr7bZuW2lm"
    "ABGOiBMEEyZHSUmFhE+O7vycs1isetbUe1Km3/X1qi1YNTMzM3BFFJIbGhsZbMBEg0e+LJEmSSNHJ0RHR4SQuNC1qp6qvqNIJ4Ih"
    "QhGCC4IhgoKCQoJCQhIig0iIuY2AsLmIhUUUEhISEhQkg0lDJ/EAAw8pDypo59d6nNRlWVYrMjk9PVZkZXG71NLSsXNzgICZsbGy"
    "sX9xa109PT2amX9xa2RXOTWZmWtdf7vIw8Ozu9Fxf6vS0dLR6p1rc5upsqmqsbGpgOCVmZubm5uZ6+DSsdC6qpWVntq9sXKgudq9"
    "vb25vbmwz/z4p2YAFI5ECwICFEVERCgoR5KSkmGQ8WWpU7Zxr5NKyPXqqhx9UzMzNXBFFKMcGhkZbPFEUUe/OZImgyRJRERPiKHS"
    "r6qq7u6HSINDC4IRghGCQkIhQhKCQoKDg0WFh6CcnZ+hhEUoRSQSDBIUIoKDg1FIhJ3FAAKtrYsbDjQqFRuyu7OoWVY1GCs9VllZ"
    "XWeZqsvLyLCcs8uZcXFrazU1NX9/f3Fra2U5NXF/a4Cru9G7y7q4nbHcZ3Gof2dqamurs5ypqqqrsbm5sZnggJWZm5ubm+vg0rvQ"
    "sqaWqtu9oHCOobGgqn5zc729nbr2k2YAFIcjAgILF0aESIdHKENDKEmFhUdPvu72rZjIqFPq6t/MVVMzMzV/RSKjHBoaGWnxRINI"
    "7nmjJ4MniVBhv72tnbnxiVFISYKCQhKCEYIKIYJCIUILgkNRiWCNqrq9wJFPSEVDQ0lJQiIkQyJCRVCjjjgOKgAFrJNVDht6xdTL"
    "y9LRmWdrGSsyOVZWVlmZqpywq5uZa11WOTIyWX9rZWtkWVY9mpprmqmbu8jLyMu9oX+Z7mVlf39neamcyLqqqqqrqqChoqGZ65WZ"
    "m5uam5vr1LLL0cOmmKrcvY1yoLmwsPf299o4qp2y+qcAFGENAgIjJS4YSCclYYVHIygjJkSDhYdRkvHwbG4q81TP5EpjVTk1f0UU"
    "oyoaGhlv8UeDSKBLwCaFhJ251JaMkZKDKEVCQyKCFEIiQiGCERKCQhEkKE+HjZWMu72jT0RHRUeJiZKFSEVDJCQUIUVQwHBaVCkA"
    "BrQZDlutKQgc4MvU4NTSq7EyGDI5OVZXZXBxa2tlVzkrOXFxZFlZZFk9MpqaZ5mpyNvL0cjIyKChoXJ/7plxZJmoq7OqsqupqKip"
    "qqChkaGZ9ZWZm5qom5vgyJvQ1LemmJ/cvXKNoI1+3Orb6umNatq656cAEo5EBAIERy8VFRhIJxglLy8rTZJHUUQmSYVJJlGRv47r"
    "NsjbUqaoVTNzKBOjbTQ3VZP9R4VPwMBEF4lNi5ugo4RJQkITgkKCQ0MTgoIhEoIRESIkSYiQcnJwvsCHJyYoJ0iRvvHx8e6IJ0Qj"
    "FCEiRVDocFhaGwAGxSocast6LRYOCCr2+/vr4NTUmxsrKyxWWV1XWVYsLHFZVj1WVjIyLHGzZ3nRy8vR0tLSu5tqcoqNq7vg67uV"
    "qqqrsLKrqaioqKmpq6GQjp3rqJuZm9Spq+DIyNvburKeqve5jqG+atDr27Kp4NBw7rnQngARhyMCCxdhKQ8VFQ8lJRUYJRsbLVZW"
    "hoeFUSYThUkjRFGIwPfqU8U3NzdyKBOSlWZpbpj9T4VIhCgLE4NP8KNIg4JCIUILghSCgkISIoIhEYJDg0RhYHBwjoeFJiQiFCNI"
    "o/D9+/39/f3xYUgoIiEkSGHubFpUGwAGrxtTnJxq2a1uFRkODkr99/fn9dGrcRsWGz09VlZkZWRWPT01LD1rs11ru7vUy7vIqKiZ"
    "Z7Pg0cu7s7vI0buAqaqrsLCwqqioqKioqbGhsZnryKqbm6uqy+vR1NTRspyc2/ugoL9yndyqcbrI29Bq7r3angAQiBMCIydOFRUV"
    "Dw8PFSUXRi8cKSkpKi1aX2FFCwskSUkjJoWj8bj8WlJSjigoo2xuaWyc8U+JJCISDBRFQ0QoQ4IhISFCEYKCgguCEgskQ0SFYYaO"
    "jo2HhUkUIhIRISJIvvDv2tLS3PD9+6KERCgoR09g6F9TNhkABloOeumrgPV6es+tblMzGTZ6xQ4Pq7NqXWddHBw1VlZWPT0sMjKa"
    "Z3GrqMi60bqyqpmZmbCenJucy9DIutG6gKmpqrCwsKuoqKioqKiyuKub4LqyurOzu8jp2+DRu6qy2/fouaDocNGza3C679vQXei9"
    "0aYADnpRCwILRg8PDw8VFQ8PFRUlSBgcKSkcKSoZDhZFBwkKDCJJgyYjUYmSv5+KoihEv7yccI2Ph1GDFCESDBRFCwwMIYISEiFC"
    "EYJCgguCKEVIh42NndK/hINDExIRERERIkWR6NC4sK24vb3S9/GHT1GEYZCg6WxeWIsABmoOfuuMsO96nPdec3p6UzYZNjQpbrCy"
    "urCAVj09LDIyLBkrZWdxnamrqaqpm6p+cbO6urBqampqnuvbs9G7lampqquwsKuoqKmoqKnDq6mV0auyqqrRyLra6eDSurja/fe5"
    "jqDoc+rQqn9q0du7WO69y6YADoQCAwMHUU1YFg8VFRUVGBYYRhYcHBspGxYOOGFFBwYHCiELKElJIyJFSYmSSRRFwJCHUYNJI0RJ"
    "FCESDCJFCwwMESFCgiFCQoITgiNETYadnZ2+kFFFJBMUISERERISI4W/usugvb/o6Ojc0vGIkY+4n5iUrIsACUpqle+MuPZ6nPtL"
    "jPeMusieFjZael56np66usuoZ2scFVmaf3N/u7urqpl+gNHL0X5qWFk5OVZTgJy60dvLm6ioqaqrqqmoqKmpqrKyqZyZyKm74Nuz"
    "s+nc69zS0vD78NydhqDcc+/Qf37UyMtqTL241KcADgQCAwMDDCNRYUwcGRYbLhgYThYWDg4ZDhVgRCQLBQUKDAoJBwsoSSQiIiIi"
    "FBJFkidFCwsLFERREyQUEyRJDAwMDAwhgiFCgkMmhGFlcYCropJHKBQSEiQiQ4IUFBQjScDQq6C5osCjwP3aue6+vbZ8aVsAC8sb"
    "c573jLj7fp77U577XrjQsA9Ken4OCAgPNlN62+CygJt5c2W7s51xXFzIu5uAgHldWFNTTjBOTXJznNTR0duoqKmqqqioqKmqsLK4"
    "sqqyquD14KuqyND39/v7+/v78PvojaCg2n7379Db9eupXXKguNdpAA4LAgoDAwMDEhNFhaOPcg8VFjBfFRUVDk1HKAwHBwoSCgkJ"
    "BgkJCyhDExQUEhIUhZInRQwMEhNEhSYnJygmSRIRESGCQkITQ0iQhnKcoKKiTyYjExQSEhIkQ4IkFCNEJ4juqauQkKLAiYXx8b7u"
    "6pNmZgANnhlznPeMy/p+n/danvBcuMuVKUqWupVuaDYVDg+Alq2Zf3Goq7GNc8vp0Ytua2t5alheTE2GiJKjj7Gou9S71Kipqaqo"
    "qKiyyNG7s7qz2/Xgu6ur0uvp79Lp6fvu3NLQ0NqOjqDSlcvr6/Xry1xYuZ3I33x8ZgALwAsLEgIDAwMDBgoUJEWEoo0bL18PFliR"
    "SSIHBwcKIQkGBgYGBgkHC0NFQxQTFBTAkidJExRDJ0SDFyYmKCODEhISFCQjR5FgX3KgoqOERydHRzCSRBQSEkNDgiIiQ1Bh8dyd"
    "vqLA8aOJUVHAvuquABD3KYCy/YvI932t91Se7li9ng4bWrLIXrLRfmpUNxYIreC7gIBrZXNzy+l+WlVcWGpcXI6hovHx8fHx4KjI"
    "4NTIqKiqoLGzqamqsqrR1NSzu8u76evc6bOxuL3q9ffc3Nza2qGNob3Ra+/36el+aqGdsevl4tNiYgAGvVFGkMANIiQMCgMDAwcJ"
    "CgoMJEWFkqNMFmFEKAshQkIiEgcJBgYGBgUHBwsjRSNERCIiSUMogyhDJicngxcmJigjSRIUI4OSYXBwsL6iiScnSUdPMCsbFiWJ"
    "EhEiKEVDJkhQkP37uejxwJFRiYNFkgAT/i2Anopste9srfdarepY0LoONm7Q0FzI6VOp+oxqDw4z5L2Zf39/s8uAUxkVS0yOoaK+"
    "8fH7/f39/furq9Hrup2goaGhnaqqqbq7y7vLy8vb6unau7CwuEwr6tn36Ojo3NrcoJ2xuJmwsJxzldqgodrf3t7y3WYABigHC1HA"
    "F5JFIyISCxIMAgYGBwcLIhNJg4NFDAdCQoKCEgcGBgYDBwICFCQiJlEnhEgjFAQMKImFJhcmJ4MXIyMkI4MoUWFfc+joo4RRRCZH"
    "RzAtFTgtNxsvhQsKIkRISFCEhKL+79LxwJBHKINJKIUAE+8qfp6KfWxeU632Wq3vWtH2KUqV27NYutpYpv04GPAWFcCJwJ1xf9CZ"
    "WC0OGV+Gv/Hx8fH7/f37+/393KvI97Kdub6+ubHL6+Db0bKyq+rr0Luwq6DSZVwrOPff5+rp6O7p67mwn7jaqqqwnbDSob395d7y"
    "4aRmAAYTAgICSReJkJKDSSgoQwsHAwcHBwcKCgoLBwwSQkKCQgcFBwILCwsTIoNDIyeJhGFPRBQHDChRJhMXJieDExQTJCODRKKp"
    "ub+jhVFJRIRhLysVKy5LKiktL4MLCyMnJ0aHhIS//tDc8cCRRyiDSSRRABPwKX6ez62WaGhLSkrP+lTXtRxUlum6U7rpaq/7Rhi+"
    "HBaFDYmOco3UmWtKNlPw7vv7+/v7+/v3+/39/fe7y/W6q72xueD14MjIqrLR9dCdnZudsY0yMjlMTNzt2Nj09enc6+/psLigsbGx"
    "uKCgoLHq9Nje06RiAAeJSRMLKBOSbEpOkJGFhUUiIgsCAgMFCgoMDAwHBxFCIgcHEhMUFCIjI4MmIyaFSFBQRRQHDCRJIxMTJyeF"
    "ExMTJCNJQ5LAkURERCdGYCldGxspLi5fKxtORoNDJicnREiEhIjA/rjS8fGHT0RJghNRABPwKXqebCls2dZ6plpSW1TKnhVanOq6"
    "WMjva7L9TBixKSqJC4WOiqDUlXlsXqr7+/v7+/z7+/v7+/v7+++7yO/by+/33Lups7va69u6urG5sblXMjhccp3qz+Xm3d7m8+3p"
    "7vv30LGxoaCgoKCgoNv45cqkaVIACE0ITYWJE6Nz7exqS5++iFFFQxMLAgIHIQcFAwMHCwsTFChDJCIiIiIjRSMXJ4hIUFBIIgcM"
    "IkkjExcnJ4UTIyMkI0kUJCQjSVFGXw4VWI04NjYuLnIuL05GhEQnJ0hPUJHA8fHvsNLx8cCJSUkiI5EAE/spcZy6lTYZNGxura9v"
    "UkqLG3qc6sha1/duw/45LMhoSpILg46MseCVel5z6/f36dDa2dvq9/37/f39++nL69vU27q60unp68vQu7GxjY0YKzhwjdrI9Pnm"
    "8t3y8vLm7fb7+/X377m5ubmxvej3+JN8aVVVAAkXAQEN8RfAcDNLn/SLU5WNkYVFQxMLEgcCAwMCBIlNMGHASSITIiITEw1ESE2I"
    "SFCESCIHDCJJIyMmJyeFFyMjIiNJEhESSSUWDy42NjhLSy4uLi5hRocvJYRGRlCHvr/w7u7w0rG+8ZGJg0kCAoMAFPApc5nQ2rq6"
    "Wi0pLTa05K2tKn6c+9BY2/1+y/5cavd9WsANSY6LvsuZfnqq7/bv29rQ2tq92uj7+/37/f3L68iqu9LS4O/p0KqgnX+NLC9XcJvq"
    "5/j05eby4fLy8vLy8ubt7e326+nb6unq6uysl1sADRcBAQjxF5KN6ctvS33XyJVccmGFRSgoEgcCFCRHYBs3LeiDKCQkExRDQ0lR"
    "hYdhTU+EQwwMIkkmFxcmJ4UnIxISI0kSEhOJGCsZNjg2OC4uLi4uLlBQTi4rj4ZyuZ56StLu7u69rZy/iFEoEwRJYQAU7y1znJw2"
    "jPbI78iYaEtKWrcqfqrpinqVbGzZ/pyc+m9awFGEjoq8upyVlfv36travX+Oj42Njbvq/v39/brbyLP16+C7ubm+cHJcLitYcND2"
    "3+fY2N7m3uHh8vLy8vLy8t7e3ubl8/nn7e2vlGkADycEARfxF5JzU1Ot155TU1qcumpfkIiJEhQiJ0hQXy1uKtyIUUmDhYMiBwcH"
    "BxSDhZJhURQMIlEnJSUmJoMnIxISJIMkKEeRK0wzODhLLi4uLi4uLkxfhktLvtxqYykZAAi+oohPRE8AFvYtcZ5+NlNKNtHbuq16"
    "WlPIKX6e27eebmhsXlp6tbVuWr5fj52c0LKrnLr7+9y9uXKGj46OnJycu+/7/f266/fr0KuxvrlXTDgvNWpz2+3l2Obm3uHd3eHh"
    "4fLy8vLy8vLh4eHd3d3TbW9vaWkAEEmDJk/AF5Jz6cpUN2zFrXpaaHqyWD3ACyImhE9NbDFuLdCHh4koEgcDAwMDAwICCyOJhSMM"
    "JFElJSUmJoMlIxISI4VDJ0aiKzg1ODhMLi4uLi4zMzZf+/fsXDUqACftLXOe+/utejMbM56t1+rXyylznNeL7Pj613pTb2g3Smja"
    "uL3o6O7Lq7Lv/evSoHCOj6ChuLiwsJza+/390Orr7r29f0xMOFxXf7P2z/Pm5vLy8uHh3d3d3d3h4fLy4fLy8t3h03x8fGkAFEkT"
    "SYXAE5JeM1Sv1mhUVGyWnF5zmVaSCxQoRidQjmx8MdzAUIlFKAMDAwMDAiQEBAtHhUkMIoUnJSUmJ4QmJhMSI4VDJ0aQKzMzM0te"
    "OC5TXjNTWq3LlktLNgApz0p+nvvQ5PbI0FoqLWicsNApfpzqUykbKS1s6uevxUpe2r2PjFg428vL++vpoI5/jqDQ3Ojc7r2wsNv9"
    "/fe5c3I4ODg4anB/2tfV2Mqu0/Ly8vLy8uHh3d3d4eHy8vLy8uZ8fHxiABdPRRMTiROJXOq0NjZ6r4t9U2itrX1YwAsURCdHg4jA"
    "uS7uCIkvJ0UMDAsCCxNJRUNDJoWFDBRRFyUlJieFJxMSEiSDQydGkCkzLi4uNjY4XsuWz616WjZqACuW236e916KcX779/7IXik2"
    "2yt+nOqZlbCebBYVFRYWSmueKysuWFq61NH369CPco6xu5uesJ2c6cuey/3r+71/ZWVwjXPQ0NblrqSkpKSu09Pd5t7m3t7e3d3h"
    "4fLy4a6klGIAG2BRJCQTklgzLZjWaDRoaJ6VU3q1c8ALFEVJQxMSE4VNiQIEkpILDBKCgiYng4VIg0NEhQsihRcvLycnhSYjEhIj"
    "gygnRpApLi4uS0o2ps+sNhsZAC9j58iw9/DQXVxTM7rv7OTL6i2AqulwXo5yc05eXFhYanKOG05OUF+e/vfr0J1yjqDQfmqz2rpq"
    "nOixufvb7e3QnJyc7Prtl5SkpKSkpKSkpKSu8vLm4fLy8t3d3tO2028AIGGFJ4mjkH9LLV3LlV5UfcW1emrACxSFIwsHBwtFSSQC"
    "AhMNEyIigoMnSInAkolERIkkJIkmLy8wJ4gnJiITI4MoT0iQX1guLp6trVoqKgAyN3zF2+/78Pfo6IxLXq37/e8zmavbemyPYGFy"
    "XF9fco+GXjhhhIiOsP779budjo7SnJzS6+rrs3OgvaC92a6lp83t5aeUpKWkpKSkpKSkpKSkpKSu4fLd4fKurq5pW2YAIjaQUE9H"
    "J5G5aDRVqsp9Sn3FjMALFIUjJigLCwcKBwcHCxNDRScnhUiHv/v78aNIRFEmiSYwMCdEiScnIxMUSURQUGFqfqqcfUobLQA2Y+Tr"
    "7Or39/fo8NBcXLDQLZWc1JWfj0aEj19gYYaPhlg4TlCGjrj+9+qxf46hvZy77+nqy8uwXL2gsOe2rq6utq7Ktq7T09PT09PT07a2"
    "rrbT0/Le07bTaWIAKCtOo4WJRmC5nVNKt9C1Xl7ACxSDJ5KShUkkBwMCAkIkQ0WEYcDx8f329/fxkkiJJ4kjJkQjRJIvJxMTFFFE"
    "wJChu7JTMyoAOWNol/jv6dLc+/v78LF/ui1+nMucjpCHiI+QYWBOhk41U19yho67/vXavaCgvXDQ0JmxftC7sFi9oLj03uHh4d7e"
    "3vLh4eHh4d3d093d4fLy8vLOynxmW2YAKikpK8Cjkk9Qo75yOHrPsMALFIMmJwQIT4UoAgcLgkVIifH7+/f37/f3+/vAYZIwiRMS"
    "FBNEo0ZGRCcmUUTAoVgtKUoAPaXl7fvv9/Xv9fDw6epsfou1j2BhjoZfkE5fYHJcXGpycKDp7/vgq3N/jr2c25k5WNva0LhYvaC4"
    "597d4eHy8uHh4eHd3d3d3eHy3t7m3q6nfFtmADApNobxwGBNwPG/SzaSCxSDIycCAQRGhQsTQkNIkP379727sau53PD3+4dQwIkT"
    "EhQjR8BGTU6QklFEwKp6AEDd0+Lz5fP4+Pjs+vbv/H2Wur6/uY1qTl9fTnBdal5zy9v7/uvg26udjqDSauuZXHCq6cu4WL2guvTe"
    "3eHh8uLi4uLi3d3d3d3h3t6urpdpY2YANC0tWKHxhE9ho6OJCySFF1EBAQEmhAsLQkOF+/vpsKCgoKCOoNz7+4hI7pEmIyYnT6Bg"
    "h4SEUUdHAEPd3d3y8vLe4ubl5eXl7d/b29Hq5Oy8n6COjbCq0er2+v316enr4J1yjp3pful/cHOV2tCxXLmd0PTe3eLi4uLi5uLi"
    "3d3h3d3TpadpWwA7Gy6GomGERIMLKIUjRwQEATCFCw1DRZL77uihoejSvqKgjejwiEbooUYnJ0ajuU2ESABH3d3T3d3d3eHh8vLy"
    "5ub5+fPl2OXl5efn7e3n5/Pz8/n66ung6enSoI2g0Gr3qoyMyLidjViwuO/m4eHi5ubm5uLm5uLi3q62pWNjYwA+KS5/h6OFE0OF"
    "I0mDRxdgiQ0kJ0jA7rGOub7Av6PxvqCe78BGv5+Gh5CjhE9EAElptt3d3d3d3eHy8vLy8uHe4fLy4eHh4eHe3uHh5ubm8/jp3Onp"
    "6Z2Njbncmerp0Om8sY1ysZ29+tjy4ebm4uLi5uLi066uZmJmAEQbGU2Ro8BHKBQog0+FEycnh7++nbG+kJKFiYjAv57Q8UbAoYaH"
    "T0dEAEtmfHyl093h4eHy8vLy8vLy4eHd3d3d3d3d3d3d4fLm8/jp3Nvp6dq5nZ29nZ7a2um5sWVwoLH2+Obh8ubm4uLi3qWufGZm"
    "AEkuMy6M/cBJIyIkIxMnRqPojnK/kIMkIklRkeidufFNwFCERwBQaW1tfOHd4fLy4fLy8uHh4d3d3d3h4eHh4eHh8vLm+ffb29vp"
    "0qCNoLGxnbvQ2o1woKCdvfzl3vLy5ubetq6lZgBQLSkqc5CEREcmSGDAuIq9vkcoIhQThYn9vtD7j8AAVltpZqfy8vLy8uHh8vLh"
    "4eHh4eHh4eHi8vLy8ub59Nvb2trSsY+goOhqc3F/f7Gdnff05vLy4dPKtmlmWwBgo1ELFMDAKEXx0NoAXGZpfLbT093y4eHh4eHh"
    "3d3d3d3h4fLy8ubz9PXq29rQnY6OoLHQ0tC9oI2d2u3l4uKupGlVW1sAY4MLE4iRRSjAAGBpaW2Tyt3h3eHd8vLd3d3d3d3y4uLi"
    "8uLm+OTa2tq5oI+Oj6CdnaCgsbLt5eanfGkAaEMSSWFhUUnAAGNmaWmk3d3d0+Hh4eHd4eHy4uLi4uLi8/r07Nro0qCNjY2NjZ2d"
    "yPj0lJRtAGpFJCiFhERRAGZVaWlp0/Le4eHh4eHh4eHh4eHh4d7e8/j0+uvuvaCdnZ2729eXk2YAbIlJJENDRABqVWlmpN3d4fLh"
    "8uHh3d3h4d3h8uLm5tjl9O3s19ramHp8YwDhaWZprube5t7m8vLm8vLh8vLi4t7e2Obzl5eXbwD/AP8A/wD/AP8A/wD/AP8A/wD/"
    "AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wCs4+PTwnc6Oj4eHh4eHjo6"
    "Oj53doG23rZpZmlpAHbbjVlxMj2o4GRkVjIAX5PT48J0d3VBPiAgICAgHyAgPj4gPz91wsLCwdPT065vAHPD9aBZnWQsMjIyZNLg"
    "mn9/NSw52/j5AFSUy8OkdnRBPiAgICAgIB8fHx8+Ojo6PiA/Pz4+QEF3wneBwQBy534yGBg9s+BrOT0yMmSo4LOAa6tqXHnIAFC0"
    "vJ9eOEo8Oh4gHx8fHx8gIB8fHyA6PDY0OiA/Pz4+Pj4+Pz4+d8LBpABv4F0yGGQ9PT2o0n9WMjJkazLgyMPUazJWcbu7qgBJyuzw"
    "ok1NRi8pHR4fHx8fHx8fHx8fHx8gHTxUYjo+PkE7dEE+ICA/Pz8+QcLCwtOkAGWXl5Z9lpjta2VX0jkrKzJWZGSamrOof3E9Vpqa"
    "f2QsLDJ/mrMARuS9Tk1IREREMDc6Hx8fHx8fHx8fHx8fHx8gPjo6QUE8Uk5aPD4/PyA/Pz4+QD5BdePT03YAX5h9i5R9en3Fltlu"
    "bmzUcWQ9GBgYZLGasphYZz0yLDJkmqiaKysymrIAQc7ajYZhYUZGR0REREYxHR8fHx8fHx8fHx8fIB8fHyAgHh07S0ZGTDc7PiAg"
    "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"
    "Sot6ci+HcrtsSjix7tKMqmpz/faeiqzHpufNrpeTfGlbUltvl5espaessKGQorm8t6/8+cq21+cAJLByTJ7i5eyfjo9fODAvco+f"
    "y/fauL28SykbLis0VWbizq0uX9DTd0B3xp240Omqm3MbGysuKUtyj7lopHaB86FyuMsucIZfhoadjufTwaSYfHZ1dXV1dXV1d8HO"
    "ACFbwR4QAQEQHx8gd3RiUzUzMzMzMzU1OVxTLC04amuVinqcarfIt9GYYzZ6uVhcXn323J1foIzNxZZ9eqatrNjOpZeTbmlmVaaX"
    "tqaXrqasrKytsLiyr662rPz+/PTt5dYAI7pfTSkcNJe19+qwf3JMTjg4TL7x6vr2y7iNjFwtGTRti40vYMx0dUHj8+7oy5VTLC0t"
    "NVOduNChX196pMGB5p9OTI+Gj4Zyj9X5wcJ3dpR8dnd1dXV3dnR3dI+/ACFjwj8fHx8fHxAfHx8ePG2cUzM1MzMzMzNTXVxcUzZK"
    "aLXRes+VecvL6npaLVps2n9cX7+92pa2yqyup7bKtpd8bm1UW2iurq6np6aurq6vr7TP7dbKtqysp/j+z7WTk9gAIvjthlBNOC0q"
    "Ntra0Nu8j4+OX3JMLiuP+/D999alem5UUy44MIbjOnV34ypTKykrLTUumZ24y5+Nck5fn6SBgfONTk5ghnP6/NPBgXZ0dXaUgXd1"
    "dXV3fHx8aW1GiAAhm4F3Px8fHx8fHx8fEBAOFSleU1MzMzM1NjZTanpqbGg2U3rPyLXPbpyVrUrLXl6csq3IxZhui7av37amk3xt"
    "blVolKWmpqenp6enpaavtdftelo0l7a0rKfK19DLpq62ACDz+ePBpmCGX0wvKysrKxs2zfT4+vSfcl5cTC9MXtXs2e3v6XJyvrnj"
    "QHV30y0sKxtdan7I6PCKLTYqXl5fvNPBgeamcI6e9LZ2dnR0dnZ0dHaWgXd1dXd2coaGbF9IgwAfzLamYsF3d3U/Hx8fHx91AQEB"
    "AQgWTjhOTDhLOTlKVF1qbH2LU0p9rZ7I5JyyStG6uq3PrXpsi9avr5iLem9pZlVttqenpaWmpqeurqbVzc3PWjYtKi1U56ysp7Tt"
    "lWrfrq4AHWnB08LCdXTj+PqKTk5NMDArKy43UlJ8+fr2rbByTEw4Tk6t0en7hl+h6dNAQXfTSjU4WLn9+8tqWFNKSlSVjIy104HB"
    "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"
    "t7bW3/THr7Svzd/Xz8/0tKeu1tmLbu3NxbV9VGiv36x6aJjHtLa2trS2tKy0rK/Zfa16aLLI7c/89rW3pva1ptkACXsWGCUwDho6"
    "dEEeICAgHyA+Ojw8PDoeOjo0UE5LOzo7OztiVEtQUEs8Ozp0Ozs7Oh4gICA+Pj4QDhB3gePTwYF3wXZ7xMTBwjsdGg8aEEBBQTtA"
    "QHUQdMQaO4HCgaS8iGGg7uu8co+IkJGRwL6++7/xve3lfGMAFd9KVbambFJVptmvbTc2Sjd69K80potTUjc3NzYxMRwVNFKstKy0"
    "tlqVyMbW2NavprbNze3Zfa36yPWvrK7W331j0a1uWmzWzWNVaHqWsreutrSnpqemrKy2ze23fVqtyMXni+ScWlS3/LWt5QAJ6b6Q"
    "iE0tFRAQECAgIB8fICA+Ph4eICAeHjtVPBo7PDs6OjxfTlBQTks8OnZiOzo6QB5AQD4QDjp0dHR2wtPBd3d0Gho7gTzjGjs7Dw4O"
    "GnQ7Dzt0dx08xBpigXaBrqGIkOjajI+PkcCjhjDAjrn9+/365W1pABXfrzGLx9mvmJhbaGOvtsWmN8f8NErZ383NaWlSN1I3NzQZ"
    "HBwZts7HWpbHtsenrK/Nzcy2UlVUUn3X9rSnrNbZi1SKXpzXplpjaG6Lzd/Nt7W1yK+0l6evtqyLSmy1uu31xWxumGOLmK36ta/l"
    "AAitYZGj7qBbDxAQEBAfICAgICAgICAgIB8gIB4eOh07PDs6OjtLUFBQXmI7QDs8OzodQDs6EBAQHkF0dnR0dMHCwjo7Ow9iOzvj"
    "GhoaECwaGngaGmJAdTs7qDEPGjyBpY+HkfC8coaRo6LAhk7xkI6N2v7OfGkAFt8xN5aWVWysr4umfWxsi25o55g0U8ivttbW1pN8"
    "UlI3Nzc3MRk31adTmM+0pq/Wta+vNjE0UlU3bM32tKes1dmVSs/si15jbpbn3+20tra12ri9y6+0zZV6NpbRyNHLt5VueqaylcjX"
    "yNettucACFcIFwiHjst2Oj4+EAEQICA/ICAfICAgHx8fHyAgHjo7YmYxNE5QUC4xOzs6Ojw0OzsQEBAdHT8+QXRpYnR3wsJ3Oxp4"
    "D2I7EHgaDxo7gXY7djtiOxo7Ozs8UzxigYGfiGGQ2piMn7+/K77AwMDAuWq19JRtABblWi0txdeebDNSl5imfXpTKqbPYzTNx4s3"
    "Y26m39amtGhSVVJKVGP0lnqvzcrPz4tpKio0UlJak2iL2fisp6fN7Z5Lemxsns/Z2+zIz7S0xbrqrYzt156v1M/459fkWlRjbm55"
    "nNv1+PzPz8fF5QAIXxcEBA0IYMFBQXVBQT4QECAgICAfHx8fHx8fICAeHjpvhlBQhFBhLio8UlIqUloaOhAQED5AQUF0PDw0dHfB"
    "wsE7GjsaYngaDw8PGjuBgTs7O3g7OztiPGKkk258k7iIh8DWlKVUTL5Oh2FhYE5r/f6mbgAW7c1bSi0pWnqsxZZKWnqmzWxsr5g0"
    "St/NzZZ6aFptlrbPz9a2UlRszfhsfcfOymxKNCo0UlKUr83H2Wys+JamprfrtWx6nK3P2fWcKVy3xcXFyNHR0VNobHr22cVubG56"
    "lYuVter16uTn+sqv1t+uAAhwFwQEBAQlrnbCwsJ3d3U+HxAQEBAgIB8fHx8fHTo6O19QhIZOTk5QUFBGTks8PBABHUFBQXV1dXd0"
    "PDx0dXfCgTsaO2I7OzsPGnaBOzt0Oxo7PD0aGjw8pJR6eox8oYiI8M58rm9eoZGEUIdfNjZKU63tbQAU382vfVRUUjQZNDSTyrZS"
    "U2gzrdmWNJbHeoumtrZsbFQ0W6/H31tjetnfepjH5dZuGzRTaaXz9Pj4tvRopufFmKWv38evttbNzc+LMxsVXOrf2eR9VFOc9vbI"
    "pm5ofYuYprLb7efZ1tbV1fTWstSvaAAInRcEBA0NRryl+ePywXbBd3U/IB8QEBAdICAgHjo8YjxUTl5vKUtOSEhQUE43EBAdd3V1"
    "QUF1dXV3dnR0dXV3woF2GjuBdjt0O3TCgTs7PDwaPSwaYnaBdqeMcoaGn6GIkeyuaa6njoaQh4SShlxTLhYcLXoAE9amepatlXpj"
    "VUo0MRwZKpbnbEq3zTRUz+3NVUo0bZiYppiLN1RuKlqv9M1o2c3lyosZaK3PzdbnpovF7W2Wrd+YpafF1tbWx8fHyqxKGxldbFQ0"
    "mNnP5OSelWxsepWtrcjX9eff2dbl2MzW7efttW4ACY1PFw0NCC/axrZmmNXO08F2d3dBHhAQEBAQPz50PDw7O4EaGUxQg1A4MTQx"
    "OxoaO3d3d0FBdXd3wXd2d3d0d8J3wjs7gXZ0gXaBO3Q7OzuBYjxidoF3d4G0hmBhjo+iiL/trmZmlo5gYYeIiJCgnY5fVFTfABFm"
    "tpNpaHzF7d+up29bUjc3MSkbGYvWzxtutpavz9+sUjZVfZiszdY3Nn3H51St0c3ntYtYfdnZxaaWls+v7Wymz+elpaWnrq6srq6u"
    "tudTKRt/uqyWxZ6YVGxTbG6Yt83W5/jf39/f1tjOztTQ7K99WwALwMCEJSVQ58SubXqe3Mi6tqXBwsLCwkE6QD4QOjoQHRAaVE5Q"
    "REgpGh0dOqR9b3yBd3d1d8HBpKSkpIGBdnbBdsJ0GnQ7Ghp2dhp2O3TCwsI7doF2dnZ8vIZhh5G/o5G+16dbS4yGhoaIkpHA8e7w"
    "9srNABFvymhSY2lvl6y21tnNlpZsVTcxLTZ9z1sqmNavfWyWttbtfVpobouvSjd9xc9Kxci3362AWJXbfnqVz5aWlt9orMvtpZel"
    "p66urqenp838bEpK6e2Li1SLt1pofZWyz+389OXl1tjY2M7Ozs2jiMCKAAyfv4ZgkcDw89PTpdmzsZyz0vf809PBwcHCwndBdB0Q"
    "EBoZSCclGC47EB0eMY6HkJ/Bd3fBgaWMX2CGUF+GfJTTwcE7DxAaGho7O2KBQHfCd8J3gYGBgZSYvaG+kZCQopG/5LRvcoaGkE2I"
    "iYhh8e7ttlsAEa6lKjdjaW1tyraspq3N2d/Fi4taSlrFxSoxW5inr7VoW3qLxef0pmhSKlJuz3qL1vrW55iVXH66z9mWU1KYtHxb"
    "p8XfppeXp6espaenl5Tl5+3q9shLi559z36VnJ7t+Nnf1tbl2M7Y3t7KvL6Jg5KLAArY2OSGJSWG2vTYtoF81uD19dHbyN/52Mrn"
    "87akgXdBdDxSS0ZGFxgZDxAQQWlyYWGGtMHCd2JvXkxNTVBNSE9Nhs7BdBAadjsaOztigXg7gXd3d3aBgYGktp/avaORh46+6NrL"
    "oYyPj6GiDoajYSVg3OvlaQAQtnwpUlpoZm18NA8V39nHpqbN5/jNx2zNt0o0HBwWW321zYuWk5OXrM16KVRu31qW2fj059bZmK3P"
    "WlV9tpNtaVRjtM3fppaLlpesp5OnysrO5c+1Wq1ejK2L262y6u3Wx8rWzMqlfJfY5djXv5KFhcB9AAqlVF6OoZCGGyqXfGmUi2xu"
    "lbLnppaWeuv9/vr83sF307WGUIRIMBYPEBAddnqHhGFgpMHCOw8rLmBQYZCRkYRIYbzBdHR3gTxieGJ4PFNigYGBgXx8k5SYy9q/"
    "voaQwPHo8Nu4vJ5ehr6iL4+hkCUPct/zbwAPtjYcUlpjZnxSVBsVFRkZY83Nz8XFxdbZ1tF6VDc2MRwZVFK05aank21UNDZ9x3ps"
    "rM2v36ZVVGh6t8/ZemNVY2OTk87N56+spqalk662ttbOzu2srVqqipy6lfr1z8e0tsq0tpRvVFU3Utbkv5KJiIjokwAKp25YK3Cw"
    "WA4ti6yXrJiy+8uVssOWze3RmZuytfPBgeJgT4SJkaEqDw5Bd3Z9kIiIj8F2GhUWKS+GkcDx6MCjiYdOb8F3d3YQeIFiPD1/hoyM"
    "b4qKjry9v7+Pj6Lx6Oz278u6uL2fX4yhvr6/ob65sBtb5pQADW+XNjFUVWZpbW0pKy4VFRsZGRkcp5bN1s3N2effyltVVFI0NBsq"
    "G1rHzdZKN1p62X1oza+WtpambFRKnotUbGhufKXW+OXP7a+slqastK62ytbl2Mdst26wcJ66lvz41sfHtqdtZlJSbq/Ni8XQwJKJ"
    "oaH2lwAKrmxeDzFmFRtec9fV1eScbJWq9ciLmH2qutLLtdXBgdAICA0IT2GGjFJ0d3Rprbi8rsF0DxYvYIaPwPH37PDowJKHjtN2"
    "dhoxeDxik5dyhoaGj6Ghor+hj6C/6O7f2NjO5ezRvb24vb6Gj5GRob7a9vP0rq4ADJN8UzZaWmNabzcOGRYWFRYZFhwcFhscNLbP"
    "z82yz9bn9K9oblQ3NDQqDzQ2NFJsfaZoltnHzd/f2Xp6brWYbXrN5eW25dbWze2vr6avtsfHx9a2pW1oU9t+0I2eyK3+9MrOl1VV"
    "UlJppvT2+u+f2r+jkb3D+fPjpQAHpaVtr5iTNy1MYIa9vLzs5PX64Kupi9nRu2uZqdDkroG/CAQBBBcNUKFWeHZ0dGJpgYF0GhUO"
    "Tr/u8fG56tns7PDxko/KgXY7MVZ/jYqnjKChob6hj6KPoOjw7OXY3d3d09j459C9ub6/kJDAkpG/0OXOylsADJd8UjFUVVtSk1QO"
    "DhUWFRUWGxsZHDEpHBkpKjTF1NfKztXf5+emlm00NDQqGTRSbqavW7bP9PT49PSWmH23z7bt387O1uXWzdXtr7bNzc3W1uXKbjdV"
    "t7XrnMuxsO/++sesWzdUaKbk7e/19/v7uOjw6L/b56diD2IAB2i0tMqXbzQ2TpCGX46hv/v9/v3p0MvpuNC9q37I7NPB2ggCAgEB"
    "ATCQXZN2dmJpdoF2Ow8rX8Dw+vvwnPz59Pj78aOP08GBgW1/jpCPvb65oqG+j46/9+rk2N7d3d3T3d3d5uXt8PDw8aGhwL+/vbS2"
    "VVJSAAusrDFSVVtbaDR9bkoVFQ8VFRUWGRsZKRspKhkZGzQtppas1retnrfPxYtpN0pSU1Svx2xuz9b0+Pz49Jycfbf49M231eXK"
    "ysrHze2vxdnZ1s2TNDdjyqXf5/rn7O/q9vrltG9bfcXk6tzr6fX7+/vpyO/Xz+zfUhoabQAEMbWfatDszcxjS1hqv0YYJWGRopCi"
    "vuju8FMrK+ju7+r008HWGAgEAQIBJaJcfIGkl5iugXZiSk699/nj5raXytPe+f79v5jTgYGXno+PkKK+oZCHkL7w8OTl5t7e3dPT"
    "09Pd3dPdztjk7evr8Ozt7J6lUmI8AAtbZs0ZKmZjVTdobFqLsrItKRUWFRU0GRkZGRkbGxsbKRkbDzRjtot6epiWxdavmFpaW83P"
    "WovN3+f4+vrkU5yLt+3Z1s7Nx7bH1tnN57bN1otVHBw2VZjl9PPz8/Tz9PTl39aXMYvv6u7SjaCdu+j39/et1+rR588ACA8bLyWO"
    "oLvZM0psevGQUEgwLyVGUGCgsJ8bKV++2u3s+dPB059hDQEBASWjcpN8fF9On5RiLnK95ePBgYGulJSkrtj0/ey8zpSUj6G/v6Kj"
    "h4aR8fDa5Nbe3d3d3d3T09PT093d3d3O2PPzyrZtfHxmZmIADW/Kr3wxNzQ3NzQtUmhur822plUbFjYuLRsZGRkbGxsbGRsWDw+W"
    "bGhobWiWts3P1sXF1npsr9bfra2vk816nJXF9N/fzs/N1s2v38/ttu3NUkpoi8jNl+Xz2NjY2NbVzdnfxmiT+OvcoKChuaG5oL3v"
    "8LCt+sjpvAAILhgYFxglTOo2Sm+e8aNhhIRQSEgYFXC1NA8ucqHX5fT508HTpZ5hRicXTb+NlZN8il6Mk1ovhtXdwcJ2gbaYtda8"
    "1df38NrXn5+hv7+ikJDA8ejs5+Lh4eHd3d3d09PT3d3d3d3d4d7epVU0NzQAEnzWtbXPbVJtljQtVFJaZnyXptnFbFRKLRYbHBwc"
    "GysbFhYOKZamY2NSVFpaW32Wpq+mllSmzc/fr5aXrMeWqpjK+N/f5c3N2ZZsz8/ttt/Fes23yMXXtOXO2NjY2Mq1yuf0tlJ92+mN"
    "oLnS6L29ubG46Nqe7MjavAAITlCEJxcYJbwtbIyf8OiPhmBQUFBGTp+YDhmGj+zz9Pn5+eP55aFPg4mJwLyfraWWimqsmIaPoLbT"
    "dnaB07yfj4ZyoY/w/ezovqOij4+h8PDk7OzV2OLy4eHd3d3d09PT3d3T093y3W18VTEAFbamWpW3r6+s11QcKjRaVWltb2/Hz9Wf"
    "XjYtKRkbGykbFRZKr5aLW1JKVFVVWmhaWmNVNErPz9bn7fT6+uSWnp7F9N/WtqbW2ce238/ttt+2bMrN155959bO2NjYzsW9z+Tf"
    "l1vK6buguen8/e/q0amN6emW7/TqtXoAB4oAAoWEhFAWKoqesPb16vCwcoZhTlS2pSouho7VYmJidsHT4/OiJiQkJoXw19fX5L3a"
    "n4yPocuuwTs8fLSOX06Pj6OP6P72oZCSv/Dx5OXm5t7Y5vLy8t3d4d3d3dPT09PT09OkrqRiUlIAFpYpKSlsyLfK5M+LfTEZGVRt"
    "ZmltfJN8ttWYMSoZFg8VG2ismGxaUkpVWlVVWlpaVDYpVK/Nz8/0/ufHzuX69/r0x2hoWy22r7bW1s/tts3FrNWMrZWY383OztjY"
    "2NfQ7OTkaWn3sH+76e3639bP9cig6O+c9vjP168ADZBSVIqfvfv19fjMrZ+PiqWlGjZMho/KYnQ7Gjt0doGeo4mJkvH32Nnk0Lzs"
    "vIyPj8nBdnZ8X4YODl+/n46h/f798O7x9/b09N7h8uHh4eHj8uHh4eHT4dPd3dOu06diW2IAGZwPmRsODmzX5dfNzc22aFI2UlVm"
    "ZmZpfG6TmJdtW1KTzayLY1JaSmhaWlpaWltaMSlo0MvNzdXt5822ts7N1+2vfGxpbXput9bO1tbttsXN1tdubJzZ1s3Ozs7Y2M+8"
    "9tfNbW/ajbvR+tbl3+Xt5Pegvfey1m5baHwADKG8KmiMvb+xajhTw9Tb6efzpQ9UTmGGrnaBdnQ7EBAaKlM17P7k8/n5+ujs7L2G"
    "ho3EgXRmb4iSYYeOSy0WlPj+/f36+vTz5t7y8vLh4eHj4/Lh4fLy4d7h09OuZmJiYgAbpg8PGRV6blR82+Ts28eemKU3KjQ0UlVj"
    "W1VUWsevr6+mbFU0VX2WbmNbWlpaXjQtir6+vrzKzeTavMfGxMbF1TY0k5OXfVuuzs7O2PO2x83J2LbF0c/Ozs7O1seutLf0zZdp"
    "19Kx0N/l7fj47frt97279+22aQAP1y0PaYy96GBOME4uLr3ovM58D15ghIasdnZ2gXZ0OxoVKhUpN2mBdmn09/Dsj46Pn8SBgW9g"
    "RkhGiWFcbCoVGZ79+vPm8uHd3fLi4vLh8vLy8uHy3t7etsppZmJSAB6fFgEBATNsHErb+Pf11M3Nz8qXWzQ0MTdSVFRKVVVUWls2"
    "NG6WbmhKUlpVWko0N829o5K+vMrN17+4vMbExsfON65SNzRuW6XMzs7f+LfIzbbK1dbPzs7Ozs7NtqWmreevfWnknbHR+N/8x6/4"
    "/Of30rjqfGlmAA/YKSpsn7y+hlBIRg4uX1+MjDEWamGEiNelpKSBdnZ2dl5cGw8VGhoaPPjw8L2Oj6GhtoGBjGBISIODhI6MSzZU"
    "LUrl8+bi4uLy4eHh8t7i4vLy8ubT5q5tbXwAIa0bDgEBDhUbVc/HrMX09O3Xza2tppM3NxwqSlJKUlJKNjeLemNSVFJTVFM3NErf"
    "z9Cjkr7Lzs7Xv6G8x7a2ts1Sx1tSN2lbi8bFx9n2yNDPtry1xc3OzsrNzs2nl6217bV9euqxvdHt1qxuaPz85PXSsN9oABE0MTZ8"
    "r+TXvJ9MTlCHUCuOnxsuhoSEiO749Oa2dnZ2aV9OTktSUjYxN7zo7r+Phr3L03R2fJCIiImFhZGh8Or6/vj58+by8vLy8vLy4eLe"
    "5t7z5t5maWZmACO1KRsBAaYcSpPmpGZbt8Pb6+rt2basrMV6bio3NjdSVJaLbGxbU1RUVFI0MWPPz8/Qvr+9z8zM5NqhuLa2trbK"
    "UsdVVFJtVYvFtsfb99K/vb29uMvNzsqutLbKtqWvt+evfZbvoLnR7dWsaFTt/OT36brfaQARKlVpb8q2tLxvLk1hiFA4n60ZW6GI"
    "iZG9387e4a6up6yhYE5OVFRejIy5v72/v7/s19N2dJSPkZGSiYSHjtDX+vr58+bh4eHh8vLT3fLe3t6uZm1mWwAmtZkVDwjHKpfT"
    "d8LCwW1yaoza5+TPxc/Z19nP7VM3Wph9bFNTUlJUVEo2LYvLvb3LzdXPz8zO1t/avbzHtra2ylK2Y1JVaWalysfN3/e/v6Ojor64"
    "tsrKtrSup66uz+Dnt5aW9p2xy/TH55bH9PrZ9+rX55QAEVKu2La2065vDi9IUIiiuK2YGWy5kYhfNlV8rq7y+eXs6I6Pj4Zyjrza"
    "oKDa7Ozs5O3mwXaloaOGX4eJRhUWrPT55vLj3d3d3eHh8vLy03xmZmYAKupLjLKYSnzCPz8/dbySiZBMbJjf2Uoq0eTb0dTUqjMq"
    "GRk2NjZKKi0tts+9vqG/y8rJzM/N1efp2tnMtra2ylKnrqeXYm7ezsrP3/fav6OSo764t8bHtLSurrTW7dutnJiv77G5y+3W7fr4"
    "+M/X29D6+fMAEZSutqWurjcPGE5IUJC526V8NHoABUoZGTGuya6yy72hj6GhvbzLjb7o5OTt5/bYwcGYhpErLY+ERi4tKtjz3uHd"
    "3d3h4eHhtnxpaWYAMVpSesI/Hx90JEJCSKGTpJN6GYDZ5NGym9TP1pZaWioqMTQZWqzWy72jo5K+vM3Y5eW22NbW5+3KrLbKl2lm"
    "zpOTxsnM0L/k7de9opKjv9DMysrHx8Xn5Nuclduzuuf2oKC62e3Y8+3FuuqwsO3i4QARzvOuZnwqGQ4YTmGGodDfWylLAAhLGxYV"
    "W5ycsuTq8OnWztj299vs7+3b0M0ABIhMN6GHAAMbeqfY2M7e3ubTpKR8ZgA0S1V/zHc/H2YhISGDPDp2xHoplcvFt8O64N/fz82v"
    "pq8ZFWjHxsu9o6Ojvr3Vzdjlzq62rra239+2yti22JRVY9bY5dW/v9rfysq4vr7a18zW1tn42ayYepjI7+rv6+m5nbO62+3n5Onr"
    "u53p7d7hABN8VVUPG05Ghr29vM3lNykADC8lFhhTy+vv7N7dAA+ici5OAAOMbG/l2ObdlKWUZgA3U1Vx4NN2do+CQiGDEBAarDZc"
    "m9S6ep6wlVpjeoummK2mvKGhub6RkaO+1c3P1ranp259fa6nx9/Ir87Yzs7Yzs7W1dC/o9rWtsq1vLzXz8/k98p6bmmX8/T09O/q"
    "6e/unaursNvs6tq9sbHb2N7hABQxCBtMAAFGvd/ttq7HNxwAD1AODy4umNgAGdjetvLTZmJVADhTU3rbz524vZKDg5E8MYGTG4CV"
    "uuyqnnpaXmwuKUotLZ7akImJiZGRub3N2MXFrHpjKlp9bXy22duy2M7O2NjYztbX2r+/19a2tra0z/jn9ZZ6eq208/n589jl9O/q"
    "6e+6q7uxsaCgoJ2dy+XzpHwAGYaWk5SUynxSUgAwpGliYmYAOlReiurn1GpYXLi92siKr3oqlbLb6q2yU0pKLg4VXFhTN9qQiYmR"
    "ubi8yra0em43WjfW+PjtfKbf67LWztbOzs7HzdfX6vDWzcrHytbt535rlqa3yPT55vLm4uLz9Pj6+vq6nZ2xoJ2enuflaWlmAJJK"
    "VJfv6dfZz7uyvNT4+vV6Npuy2/WVrV5KNggBAQE4fjfaoZGRocXHx6ZuWlI2evT05/j4+JOlze2y1tbWzc3Nz8/0+NbWzsrN5O2W"
    "em2y0fjn7W7H8+by4+Py8ubz+fT479zSoLG56dZ8b2IAlEpUl+Seeqy07PXvnq2t13qcqrrU9ZOwX1MVAQEBASx+U9C4vb28x696"
    "Sikpbs3s/Pz+/Pz856a09N/Pz8vQ2uz19eTPyM/f+KxoW1qvxfTW2cW0LbTz5uHy8vLy8vLz8/Pz8/jVza5mW2IAlkpVltFKMVRt"
    "ts3t7fTs7WiWm7LR4H+5c0poCAEBASyMN8uwy8/LtZZbGabI7P39/f79/fv8+ral/OfPy8vX8PDPt7fZ19F6aFWYx8/2yM+vrzQ0"
    "Sq754t3h4eHy8vLm5vnzlHxpY2MAmUpaltGtKhkcN1J8rOfn6mh9ltTb24DImFNLSwgBDjOTSrqqyMu6nG4plu33++/puL3Q7vf7"
    "+seXx+/q7Oy8tbfk9s/Xbmh6x6z0z7fIrYotNEqX8/Pm4t3d3eHh8vLyrqSkfACdWmMAA25aaDQxN2N6fc80fZXqsqmW38Webjda"
    "X2toUmy4n7jLsm5oaO37++mwoLGgsdLp9/rZmJ7brbWtz9ft0ZWVnJXP2efH2aaLSjNTVNjl8+bm5uHd0/Ld8tOkrnwAoFpTAAWK"
    "XopKGzEciy16le3Z7Z7ay82wcnJOODZT0aGhuLytbGiV9vvonb3Q6dy9sLDU9uevrevIt8jPi159rZzt/O21zW5SKUpsevTl2Obm"
    "8uLh8uHy4WZiYjw8PACgXlMACHCMVCpvKnqL36y29O/lxZIkIiRFhL7bj5ChuK1paM/32qC90tv39+/as7rr9rem9/ymbmyKrcj8"
    "+siYsmg2HFKYmOfn9NjT3eHy4fLdpVJSMQClb1QAC1qWM4qYtVJpfIvn1IkTAgICC5LXnI64yLVtburQsbG76vv69/b617G4+9GV"
    "rYCVgH3X9cWti5Z9NC1s0ef49M7K09Pd0922rqQ8PACoynoADJ83n7DaWi0qNFhSjolJJCRFouStn7zXi2iW772Ovev27eTt5PT2"
    "ubHv75W1ps36332cnG5TKlRb2dnW5djY3tPT07alpTw0MQC4uFiPAAN6FQ8PGTFSXnKOc9/67fXt6p5erduduL395N/n9O3W9u6x"
    "0Pem1vPz5+WmbDdKaG7n7djOys7d3eHT09OUWwC8u1iPAAReYzEODhUVKTdVl5ZoSjY2nozb0JzQ7+zWyrav3/i667Ha9/zY3t7Y"
    "5fPztrT458rO3dPd093T3XxSUlsAvb1MjQAGVVRLX0sVCAgIDg4bVGiLuvu6n9D3386labTn57Lgsdrbyt3d3d7e3uLe3s7i09PT"
    "ttPd3ZRpbwDA0FONAAtQSEZGRlhVAAKV2/3gsNrv38pbUrbf9cjgsdrkzt3d3d3h3d3y093T09PT06WkUlUAwvh6nQAUldD70rHq"
    "9vS2fGO059+y3LDP9N7h4eHh4d3d3d3d3dO2k21SUgDbrffu0rnb9tblzrbf9M/pucvF3tPh4eHh8vLh3eHhpHxpUgDey/fw0rnc"
    "9+f0/O30t9fpmdDl3t3d3d3h3d3Trml8YgDi6PDSsdLp9N/t7eft9dGb0fPe3d3T093dfGlpAObo3I7S3O/85/b19duq0efm4dPh"
    "06RSYmIA6dq5sdrp7fb19dTLuvjz3q6up2JmAO3SoJ29vrnSs6u7+PPermZiYgDw0qCgoKC5sci1yqRmaWYA872gnbGwyM6UW2IA"
    "/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AP8A/wD/AGY=";
/* u0, v0, u1, v1, largura no mundo, altura no mundo */
static const float CARTEX[4][6] = {
    { 0.000000f, 0.000000f, 0.492188f, 0.343750f, 9.00f, 6.286f },
    { 0.500000f, 0.000000f, 0.992188f, 0.347656f, 6.40f, 4.521f },
    { 0.000000f, 0.500000f, 0.492188f, 0.789062f, 5.00f, 2.937f },
    { 0.500000f, 0.500000f, 0.992188f, 0.875000f, 6.80f, 5.181f }
};

static int tex_ready = 0;

static void draw_car(float x, float z, float ang, int idx) {
    static const unsigned int cc[4] = { 0xff3040a0, 0xff707878, 0xff305870, 0xff404040 };
    float c = cosf(ang), s = sinf(ang);
    box(x, 0.35f, z, 2.2f, 0.9f, 4.2f, ang, cc[idx % 4]);
    box(x - s * 0.2f, 1.25f, z - c * 0.2f, 1.9f, 0.8f, 2.2f, ang, 0xff202830);
    for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) {
        float lx = a * 1.1f, lz = b * 1.3f;
        box(x + lx * c + lz * s, 0, z - lx * s + lz * c, 0.3f, 0.6f, 0.6f, ang, 0xff101010);
    }
}

static void draw_building(const Bld *b) {
    float dz = b->z + b->door * (b->hd + 0.05f);
    box(b->x, 0, b->z, b->hw * 2, b->h, b->hd * 2, 0, b->wall);
    box(b->x, b->h, b->z, b->hw * 2 + 0.5f, 0.6f, b->hd * 2 + 0.5f, 0, b->roof);
    if (b->h > 10.0f) return;
    box(b->x, 0, dz, 1.2f, 2.2f, 0.12f, 0, 0xff203040);
    box(b->x - b->hw * 0.55f, 1.6f, dz, 0.9f, 0.9f, 0.1f, 0, 0xff101418);
    box(b->x + b->hw * 0.55f, 1.6f, dz, 0.9f, 0.9f, 0.1f, 0, 0xff101418);
}

static void draw_world_pois(void) {
    if (NEAR(STN_X, STN_Z, 70)) {
        box(STN_X - 2.0f, 0, STN_Z, 0.6f, 1.6f, 0.6f, 0, 0xff303040);
        box(STN_X + 2.0f, 0, STN_Z, 0.6f, 1.6f, 0.6f, 0, 0xff303040);
        box(STN_X, 3.6f, STN_Z, 6.0f, 0.4f, 4.0f, 0, 0xff2040c0);
        box(STN_X, 0, STN_Z - 3.0f, 5.0f, 2.2f, 3.0f, 0, 0xff8090a0);
    }
    /* roads */
    for (int i = 0; i < NROADS; i++) {
        float x1 = ROADS[i][0], z1 = ROADS[i][1], x2 = ROADS[i][2], z2 = ROADS[i][3];
        float mx = (x1 + x2) * 0.5f, mz = (z1 + z2) * 0.5f;
        float lx = fabsf(x2 - x1), lz = fabsf(z2 - z1);
        if (dist2d(px, pz, mx, mz) > (lx > lz ? lx : lz) * 0.5f + 45.0f) continue;
        if (lx > lz) box(mx, 0.04f, mz, lx, 0.08f, 5.0f, 0, 0xff484c50);
        else box(mx, 0.04f, mz, 5.0f, 0.08f, lz, 0, 0xff484c50);
    }
    /* lago */
    if (NEAR(LAKE_X, LAKE_Z, 70)) box(LAKE_X, 0.02f, LAKE_Z, LAKE_HX * 2, 0.1f, LAKE_HZ * 2, 0, 0xffa06030);
    /* cidade, posto, fazenda, torre, base militar */
    for (int i = 0; i < NB; i++) if (NEAR(BLD[i].x, BLD[i].z, 75)) draw_building(&BLD[i]);
    if (!tex_ready) for (int i = 0; i < NCARS; i++) if (NEAR(carX[i], carZ[i], 60)) draw_car(carX[i], carZ[i], carAng[i], i);
    if (NEAR(-35, -52, 70)) {
        box(-33, 3.2f, -50, 6.0f, 0.4f, 4.0f, 0, 0xff2020b0);
        box(-35.5f, 0, -48.5f, 0.3f, 3.2f, 0.3f, 0, 0xffc0c0c0); box(-30.5f, 0, -48.5f, 0.3f, 3.2f, 0.3f, 0, 0xffc0c0c0);
        box(-34, 0, -50, 0.7f, 1.4f, 0.7f, 0, 0xff2020d0); box(-32, 0, -50, 0.7f, 1.4f, 0.7f, 0, 0xff2020d0);
    }
    if (NEAR(66, 24, 70)) { box(66, 0, 24, 2.5f, 7.0f, 2.5f, 0, 0xffa0a8a8); box(66, 7.0f, 24, 2.8f, 0.8f, 2.8f, 0, 0xff303030); }
    if (NEAR(-65, -15, 80)) {
        box(-65, 16.0f, -15, 4.5f, 0.6f, 4.5f, 0, 0xff404850);
        box(-65, 16.6f, -15, 0.2f, 6.0f, 0.2f, 0, 0xffc0c0c0);
        box(-65, 22.4f, -15, 0.6f, 0.6f, 0.6f, 0, (((int)(gameTime * 2.0f)) & 1) ? 0xff2020ff : 0xff202060);
    }
    if (NEAR(68, 53, 80)) {
        fence_line(54, 39, 82, 39); fence_line(82, 39, 82, 67); fence_line(82, 67, 54, 67); fence_line(54, 67, 54, 39);
        box(66, 0, 60, 4.0f, 2.2f, 3.0f, 0, 0xff486038); box(72, 0, 46, 4.0f, 2.2f, 3.0f, 0, 0xff486038);
        for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) box(80 + a * 0.9f, 0, 42 + b * 0.9f, 0.25f, 6.0f, 0.25f, 0, 0xff505858);
        box(80, 6.0f, 42, 2.8f, 0.4f, 2.8f, 0, 0xff303838);
        box(76, 0.05f, 46, 6.0f, 0.1f, 6.0f, 0, 0xff303030); box(76, 0.12f, 46, 0.5f, 0.05f, 3.0f, 0, 0xffe0e0e0);
        box(76, 0.12f, 46, 2.0f, 0.05f, 0.5f, 0, 0xffe0e0e0);
    }
}

static void draw_base(void) {
    /* casa do acampamento (para dormir) */
    box(CH_X, 0, CH_Z, 4.4f, 2.6f, 4.4f, 0, 0xff405870);
    box(CH_X, 2.6f, CH_Z, 4.9f, 0.5f, 4.9f, 0, 0xff303848);
    box(CH_X, 0, CH_Z + 2.25f, 1.0f, 1.9f, 0.12f, 0, 0xff203040);
    box(CH_X + 1.4f, 1.3f, CH_Z + 2.25f, 0.8f, 0.8f, 0.1f, 0, 0xfff0e0a0);
    float fl = 0.5f + 0.5f * sinf(gameTime * 12.0f);
    box(BASE_X, 0.05f, BASE_Z, 1.4f, 0.3f, 0.3f, 0.6f, 0xff203050); box(BASE_X, 0.05f, BASE_Z, 1.4f, 0.3f, 0.3f, -0.6f, 0xff203050);
    box(BASE_X, 0.2f, BASE_Z, 0.7f, 0.5f + fl * 0.5f, 0.7f, 0, 0xff2080ff);
    box(BASE_X, 0.5f, BASE_Z, 0.4f, 0.4f + fl * 0.4f, 0.4f, 0, 0xff30e0ff);
    box(-5, 0, -4, 3.6f, 1.8f, 3.0f, 0, 0xff486038); box(-5, 1.8f, -4, 3.6f, 0.4f, 1.0f, 0, 0xff384828);
    box(5, 0, -4, 1.8f, 1.0f, 1.0f, 0, 0xff3a6a9a); box(5, 1.0f, -4, 0.8f, 0.5f, 0.6f, 0, 0xff303030);
    box(5.6f, 1.0f, -4, 0.1f, 2.4f, 0.1f, 0, 0xffc0c0c0);
    box(HELI_X, 0.05f, HELI_Z, 7.0f, 0.1f, 7.0f, 0, 0xff404040);
    box(HELI_X, 0.12f, HELI_Z, 0.5f, 0.05f, 3.0f, 0, 0xffe0e0e0); box(HELI_X, 0.12f, HELI_Z, 2.0f, 0.05f, 0.5f, 0, 0xffe0e0e0);
    if (rescue == 0) for (int i = 0; i < 16; i++) {
        float a = i * (PI_F / 8.0f);
        box(BASE_X + cosf(a) * SAFE_R, 0, BASE_Z + sinf(a) * SAFE_R, 0.4f, 0.5f, 0.4f, 0, 0xffa0e0a0);
    }
    if (rescue == 2) {
        float t = 90.0f - rescueT, y = (t < 5.0f) ? (1.0f - t / 5.0f) * 22.0f : 0.0f;
        box(HELI_X, 0.4f + y, HELI_Z, 2.2f, 1.8f, 5.0f, 0, 0xff405030);
        box(HELI_X, 1.1f + y, HELI_Z - 4.5f, 0.6f, 0.6f, 4.0f, 0, 0xff405030);
        box(HELI_X - 1.3f, 0.2f + y, HELI_Z, 0.15f, 0.15f, 4.5f, 0, 0xff202020); box(HELI_X + 1.3f, 0.2f + y, HELI_Z, 0.15f, 0.15f, 4.5f, 0, 0xff202020);
        box(HELI_X, 2.2f + y, HELI_Z, 9.0f, 0.08f, 0.5f, gameTime * 25.0f, 0xff202020);
        box(HELI_X, 2.2f + y, HELI_Z, 9.0f, 0.08f, 0.5f, gameTime * 25.0f + 1.57f, 0xff202020);
    }
}

/* posicao da camera: 3a pessoa atras do jogador, ou 1a pessoa na cabeca */
static void camera_pos(float *ex, float *ey, float *ez, float *cx, float *cy, float *cz) {
    *ex = px - sinf(cam) * 11.0f; *ey = 7.0f; *ez = pz - cosf(cam) * 11.0f;
    *cx = px; *cy = 1.5f; *cz = pz;
    if (firstPerson) {
        *ex = px; *ey = 1.75f; *ez = pz;
        *cx = px + sinf(cam) * 10.0f; *cy = 1.75f; *cz = pz + cosf(cam) * 10.0f;
    }
}

/* projeta o ponto para onde a arma aponta na tela (mesma camera da cena) */
static int aim_screen(float *sx, float *sy) {
    float R = (wAmmo[curW] >= 0) ? 12.0f : 3.0f;
    float ex, ey, ez, cx, cy, cz;
    camera_pos(&ex, &ey, &ez, &cx, &cy, &cz);
    float tx = px + sinf(pang) * R, tz = pz + cosf(pang) * R;
    float ty = firstPerson ? ey : 1.2f;
    float fx = cx - ex, fy = cy - ey, fz = cz - ez, fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float rx = -cosf(cam), ry = 0.0f, rz = sinf(cam);
    float ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
    float dx = tx - ex, dy = ty - ey, dz = tz - ez;
    float zc = dx * fx + dy * fy + dz * fz;
    if (zc < 0.5f) return 0;
    float xc = dx * rx + dy * ry + dz * rz, yc = dx * ux + dy * uy + dz * uz;
    *sx = 240.0f + 189.0f * xc / zc;
    *sy = 136.0f - 194.2f * yc / zc;
    (void)uz;
    return *sx >= 0.0f && *sx < 480.0f && *sy >= 0.0f && *sy < 272.0f;
}

static void draw_fire(void) {
    for (int i = 0; i < 8; i++) {
        float a = i * 0.7854f;
        box(FIRE_X + cosf(a) * 1.1f, 0, FIRE_Z + sinf(a) * 1.1f, 0.4f, 0.3f, 0.4f, 0, 0xff505860);
    }
    box(FIRE_X, 0.2f, FIRE_Z, 1.6f, 0.2f, 0.25f, 0.5f, 0xff203050);
    box(FIRE_X, 0.2f, FIRE_Z, 1.6f, 0.2f, 0.25f, -0.5f, 0xff203050);
    float h = 0.6f + 0.3f * fireLvl;
    for (int i = 0; i < 3; i++) {
        float fl = 0.8f + 0.2f * sinf(gameTime * 9.0f + i * 2.1f);
        box(FIRE_X + (i - 1) * 0.25f, 0.4f, FIRE_Z, 0.35f, h * fl, 0.35f, 0, (i == 1) ? 0xff30e0ff : 0xff2080ff);
    }
}

/* luz: 0..1 perto da fogueira (pelo alcance do nivel) ou com a lanterna ligada */
static float light_boost(void) {
    float bst = 0.0f, df = dist2d(px, pz, FIRE_X, FIRE_Z), fr = fire_radius();
    if (df < fr) bst = 1.0f - df / fr;
    if (lantOn && hasLant && bst < 0.8f) bst = 0.8f;
    return bst;
}


/* ---------- texturas dos carros (atlas 256x256, T8 com paleta, RLE + base64) ---------- */
static unsigned char texPix[256 * 256] __attribute__((aligned(64)));
static unsigned int texClut[256] __attribute__((aligned(64)));
typedef struct { float u, v; unsigned int color; float x, y, z; } TexV;
#define TEX_VTYPE (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int b64_decode(const char *src, unsigned char *dst, int cap) {
    int n = 0; unsigned int acc = 0; int bits = 0;
    for (const char *p = src; *p; p++) {
        int v = b64val(*p);
        if (v < 0) continue;
        acc = (acc << 6) | (unsigned int)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (n < cap) dst[n++] = (unsigned char)((acc >> bits) & 0xff); }
    }
    return n;
}

static void tex_init(void) {
    static unsigned char raw[TEXRAW_LEN];
    int n = b64_decode(TEX_B64, raw, TEXRAW_LEN);
    if (n < 1024) return;
    memcpy(texClut, raw, 1024);
    int o = 1024, p = 0;
    while (o < n && p < 256 * 256) {
        unsigned char b = raw[o++];
        if (b == 0) {
            int cnt = (o < n) ? raw[o++] : 0;
            for (int k = 0; k < cnt && p < 256 * 256; k++) texPix[p++] = 0;
        } else texPix[p++] = b;
    }
    while (p < 256 * 256) texPix[p++] = 0;
    sceKernelDcacheWritebackAll();
    tex_ready = 1;
}

/* carros como sprites texturizados (sempre de frente para a camera) */
static void draw_cars_textured(void) {
    if (!tex_ready) return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0x7f, 0xff);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    sceGuTexImage(0, 256, 256, 256, texPix);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(32, texClut);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    float rx = -cosf(cam), rz = sinf(cam);
    for (int i = 0; i < NCARS; i++) {
        if (!NEAR(carX[i], carZ[i], 60.0f)) continue;
        int t = i % 4;
        float W = CARTEX[t][4] * 0.75f, H = CARTEX[t][5] * 0.75f, hw = W * 0.5f;
        float x0 = carX[i] - rx * hw, z0 = carZ[i] - rz * hw, x1 = carX[i] + rx * hw, z1 = carZ[i] + rz * hw;
        float u0 = CARTEX[t][0], v0 = CARTEX[t][1], u1 = CARTEX[t][2], v1 = CARTEX[t][3];
        TexV *tv = (TexV *)sceGuGetMemory(6 * sizeof(TexV));
        TexV q[6] = {
            { u0, v1, 0xffffffff, x0, 0, z0 }, { u1, v1, 0xffffffff, x1, 0, z1 }, { u1, v0, 0xffffffff, x1, H, z1 },
            { u0, v1, 0xffffffff, x0, 0, z0 }, { u1, v0, 0xffffffff, x1, H, z1 }, { u0, v0, 0xffffffff, x0, H, z0 }
        };
        for (int k = 0; k < 6; k++) tv[k] = q[k];
        sceGumDrawArray(GU_TRIANGLES, TEX_VTYPE, 6, 0, tv);
    }
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_TEXTURE_2D);
}

/* ---------- multijogador: outros jogadores (protocolo pronto; transporte ainda nao ligado) ---------- */
static void draw_remote_players(void) {
    for (int i = 1; i < MP_MAX; i++) {
        NetPlayer *r = &netRemote[i];
        if (!r->used || r->state == 2) continue;
        float d = dist2d(px, pz, r->x, r->z);
        if (d > 60.0f) continue;
        float ph = gameTime * 7.0f + i;
        draw_humanoid(r->x, r->z, r->ang, 1.0f, &HPAL_SURV[i % 3], 0.6f * sinf(ph), 0.2f * sinf(ph), -0.9f, 0, d > 38.0f, 0);
    }
}

static void draw_scene(void) {
    float dl = daylight(), nt = nightness();
    float b = 0.2f + 0.8f * dl;
    b += light_boost() * 0.5f * nt; if (b > 1.1f) b = 1.1f;
    float skyR = 110.0f * (1 - nt) + 12.0f * nt, skyG = 190.0f * (1 - nt) + 18.0f * nt, skyB = 235.0f * (1 - nt) + 40.0f * nt;
    unsigned int sky = 0xff000000 | ((unsigned int)skyB << 16) | ((unsigned int)skyG << 8) | (unsigned int)skyR;
    sceGuClearColor(sky);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(70.0f, 16.0f / 9.0f, 1.0f, 200.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float ex, ey, ez, ccx, ccy, ccz;
    camera_pos(&ex, &ey, &ez, &ccx, &ccy, &ccz);
    ScePspFVector3 eye = { ex, ey, ez };
    ScePspFVector3 ctr = { ccx, ccy, ccz };
    ScePspFVector3 up  = { 0, 1, 0 };
    sceGumLookAt(&eye, &ctr, &up);

    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    ScePspFVector3 dir = { 0.4f, 1.0f, 0.3f };
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &dir);
    unsigned int lc = 0xff000000 | ((unsigned int)(165 * b) << 16) | ((unsigned int)(165 * b) << 8) | (unsigned int)(165 * b);
    sceGuLightColor(0, GU_DIFFUSE, lc);
    sceGuAmbient(0xff000000 | ((unsigned int)(105 * b) << 16) | ((unsigned int)(105 * b) << 8) | (unsigned int)(105 * b));
    sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);

    groundC1 = 0xff2e7a3e; groundC2 = 0xff296f38;
    draw_ground();
    draw_world_pois();
    draw_base();
    if (NEAR(FIRE_X, FIRE_Z, 70)) draw_fire();

    /* caixas de saque */
    for (int i = 0; i < nloot; i++) {
        Loot *L = &loot[i];
        if (L->looted) continue;
        float d = dist2d(px, pz, L->x, L->z);
        if (L->kind == 3) {
            if (d < 95.0f) box(L->x, 0, L->z, 0.5f, 14.0f, 0.5f, 0, 0xff30e0ff);
            if (d < 70.0f) box(L->x, 0, L->z, 1.0f, 0.8f, 1.0f, 0, 0xff30c0ff);
        } else if (d < 40.0f && L->kind != 1) {
            box(L->x, 0, L->z, 0.9f, 0.7f, 0.9f, 0, 0xff3a6a9a);
            box(L->x, 0.9f + 0.15f * sinf(gameTime * 3.0f + i), L->z, 0.35f, 0.35f, 0.35f, 0, L->kind == 2 ? 0xff40e040 : 0xff30e0ff);
        } else if (d < 25.0f && L->kind == 1) {
            box(L->x - 2.2f, 2.0f + 0.15f * sinf(gameTime * 3.0f + i), L->z - 1.0f, 0.35f, 0.35f, 0.35f, 0, 0xff30e0ff);
        }
    }

    /* armas coletaveis no chao */
    for (int i = 0; i < NWP; i++) {
        WPick *wp = &wpick[i];
        if (wp->taken) continue;
        float d = dist2d(px, pz, wp->x, wp->z);
        if (d > 60.0f) continue;
        float by = 0.9f + 0.12f * sinf(gameTime * 2.5f + i);
        draw_weapon(wp->w, wp->x, by, wp->z, gameTime * 1.5f, -1.5708f, 0.0f);
        box(wp->x, 0.0f, wp->z, 0.9f, 0.08f, 0.9f, 0, 0xffe0e040);
        if (d < 45.0f) box(wp->x, 0.0f, wp->z, 0.12f, 6.0f, 0.12f, 0, 0xffe0e040);
    }

    /* arvores */
    for (int i = 0; i < NTREES; i++) {
        float tdx = treeX[i] - px, tdz = treeZ[i] - pz;
        if (tdx * tdx + tdz * tdz > 55.0f * 55.0f) continue;
        box(treeX[i], 0, treeZ[i], 0.7f, treeH[i], 0.7f, 0, 0xff203848);
        if (i % 4 == 0) box(treeX[i] + 0.5f, treeH[i] - 1.2f, treeZ[i], 1.4f, 0.25f, 0.25f, 0.5f, 0xff203848);
        else box(treeX[i], treeH[i] - 0.6f, treeZ[i], 3.0f, 2.6f, 3.0f, 0.4f * i, (i % 3 == 0) ? 0xff1c5a30 : 0xff1a4a28);
    }

    /* efeitos de combate */
    if (swingT > 0) {
        float fx = sinf(pang), fz = cosf(pang);
        box(px + fx * 1.6f, 0.9f, pz + fz * 1.6f, 1.6f, 0.15f, 0.3f, pang + 1.57f + swingT * 8.0f, 0xffe0f0ff);
    }
    if (shotT > 0) {
        float mx = (shotX1 + shotX2) * 0.5f, mz = (shotZ1 + shotZ2) * 0.5f, len = dist2d(shotX1, shotZ1, shotX2, shotZ2);
        box(mx, 1.2f, mz, 0.08f, 0.08f, len, atan2f(shotX2 - shotX1, shotZ2 - shotZ1), 0xff60f0ff);
        box(shotX1, 1.1f, shotZ1, 0.4f, 0.4f, 0.4f, 0, 0xff30e0ff);
    }

    /* sombras */
    box(px, 0.1f, pz, 1.0f, 0.02f, 0.6f, 0, 0xff000000);
    for (int i = 0; i < NZ; i++)
        if (zomb[i].active && zomb[i].state != 2 && NEAR(zomb[i].x, zomb[i].z, 50))
            box(zomb[i].x, 0.1f, zomb[i].z, 1.0f * Z_K[zomb[i].type], 0.02f, 0.7f * Z_K[zomb[i].type], 0, 0xff000000);

    draw_cars_textured();
    draw_remote_players();

    /* zumbis em 3D */
    for (int i = 0; i < NZ; i++) {
        Zomb *z = &zomb[i];
        if (!z->active) continue;
        float d = dist2d(px, pz, z->x, z->z);
        if (d > 60.0f) continue;
        float k = Z_K[z->type];
        if (z->state == 2) { if (z->corpse > 0) draw_corpse(z->x, z->z, z->ang, k, &HPAL_Z[z->type]); continue; }
        float ph = gameTime * (z->state == 1 ? 7.0f : 2.5f) + i * 1.7f;
        float sw = sinf(ph) * (z->state == 1 ? 0.8f : 0.35f);
        float arm = -1.35f + sinf(ph * 0.7f) * 0.15f;
        if (z->atk > 0.6f) arm = -1.95f + sinf(gameTime * 20.0f) * 0.2f;
        draw_humanoid(z->x, z->z, z->ang, k, z->hurt > 0 ? &HPAL_HIT : &HPAL_Z[z->type], sw, arm, arm, 0, d > 38.0f, 0);
    }

    /* sobreviventes em 3D: selvagens com farol ciano, do grupo com marcador verde */
    for (int i = 0; i < NSURV; i++) {
        Surv *sv = &surv[i];
        float d = dist2d(px, pz, sv->x, sv->z);
        if (sv->state == 2) { if (sv->corpse > 0 && d < 60.0f) draw_corpse(sv->x, sv->z, sv->ang, 1.0f, &HPAL_SURV[sv->name % 3]); continue; }
        if (d > 60.0f) continue;
        if (sv->state == 0) {
            if (d < 45.0f) box(sv->x, 0.0f, sv->z, 0.12f, 6.0f, 0.12f, 0, 0xff30e0ff);
            draw_humanoid(sv->x, sv->z, sv->ang, 1.0f, sv->hurt > 0 ? &HPAL_HIT : &HPAL_SURV[sv->name % 3], 0.05f * sinf(gameTime * 2.0f + i), -0.2f, -0.2f, 0, d > 38.0f, 0);
        } else {
            float ph = gameTime * 7.0f + i * 1.3f;
            draw_humanoid(sv->x, sv->z, sv->ang, 1.0f, sv->hurt > 0 ? &HPAL_HIT : &HPAL_SURV[sv->name % 3], 0.7f * sinf(ph), 0.2f * sinf(ph), -1.3f, 4, d > 38.0f, 0);
            box(sv->x, 2.6f, sv->z, 0.25f, 0.25f, 0.25f, 0, 0xff40ff40);
        }
    }

    /* jogador em 3D */
    {
        float sw = moving ? sinf(walkPhase) * 1.0f : 0.0f, aR, aL;
        if (curW >= 4) { aR = -1.45f + (shotT > 0 ? 0.18f : 0.0f); aL = -1.25f; }
        else {
            aR = -0.7f - sw * 0.4f; aL = sw * 0.6f;
            if (swingT > 0) aR = -2.6f + (1.0f - swingT / 0.18f) * 2.8f;
        }
        if (mode == 3) draw_corpse(px, pz, pang, 1.0f, &HPAL_PLAYER);
        else if (!firstPerson && inCar < 0) draw_humanoid(px, pz, pang, rollT > 0 ? 0.8f : 1.0f, hurtT > 0.25f ? &HPAL_HIT : &HPAL_PLAYER, sw, aL, aR, curW, 0, 1);
    }

    /* na tela de titulo: zumbis ao lado, olhando para o personagem */
    if (appState != APP_PLAY && appState != APP_SPLASH && appState != APP_POST) {
        float rx = -cosf(cam), rz = sinf(cam), fx2 = sinf(cam), fz2 = cosf(cam);
        float sw = sinf(appT * 3.0f) * 0.3f;
        float zx = px - rx * 8.0f + fx2 * 1.0f, zz = pz - rz * 8.0f + fz2 * 1.0f;
        draw_humanoid(zx, zz, atan2f(px - zx, pz - zz), 1.0f, &HPAL_Z[0], sw, -1.35f, -1.35f, 0, 0, 0);
        float wx = px + rx * 8.5f + fx2 * 2.0f, wz = pz + rz * 8.5f + fz2 * 2.0f;
        draw_humanoid(wx, wz, atan2f(px - wx, pz - wz), 1.45f, &HPAL_Z[2], -sw, -1.35f, -1.35f, 0, 0, 0);
    }
    if (firstPerson && mode != 3 && inCar < 0) {
        /* arma na mao, vista de primeira pessoa */
        float vx = px + sinf(cam) * 0.9f + cosf(cam) * 0.45f, vz = pz + cosf(cam) * 0.9f - sinf(cam) * 0.45f;
        draw_weapon(curW, vx, 1.35f, vz, cam, (curW >= 4) ? -1.5708f : -1.1f, 0.15f);
    }
    sceGuDisable(GU_LIGHTING);
}

/* ---------- interface ---------- */
static void window(int x0, int y0, int x1, int y1) {
    rect2d(x0, y0, x1, y1, 0xf060c0f0);
    rect2d(x0 + 4, y0 + 4, x1 - 4, y1 - 4, 0xf0201008);
}
static void ui_center(int y, int sc, unsigned int col, const char *str) {
    ui_text((480 - (int)strlen(str) * 6 * sc) / 2, y, sc, col, str);
}
static void bar(int x, int y, int w, int h, float v, unsigned int col) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    rect2d(x - 1, y - 1, x + w + 1, y + h + 1, 0xff000000);
    rect2d(x, y, x + w, y + h, 0xff383838);
    if (v > 0) rect2d(x, y, x + (int)(w * v / 100.0f), y + h, col);
}

static void minimap_dot(float wx, float wz, int sz, unsigned int col, int clampEdge) {
    const int X0 = 408, Y0 = 4, S = 68; const float k = (S / 2.0f) / 60.0f;
    float dx = wx - px, dz = wz - pz;
    float sx = (dx * (-cosf(cam)) + dz * sinf(cam)) * k, sy = (dx * sinf(cam) + dz * cosf(cam)) * k;
    float lim = S / 2.0f - 3.0f;
    if (sx > lim || sx < -lim || sy > lim || sy < -lim) {
        if (!clampEdge) return;
        float m = fmaxf(fabsf(sx), fabsf(sy)); sx = sx / m * lim; sy = sy / m * lim;
    }
    int ix = X0 + S / 2 + (int)sx, iy = Y0 + S / 2 - (int)sy;
    rect2d(ix - sz / 2, iy - sz / 2, ix + sz - sz / 2, iy + sz - sz / 2, col);
}

static void draw_minimap(void) {
    rect2d(406, 2, 478, 74, 0xe0000000);
    rect2d(408, 4, 476, 72, 0xff2a3a2a);
    minimap_dot(BASE_X, BASE_Z, 4, 0xff40d040, 1);
    if (rescue > 0) minimap_dot(HELI_X, HELI_Z, 4, 0xffe08040, 1);
    for (int i = 0; i < nloot; i++) if (loot[i].kind == 3 && !loot[i].looted) minimap_dot(loot[i].x, loot[i].z, 3, 0xff30e0ff, 1);
    for (int i = 0; i < NWP; i++) if (!wpick[i].taken) minimap_dot(wpick[i].x, wpick[i].z, 3, 0xffe040e0, 0);
    for (int i = 0; i < NSURV; i++) if (surv[i].state != 2) minimap_dot(surv[i].x, surv[i].z, 3, surv[i].state == 1 ? 0xff40ff40 : 0xff40e0ff, 1);
    for (int c = 0; c < NCITY; c++) minimap_dot(CITY[c][0], CITY[c][1], 4, cityVis[c] ? 0xff40ff40 : 0xffff8040, 1);
    minimap_dot(STN_X, STN_Z, 3, 0xff40c0ff, 1);
    for (int i = 0; i < NZ; i++) if (zomb[i].active && zomb[i].state != 2) minimap_dot(zomb[i].x, zomb[i].z, 2, zomb[i].state == 1 ? 0xff2020ff : 0xff3030a0, 0);
    rect2d(440, 36, 445, 41, 0xffffffff);
}

static void draw_ui(void) {
    char buf[96];
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    /* barras de status */
    rect2d(0, 0, 204, 84, 0x90000000);
    ui_text(4, 4, 1, 0xffffffff, "VIDA");  bar(44, 4, 150, 7, hp, 0xff3030e0);
    ui_text(4, 16, 1, 0xffffffff, "FOME"); bar(44, 16, 150, 7, hunger, 0xff30a0f0);
    ui_text(4, 28, 1, 0xffffffff, "SEDE"); bar(44, 28, 150, 7, thirst, 0xffe08030);
    ui_text(4, 40, 1, 0xffffffff, "ENER"); bar(44, 40, 150, 7, stam, 0xff40d0a0);
    int hh = (int)hour_now(), mm = (int)((hour_now() - hh) * 60.0f);
    sprintf(buf, "Dia %d  %02d:%02d  Abates:%d", day_number(), hh, mm, kills);
    ui_text(4, 54, 1, 0xffc0ffc0, buf);
    if (rescue == 0) { if (parts < 4) sprintf(buf, "Obj: achar pecas de radio %d/4", parts); else strcpy(buf, "Obj: use o radio no acampamento"); }
    else if (rescue == 1) strcpy(buf, "Obj: sobreviva ate o resgate!");
    else strcpy(buf, "Obj: embarque no helicoptero!");
    ui_text(4, 66, 1, 0xff40e0ff, buf);
    sprintf(buf, "Grupo: %d/%d  Cidades: %d/3", surv_followers(), MAXFOL, cityVis[0] + cityVis[1] + cityVis[2]);
    ui_text(4, 78, 1, 0xffffffff, buf);
    draw_minimap();
    rect2d(330, 76, 476, 100, 0x90000000);
    if (wAmmo[curW] >= 0) sprintf(buf, "%s  %s:%d", wName[curW], itemName[wAmmo[curW]], inv[wAmmo[curW]]); else strcpy(buf, wName[curW]);
    ui_text(334, 80, 1, 0xffffffff, buf);
    if (inCar >= 0) {
        sprintf(buf, "Gasolina: %d%%", (int)carFuel[inCar]);
        rect2d(330, 102, 420, 116, 0x90000000);
        ui_text(334, 104, 1, carFuel[inCar] < 20.0f ? 0xff4040ff : 0xff40e0ff, buf);
    }
    { static const int ci[3] = { I_COM, I_AGUA, I_BAND };
      sprintf(buf, "< %s x%d >  Cima: usar", itemName[ci[selUse]], inv[ci[selUse]]);
      ui_text(334, 90, 1, 0xffc0c0c0, buf); }
    if (rescue == 1 || rescue == 2) {
        rect2d(210, 4, 402, 24, 0xb0000000);
        if (rescue == 1) sprintf(buf, "RESGATE: helicoptero em %ds!", (int)rescueT + 1);
        else sprintf(buf, "HELICOPTERO: embarque em %ds!", (int)rescueT + 1);
        ui_text(214, 10, 1, 0xff40e0ff, buf);
    }

    if (mode == 0) {
        if (showHelp) {
            rect2d(0, 86, 250, 126, 0x90000000);
            ui_text(4, 90, 1, 0xffffffff, "Analog:andar  L/R:camera  X:interagir");
            ui_text(4, 100, 1, 0xffffffff, "Quad:atacar  Circ:rolar  Tri:trocar arma");
            ui_text(4, 110, 1, 0xffffffff, "Esq/Dir:item  Cima:usar  START:menu");
            ui_text(4, 118, 1, 0xff909090, "SELECT: lanterna   START: menu");
        }
        if (curW != 0) {   /* mira: so com arma na mao, no ponto para onde a arma aponta */
            float rsx, rsy;
            if (aim_screen(&rsx, &rsy)) {
                int tg = aim_target(wAmmo[curW] >= 0 ? wRange[curW] : 3.8f, wAmmo[curW] >= 0 ? 1.1f : 3.2f);
                unsigned int cc = (tg >= 0) ? 0xff4040ff : 0xffffffff;
                int X = (int)rsx, Y = (int)rsy;
                rect2d(X - 8, Y, X + 8, Y + 2, cc);
                rect2d(X - 1, Y - 8, X + 1, Y + 8, cc);
            }
        }
        if (msgTimer > 0) {
            int w = (int)strlen(msg) * 12; if (w > 470) w = 470;
            int x = (480 - w) / 2;
            rect2d(x - 6, 186, x + w + 6, 208, 0xa0000000);
            ui_text(x, 190, 2, 0xffffffff, msg);
        }
        const char *pr = NULL; char pb[64];
        if (inCar >= 0) pr = "No carro: analogico dirige, X sai";
        else if (near_heli()) pr = "X: embarcar no helicoptero!";
        else if (dist2d(px, pz, CH_X, CH_Z) < 3.5f) pr = "CASA  X: dormir";
        else if (near_station()) pr = "POSTO  X: abrir";
        else if (near_car_idx() >= 0) pr = "X: entrar no carro";
        else if (near_camp()) pr = "ACAMPAMENTO  X: abrir";
        else if (nearest_wild_surv(4.5f) >= 0) {
            int sv = nearest_wild_surv(4.5f);
            sprintf(pb, "X: recrutar %s (%d comida)", survNames[surv[sv].name], SV_RECRUIT_FOOD); pr = pb;
        }
        else if (nearest_wpick() >= 0) { sprintf(pb, "X: pegar %s", wName[wpick[nearest_wpick()].w]); pr = pb; }
        else {
            int l = nearest_loot();
            if (l >= 0) pr = loot[l].kind == 3 ? "X: pegar peca de radio!" : (loot[l].kind == 1 ? "X: revistar carro" : "X: revistar");
            else if (near_lake()) pr = "X: beber agua";
            else {
                float dd; int zi = nearest_zombie(5.0f, &dd);
                if (zi >= 0) { sprintf(pb, "Zumbi perto! Quad: atacar"); pr = pb; }
            }
        }
        if (pr) {
            int w = (int)strlen(pr) * 12, x = (480 - w) / 2;
            rect2d(x - 6, 238, x + w + 6, 266, 0xa0000000);
            ui_text(x, 245, 2, 0xff40e0ff, pr);
        }
        if (hurtT > 0) rect2d(0, 0, 480, 272, ((unsigned int)(hurtT / 0.4f * 110.0f) << 24) | 0x000000ff);
        if (hp < 25 && ((int)(gameTime * 3.0f) & 1)) rect2d(0, 0, 480, 272, 0x300000ff);
    } else if (mode == 1) {
        static const char *opt[10] = { "Continuar", "", "", "", "", "Loja", "", "Salvar e ir ao menu", "Menu sem salvar", "Sair do jogo" };
        rect2d(0, 0, 480, 272, 0x80000000);
        window(96, 20, 384, 252);
        ui_text(180, 28, 2, 0xff40e0ff, "== MENU ==");
        sprintf(buf, "%s - slot %d - %s", survNames[nameIdx], curSlot + 1, diffNames[difficulty]);
        ui_text(108, 46, 1, 0xffc0c0c0, buf);
        for (int i = 0; i < 10; i++) {
            int y = 58 + i * 15;
            if (i == menuSel) rect2d(106, y - 2, 374, y + 14, 0xc0a06020);
            if (i == 1) sprintf(buf, "%s Musica: %s", i == menuSel ? ">" : " ", musicOn ? "ligada" : "desligada");
            else if (i == 2) sprintf(buf, "%s Visao: %s", i == menuSel ? ">" : " ", firstPerson ? "1a pessoa" : "3a pessoa");
            else if (i == 3) sprintf(buf, "%s Ajuda: %s", i == menuSel ? ">" : " ", showHelp ? "ligada" : "desligada");
            else if (i == 4) sprintf(buf, "%s Camera: %s", i == menuSel ? ">" : " ", camMode == 0 ? "lenta" : (camMode == 1 ? "normal" : "rapida"));
            else if (i == 6) sprintf(buf, "%s Salvar (slot %d)", i == menuSel ? ">" : " ", curSlot + 1);
            else sprintf(buf, "%s %s", i == menuSel ? ">" : " ", opt[i]);
            ui_text(110, y, 2, 0xffffffff, buf);
        }
        ui_text(108, 216, 1, 0xffc0c0c0, "Cima/Baixo + X     START volta");
        if (menuMsg[0]) ui_text(110, 230, 2, 0xff60ff60, menuMsg);
    } else if (mode == 2) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 24, 444, 254);
        ui_text(150, 30, 2, 0xff40e0ff, "== ACAMPAMENTO ==");
        for (int i = 0; i < 8; i++) {
            int y = 50 + i * 18;
            if (i == menuSel) rect2d(46, y - 3, 434, y + 15, 0xc0a06020);
            if (i == 0) strcpy(buf, "Bandagem (2 Pano)");
            else if (i == 1) strcpy(buf, "Municao x6 (2 Sucata)");
            else if (i == 2) strcpy(buf, "Cartuchos x4 (3 Sucata)");
            else if (i == 3) strcpy(buf, "Balas de rifle x3 (4 Sucata)");
            else if (i == 4) strcpy(buf, "Dormir ate amanhecer");
            else if (i == 5) sprintf(buf, "Radio: chamar resgate (%d/4)", parts);
            else if (i == 6) sprintf(buf, "Melhorar fogueira (nv %d)", fireLvl);
            else strcpy(buf, "Sair");
            ui_text(56, y, 2, 0xffffffff, buf);
        }
        sprintf(buf, "Madeira:%d Sucata:%d Pano:%d Bandagem:%d Pecas:%d/4", inv[I_MAD], inv[I_SUC], inv[I_PANO], inv[I_BAND], parts);
        ui_text(46, 204, 1, 0xffc0ffc0, buf);
        sprintf(buf, "Municao:%d Cartuchos:%d Balas rifle:%d Comida:%d Agua:%d", inv[I_MUN], inv[I_CART], inv[I_RIFLE], inv[I_COM], inv[I_AGUA]);
        ui_text(46, 214, 1, 0xffc0ffc0, buf);
        ui_text(46, 224, 1, 0xffc0c0c0, "Cima/Baixo + X escolhe   START/O sai");
        if (menuMsg[0]) ui_text(46, 235, 2, 0xff60ff60, menuMsg);
    } else if (mode == 8) {
        static const char *sopt[3] = { "Comprar 3 gasolina (6 moedas)", "Abastecer carro no posto", "Sair" };
        rect2d(0, 0, 480, 272, 0x80000000);
        window(60, 50, 420, 232);
        ui_text(150, 58, 2, 0xff40e0ff, "== POSTO ==");
        sprintf(buf, "Moedas: %d   Gasolina: %d", coins, inv[I_GAS]);
        ui_text(76, 82, 2, 0xfff0e040, buf);
        for (int i = 0; i < 3; i++) {
            int y = 110 + i * 24;
            if (i == stnSel) rect2d(76, y - 3, 404, y + 17, 0xc0a06020);
            ui_text(86, y, 2, 0xffffffff, sopt[i]);
        }
        if (menuMsg[0]) ui_text(76, 186, 2, 0xff60ff60, menuMsg);
        ui_text(76, 216, 1, 0xffc0c0c0, "Cima/Baixo + X     START/O sai");
    } else if (mode == 7) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 24, 444, 254);
        ui_text(170, 30, 2, 0xff40e0ff, "== LOJA ==");
        sprintf(buf, "Moedas: %d", coins);
        ui_text(56, 50, 2, 0xfff0e040, buf);
        for (int i = 0; i < NSHOP + 2; i++) {
            int y = 74 + i * 15;
            if (i == shopSel) rect2d(46, y - 2, 434, y + 12, 0xc0a06020);
            if (i < NSHOP) {
                ui_text(56, y, 1, 0xffffffff, shopName[i]);
                sprintf(buf, "%d moedas", shopCost[i]); ui_text(330, y, 1, 0xff40e0ff, buf);
            } else if (i == NSHOP) ui_text(56, y, 1, 0xff909090, "Multijogador (em breve)");
            else ui_text(56, y, 1, 0xffffffff, "Voltar");
        }
        ui_text(46, 216, 1, 0xffc0c0c0, "Cima/Baixo + X comprar    O/START voltar");
        if (shopMsg[0]) ui_text(46, 230, 1, 0xff60ff60, shopMsg);
    } else if (mode == 3) {
        rect2d(0, 0, 480, 272, 0xc0000020);
        ui_center(80, 4, 0xff2020e0, "VOCE MORREU");
        sprintf(buf, "Zumbis derrubados: %d    Mortes: %d", kills, deaths);
        ui_center(130, 1, 0xffc0c0c0, buf);
        if (deathT > 1.2f) {
            ui_center(160, 2, 0xffffffff, "X: renascer no acampamento");
            ui_center(190, 1, 0xffc0c0c0, "Voce perde metade dos itens ao renascer.");
        }
    }

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
}

/* ---------- telas: abertura, titulo, novo jogo, carregar, creditos ---------- */
typedef struct { const char *t; int sc; unsigned int col; } CLine;
static const CLine CREDITS[] = {
    { "SOBREVIVENDO EM GS", 3, 0xff40e0ff },
    { "", 1, 0 },
    { "Um jogo desenvolvido por", 2, 0xffffffff },
    { "MECANIC ENGINE GAMER", 3, 0xfff0e040 },
    { "", 1, 0 },
    { "Direcao de jogo", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Arte e personagens", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Musica e efeitos sonoros", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Programacao", 1, 0xffc0c0c0 },
    { "MECANIC ENGINE GAMER", 2, 0xffffffff },
    { "", 1, 0 },
    { "Sobreviventes", 1, 0xffc0c0c0 },
    { "Rafa  Bia  Leo  Ana  Dudu  Nina  Caio  Lia", 1, 0xffffffff },
    { "", 1, 0 },
    { "Feito para PSP e PPSSPP", 2, 0xffffffff },
    { "", 1, 0 },
    { "", 1, 0 },
    { "Obrigado por jogar!", 3, 0xff40e0ff }
};
#define NCRED ((int)(sizeof(CREDITS) / sizeof(CREDITS[0])))
static int cred_h(const CLine *l) { return l->sc * 8 + (l->sc >= 2 ? 10 : 6); }
static int credits_height(void) { int h = 0; for (int i = 0; i < NCRED; i++) h += cred_h(&CREDITS[i]); return h; }

static void draw_heli_art(int x, int y, int blink) {
    rect2d(x, y + 6, x + 60, y + 26, 0xff405030);
    rect2d(x + 40, y + 9, x + 56, y + 18, 0xffe0c080);
    rect2d(x - 36, y + 10, x, y + 16, 0xff405030);
    rect2d(x - 40, y + 2, x - 34, y + 20, 0xff304020);
    rect2d(x - 14, y, x + 74, y + 3, 0xff202020);
    rect2d(x + 28, y + 3, x + 32, y + 6, 0xff202020);
    rect2d(x + 4, y + 28, x + 56, y + 31, 0xff202020);
    if (blink) rect2d(x + 26, y - 4, x + 32, y, 0xff2020ff);
}

static void draw_app_ui(void) {
    char buf[96];
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    if (appState == APP_SPLASH) {
        ui_center(108, 3, 0xfff0e040, "MECANIC ENGINE GAMER");
        ui_center(148, 2, 0xffffffff, "apresenta");
        float a = (appT < 0.8f) ? 1.0f - appT / 0.8f : (appT > 2.2f ? (appT - 2.2f) / 0.8f : 0.0f);
        if (a > 1.0f) a = 1.0f;
        if (a > 0) rect2d(0, 0, 480, 272, ((unsigned int)(a * 255.0f) << 24));
    } else if (appState == APP_TITLE) {
        static const char *to[4] = { "Novo jogo", "Carregar jogo", "Creditos", "Sair" };
        rect2d(0, 0, 480, 272, 0x70000000);
        window(66, 6, 414, 124);
        ui_center(14, 4, 0xff2020e0, "SOBREVIVENDO");
        ui_center(50, 6, 0xff40e0ff, "EM GS");
        ui_center(104, 2, 0xfff0e040, "MECANIC ENGINE GAMER");
        window(130, 130, 350, 240);
        for (int i = 0; i < 4; i++) {
            int y = 140 + i * 24;
            if (i == titleSel) rect2d(138, y - 3, 342, y + 17, 0xc0a06020);
            sprintf(buf, "%s %s", i == titleSel ? ">" : " ", to[i]);
            ui_text(146, y, 2, 0xffffffff, buf);
        }
        ui_center(254, 1, 0xffc0c0c0, "(c) MECANIC ENGINE GAMER - Sobrevivendo Em GS");
    } else if (appState == APP_NEW) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 20, 444, 252);
        ui_center(28, 2, 0xff40e0ff, "== NOVO JOGO ==");
        for (int i = 0; i < 6; i++) {
            int y = 58 + i * 24;
            if (i == nwSel) rect2d(46, y - 3, 434, y + 17, 0xc0a06020);
            unsigned int col = (i == 4) ? 0xff60ff60 : 0xffffffff;
            if (i == 0) sprintf(buf, "Slot: %d %s", nwSlot + 1, slotInfos[nwSlot].used ? "(sobrescrever!)" : "(vazio)");
            else if (i == 1) sprintf(buf, "Sobrevivente: %s", survNames[nwName]);
            else if (i == 2) { if (nwSeed) sprintf(buf, "Semente do mundo: %d", nwSeed); else strcpy(buf, "Semente do mundo: aleatoria"); }
            else if (i == 3) sprintf(buf, "Dificuldade: %s", diffNames[nwDiff]);
            else if (i == 4) strcpy(buf, "COMECAR A SOBREVIVER");
            else strcpy(buf, "Voltar");
            ui_text(56, y, 2, col, buf);
        }
        ui_text(46, 216, 1, 0xffc0c0c0, "Cima/Baixo escolhe   Esq/Dir muda   X confirma   O volta");
        ui_text(46, 228, 1, 0xffc0c0c0, "A semente muda a posicao dos zumbis, arvores e saques.");
    } else if (appState == APP_LOAD) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 24, 444, 254);
        ui_center(32, 2, 0xff40e0ff, "== CARREGAR JOGO ==");
        for (int i = 0; i < 3; i++) {
            int y = 62 + i * 50;
            if (i == slotSel) rect2d(46, y - 4, 434, y + 36, 0xc0a06020);
            if (slotInfos[i].used) {
                sprintf(buf, "Slot %d: %s", i + 1, survNames[slotInfos[i].nameIdx]);
                ui_text(56, y, 2, 0xffffffff, buf);
                sprintf(buf, "Dia %d   Pecas de radio: %d/4   Abates: %d", slotInfos[i].day, slotInfos[i].parts, slotInfos[i].kills);
                ui_text(56, y + 20, 1, 0xffc0ffc0, buf);
            } else { sprintf(buf, "Slot %d: (vazio)", i + 1); ui_text(56, y, 2, 0xff909090, buf); }
        }
        if (menuMsg[0]) ui_text(46, 214, 2, 0xff60ff60, menuMsg);
        ui_text(46, 238, 1, 0xffc0c0c0, "X: carregar     Quadrado: apagar     O: voltar");
    } else if (appState == APP_CREDITS) {
        rect2d(0, 0, 480, 272, 0xd8000000);
        float y = creditsY;
        for (int i = 0; i < NCRED; i++) {
            if (CREDITS[i].t[0] && y > -40 && y < 280) ui_center((int)y, CREDITS[i].sc, CREDITS[i].col, CREDITS[i].t);
            y += cred_h(&CREDITS[i]);
        }
        rect2d(0, 254, 480, 272, 0xe0000000);
        ui_center(260, 1, 0xff909090, "START: pular    X: acelerar");
    } else if (appState == APP_POST) {
        float t = appT;
        for (int i = 0; i < 8; i++) rect2d(0, i * 34, 480, i * 34 + 34, 0xff000000 | ((20 + i * 3) << 16) | ((10 + i) << 8) | 8);
        if (t < 3.0f) ui_center(120, 2, 0xffffffff, "Algumas semanas depois...");
        else if (t < 8.0f) {
            ui_center(40, 2, 0xff40e0ff, "[RADIO] Aqui e a Central...");
            ui_center(70, 1, 0xffffffff, "Captamos um novo sinal de socorro vindo de uma ilha distante.");
            for (int i = 0; i < 24; i++) { int h = 6 + (int)(24.0f * fabsf(sinf(appT * 6.0f + i * 0.7f))); rect2d(120 + i * 10, 150 - h, 126 + i * 10, 150 + h, 0xff40e0ff); }
            ui_center(200, 1, 0xffc0c0c0, "...repetindo: ha sobreviventes. Preciso de voluntarios.");
        } else if (t < 12.0f) {
            ui_center(40, 2, 0xffffffff, "O helicoptero decola de novo...");
            draw_heli_art(-60 + (int)((t - 8.0f) * 140.0f), 130, ((int)(t * 4.0f)) & 1);
        } else if (t < 14.0f) {
            ui_center(120, 2, 0xffffffff, "A sobrevivencia continua...");
        } else {
            ui_center(60, 3, 0xff40e0ff, "SOBREVIVENDO EM GS 2");
            ui_center(110, 4, 0xfff0e040, "EM BREVE");
            ui_center(170, 2, 0xffffffff, "MECANIC ENGINE GAMER");
            if (((int)(appT * 2.0f)) & 1) ui_center(230, 2, 0xffc0c0c0, creditsFromGame ? "X: voltar ao jogo" : "X: voltar ao menu");
        }
        ui_center(260, 1, 0xff707070, "START: pular");
    }

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
}

static void app_update(float dt, unsigned int pressed, unsigned int held) {
    appT += dt;
    if (appState != APP_SPLASH && appState != APP_POST) cam += dt * 0.12f;
    if (appState == APP_SPLASH) {
        if (appT > 3.0f || (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS))) { appState = APP_TITLE; titleSel = 0; appT = 0; }
    } else if (appState == APP_TITLE) {
        if (pressed & PSP_CTRL_UP)   { titleSel = (titleSel + 3) % 4; play_sfx(5); }
        if (pressed & PSP_CTRL_DOWN) { titleSel = (titleSel + 1) % 4; play_sfx(5); }
        if (pressed & PSP_CTRL_CROSS) {
            play_sfx(5);
            if (titleSel == 0) {
                refresh_slots(); nwSel = 0; nwName = 0; nwSeed = 0; nwDiff = 1; nwSlot = 0;
                for (int i = 2; i >= 0; i--) if (!slotInfos[i].used) nwSlot = i;
                menuMsg[0] = 0; appState = APP_NEW;
            } else if (titleSel == 1) { refresh_slots(); slotSel = 0; delConfirm = -1; menuMsg[0] = 0; appState = APP_LOAD; }
            else if (titleSel == 2) { creditsFromGame = 0; creditsY = 272.0f; appT = 0; appState = APP_CREDITS; }
            else running = 0;
        }
    } else if (appState == APP_NEW) {
        if (pressed & PSP_CTRL_UP)   { nwSel = (nwSel + 5) % 6; play_sfx(5); }
        if (pressed & PSP_CTRL_DOWN) { nwSel = (nwSel + 1) % 6; play_sfx(5); }
        int d = 0;
        if (pressed & PSP_CTRL_LEFT) d = -1;
        if (pressed & PSP_CTRL_RIGHT) d = 1;
        if (d) {
            play_sfx(5);
            if (nwSel == 0) nwSlot = (nwSlot + 3 + d) % 3;
            else if (nwSel == 1) nwName = (nwName + 8 + d) % 8;
            else if (nwSel == 2) { nwSeed += d; if (nwSeed < 0) nwSeed = 99; if (nwSeed > 99) nwSeed = 0; }
            else if (nwSel == 3) nwDiff = (nwDiff + 3 + d) % 3;
        }
        if ((pressed & PSP_CTRL_LTRIGGER) && nwSel == 2) nwSeed = (nwSeed + 90) % 100;
        if ((pressed & PSP_CTRL_RTRIGGER) && nwSel == 2) nwSeed = (nwSeed + 10) % 100;
        if (pressed & PSP_CTRL_CIRCLE) appState = APP_TITLE;
        if (pressed & PSP_CTRL_CROSS) {
            if (nwSel == 4) { create_world(); appState = APP_PLAY; mode = 0; say("Sobreviva! Ache 4 pecas de radio."); play_sfx(4); }
            else if (nwSel == 5) appState = APP_TITLE;
        }
    } else if (appState == APP_LOAD) {
        if (pressed & PSP_CTRL_UP)   { slotSel = (slotSel + 2) % 3; delConfirm = -1; menuMsg[0] = 0; play_sfx(5); }
        if (pressed & PSP_CTRL_DOWN) { slotSel = (slotSel + 1) % 3; delConfirm = -1; menuMsg[0] = 0; play_sfx(5); }
        if (pressed & PSP_CTRL_CIRCLE) appState = APP_TITLE;
        if (pressed & PSP_CTRL_SQUARE) {
            if (!slotInfos[slotSel].used) { strcpy(menuMsg, "Slot vazio."); play_sfx(10); }
            else if (delConfirm == slotSel) {
                char pth[96]; slot_path(slotSel, pth); sceIoRemove(pth);
                refresh_slots(); delConfirm = -1; strcpy(menuMsg, "Slot apagado."); play_sfx(4);
            } else { delConfirm = slotSel; strcpy(menuMsg, "Quadrado de novo para apagar!"); play_sfx(10); }
        }
        if (pressed & PSP_CTRL_CROSS) {
            if (!slotInfos[slotSel].used) { strcpy(menuMsg, "Slot vazio."); play_sfx(10); }
            else {
                curSlot = slotSel;
                if (load_game()) { appState = APP_PLAY; mode = 0; say("Jogo carregado!"); play_sfx(4); }
                else { strcpy(menuMsg, "Erro ao carregar!"); play_sfx(10); }
            }
        }
    } else if (appState == APP_CREDITS) {
        float sp = 28.0f + ((held & PSP_CTRL_CROSS) ? 70.0f : 0.0f);
        creditsY -= sp * dt;
        if (creditsY + credits_height() < 0 || (pressed & PSP_CTRL_START)) { appState = APP_POST; appT = 0; }
        else if ((pressed & PSP_CTRL_CIRCLE) && !creditsFromGame) appState = APP_TITLE;
    } else if (appState == APP_POST) {
        if ((pressed & PSP_CTRL_START) || (appT > 14.0f && (pressed & PSP_CTRL_CROSS))) {
            if (creditsFromGame) { appState = APP_PLAY; mode = 0; say("Voce escapou! Continue sobrevivendo."); }
            else { appState = APP_TITLE; titleSel = 0; }
        }
    }
}

int main(void) {
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    build_cube();
    build_ground();
    tex_init();
    reset_state();
    seed_rng = seed_for(0);
    init_world();

    int at = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
    if (at >= 0) sceKernelStartThread(at, 0, NULL);

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FRAME, BUF_W);
    sceGuDepthBuffer((void *)(FRAME * 2), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    SceCtrlData pad; memset(&pad, 0, sizeof(pad));
    unsigned int oldButtons = 0;
    unsigned int last = sceKernelGetSystemTimeLow();

    while (running) {
        unsigned int now = sceKernelGetSystemTimeLow();
        float dt = (now - last) / 1000000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;

        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldButtons;
        oldButtons = pad.Buttons;

        if (appState != APP_PLAY) app_update(dt, pressed, pad.Buttons);
        else if (mode == 1) pause_update(pressed);
        else if (mode == 2) camp_update(pressed);
        else if (mode == 7) coin_shop_update(pressed);
        else if (mode == 8) station_update(pressed);
        else if (mode == 3) death_update(dt, pressed);
        else if (inCar >= 0) car_update(dt, &pad, pressed);
        else update(dt, &pad, pressed);

        if (appState == APP_PLAY) mp_update(dt);
        if (appState == APP_PLAY && mode != 3) city_check();
        songId = (appState == APP_PLAY) ? ((chasers > 0) ? 3 : (nightness() > 0.5f ? 2 : 1))
                                        : ((appState == APP_CREDITS || appState == APP_POST) ? 4 : 0);

        sceGuStart(GU_DIRECT, list);
        if (appState == APP_SPLASH || appState == APP_POST) {
            sceGuClearColor(0xff000000);
            sceGuClearDepth(0);
            sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);
        } else draw_scene();
        if (appState == APP_PLAY) draw_ui(); else draw_app_ui();
        sceGuFinish();
        sceGuSync(0, 0);

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
