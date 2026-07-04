// VR sky dome for Star Fox 64. SF64's sky is a screen-space starfield
// + scrolling backdrop, scrolled by the game camera - which can't be world-anchored on PITCH in VR (a 2D
// remap only fakes yaw). This rebuilds it as a real 3D sphere at infinity: a vertex-colored gradient dome
// plus, in space levels, a scattering of 3D star quads. It is drawn by the VR layer with a rotation-only
// view-projection that combines the GAME camera orientation (so it sweeps as the ship turns) with the head
// orientation (so it stays put as you look around) - anchored on both yaw and pitch, unlike the flat sky.
//
// The flat starfield / backdrop are skipped game-side (fox_display.c) while this is active, so the dome
// replaces them. gVRSkyDome toggles it.
#include "global.h" // brings M_PI + the math functions (cosf/sinf/sqrtf/asinf), same as the rest of the engine

extern s32 CVarGetInteger(const char* name, s32 defaultValue);
extern f32 CVarGetFloat(const char* name, f32 defaultValue);
extern s32 gStarCount;
extern s32 gFogRed;
extern s32 gFogGreen;
extern s32 gFogBlue;

// Cached dome DL + the frame it was built on (the VR engine loop rebuilds it once per game frame because
// the gradient colours follow the level's fog). Read by the interpreter's dome pass.
Gfx* gVrSkyDomeGfx = NULL;
unsigned int gVrSkyDomeFrame = 0xFFFFFFFFu;

#define DOME_AZ 16    // azimuth segments (around)
#define DOME_BANDS 10 // elevation bands from zenith to nadir
#define DOME_QUADS (DOME_AZ * DOME_BANDS)
#define STAR_MAX 320  // 3D stars scattered on the dome in space levels
#define CLOUD_MAX 220 // soft cloud puffs banded around the horizon in planet levels

static Gfx sDomeGfx[DOME_QUADS * 2 + STAR_MAX * 2 + CLOUD_MAX * 2 + 80];
static Vtx sDomeVtx[DOME_QUADS * 4 + STAR_MAX * 4 + CLOUD_MAX * 4];

// Procedural soft round blob for the cloud puffs (I8, radial alpha falloff). Textured onto each puff so
// they have feathered edges instead of the hard, smeared rectangles that flat vertex-colour quads give.
#define CLOUD_TEX_DIM 32
static u8 sCloudTex[CLOUD_TEX_DIM * CLOUD_TEX_DIM];
static void build_cloud_tex(void) {
    s32 y, x;
    for (y = 0; y < CLOUD_TEX_DIM; y++) {
        for (x = 0; x < CLOUD_TEX_DIM; x++) {
            f32 dx = (x + 0.5f) - CLOUD_TEX_DIM * 0.5f;
            f32 dy = (y + 0.5f) - CLOUD_TEX_DIM * 0.5f;
            f32 r = sqrtf(dx * dx + dy * dy) / (CLOUD_TEX_DIM * 0.5f); // 0 centre .. 1 edge
            f32 t = 1.0f - r;
            if (t < 0.0f) t = 0.0f;
            t = t * t * (3.0f - 2.0f * t); // smoothstep -> soft feathered edge
            sCloudTex[y * CLOUD_TEX_DIM + x] = (u8) (t * 255.0f);
        }
    }
}

static void set_vtx(Vtx* v, f32 x, f32 y, f32 z, u8 r, u8 g, u8 b) {
    v->v.ob[0] = (s16) x;
    v->v.ob[1] = (s16) y;
    v->v.ob[2] = (s16) z;
    v->v.flag = 0;
    v->v.tc[0] = 0;
    v->v.tc[1] = 0;
    v->v.cn[0] = r;
    v->v.cn[1] = g;
    v->v.cn[2] = b;
    v->v.cn[3] = 255;
}

static void set_vtx_a(Vtx* v, f32 x, f32 y, f32 z, u8 r, u8 g, u8 b, u8 a) {
    set_vtx(v, x, y, z, r, g, b);
    v->v.cn[3] = a;
}

// A billboard quad centred at world direction d (unit), radius Rq, half-size (su, sv) in the tangent
// plane, with vertex colour+alpha. Returns the 4 verts written; advances the caller's index.
static void billboard_quad(Vtx* v, f32 dx, f32 dy, f32 dz, f32 Rq, f32 su, f32 sv, u8 r, u8 g, u8 b, u8 a) {
    f32 t1x, t1y, t1z, t2x, t2y, t2z, len, cx, cy, cz;
    if (dy > 0.99f || dy < -0.99f) {
        t1x = 1.0f; t1y = 0.0f; t1z = 0.0f;
    } else {
        t1x = -dz; t1y = 0.0f; t1z = dx; // cross(up=(0,1,0), d), horizontal
        len = sqrtf(t1x * t1x + t1z * t1z);
        t1x /= len; t1z /= len;
    }
    t2x = dy * t1z - dz * t1y; // cross(d, t1)
    t2y = dz * t1x - dx * t1z;
    t2z = dx * t1y - dy * t1x;
    cx = Rq * dx; cy = Rq * dy; cz = Rq * dz;
    set_vtx_a(&v[0], cx - su * t1x - sv * t2x, cy - su * t1y - sv * t2y, cz - su * t1z - sv * t2z, r, g, b, a);
    set_vtx_a(&v[1], cx + su * t1x - sv * t2x, cy + su * t1y - sv * t2y, cz + su * t1z - sv * t2z, r, g, b, a);
    set_vtx_a(&v[2], cx + su * t1x + sv * t2x, cy + su * t1y + sv * t2y, cz + su * t1z + sv * t2z, r, g, b, a);
    set_vtx_a(&v[3], cx - su * t1x + sv * t2x, cy - su * t1y + sv * t2y, cz - su * t1z + sv * t2z, r, g, b, a);
}

// Deterministic PRNG (xorshift) so the starfield is identical every frame - no shimmer.
static u32 sRng;
static f32 rng01(void) {
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return (f32) (sRng & 0xFFFFFF) / (f32) 0x1000000;
}

Gfx* build_sky_dome_vr(void) {
    s32 b, a, i;
    u8 zr, zg, zb, hr, hg, hb; // zenith + horizon colours
    const f32 R = 1000.0f;     // dome radius (game units); the rotation-only sky VP puts it at infinity
    Gfx* g = sDomeGfx;
    Vtx* vtx = sDomeVtx;
    s32 vi = 0;
    s32 space = (gStarCount != 0);
    // Live tuning knobs (read every frame; the dome rebuilds each frame) so the sky/clouds can be dialled
    // in the headset without a rebuild. gVRSkyBright scales the gradient brightness, gVRCloudAlpha the cloud
    // opacity, gVRCloudCover the cloud density (fraction of the puffs drawn).
    f32 skyBright = CVarGetFloat("gVRSkyBright", 1.0f);
    f32 cloudAlpha = CVarGetFloat("gVRCloudAlpha", 1.0f);
    f32 cloudCover = CVarGetFloat("gVRCloudCover", 1.0f);
    if (skyBright < 0.2f) skyBright = 0.2f;
    if (skyBright > 2.0f) skyBright = 2.0f;
    if (cloudAlpha < 0.0f) cloudAlpha = 0.0f;
    if (cloudAlpha > 3.0f) cloudAlpha = 3.0f;
    if (cloudCover < 0.0f) cloudCover = 0.0f;
    if (cloudCover > 1.0f) cloudCover = 1.0f;

    // Gradient endpoints. SPACE (starfield) levels: near-black so the 3D stars read on black. PLANET levels
    // (Corneria etc.): the level's fog/haze colour at the horizon fading to a deeper sky toward the zenith -
    // the dome then sits BEHIND the game's cloud panorama (which is still drawn) as a world-anchored sky,
    // instead of being "encapsulated in fog" with the clouds gone.
    if (space) {
        hr = 0;
        hg = 0;
        hb = 6; // whisper of deep blue at the horizon
        zr = 0;
        zg = 0;
        zb = 0; // pure black zenith
    } else {
        // A VIVID sky, only lightly tinted (25%) by the level's fog colour - using the fog colour straight
        // made a flat desaturated grey-blue that read as "in fog". Deep saturated blue at the zenith fading
        // to a lighter haze toward the horizon, so it looks like a real sky with depth, per level.
        f32 fr = (f32) (gFogRed < 0 ? 0 : (gFogRed > 255 ? 255 : gFogRed));
        f32 fg = (f32) (gFogGreen < 0 ? 0 : (gFogGreen > 255 ? 255 : gFogGreen));
        f32 fb = (f32) (gFogBlue < 0 ? 0 : (gFogBlue > 255 ? 255 : gFogBlue));
        zr = (u8) (0.75f * 40.0f + 0.25f * fr * 0.5f);   // deep sky blue zenith
        zg = (u8) (0.75f * 92.0f + 0.25f * fg * 0.5f);
        zb = (u8) (0.75f * 188.0f + 0.25f * fb * 0.6f);
        hr = (u8) (0.75f * 176.0f + 0.25f * fr);         // light hazy horizon
        hg = (u8) (0.75f * 200.0f + 0.25f * fg);
        hb = (u8) (0.75f * 228.0f + 0.25f * fb);
    }
    if (skyBright != 1.0f) { // live brightness knob
        f32 c6[6];
        u8* p6[6];
        s32 ci;
        c6[0] = zr; c6[1] = zg; c6[2] = zb; c6[3] = hr; c6[4] = hg; c6[5] = hb;
        p6[0] = &zr; p6[1] = &zg; p6[2] = &zb; p6[3] = &hr; p6[4] = &hg; p6[5] = &hb;
        for (ci = 0; ci < 6; ci++) {
            f32 v = c6[ci] * skyBright;
            *p6[ci] = (u8) (v > 255.0f ? 255.0f : v);
        }
    }

    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetTextureLUT(g++, G_TT_NONE);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE); // output = vertex colour, opaque
    gDPSetRenderMode(g++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG | G_TEXTURE_GEN);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);

    // --- gradient sphere ---
    for (b = 0; b < DOME_BANDS; b++) {
        f32 el0 = (90.0f - (f32) b * (180.0f / DOME_BANDS)) * (f32) (M_PI / 180.0);
        f32 el1 = (90.0f - (f32) (b + 1) * (180.0f / DOME_BANDS)) * (f32) (M_PI / 180.0);
        u8 cTop[3], cBot[3];
        s32 p;
        for (p = 0; p < 2; p++) {
            f32 eDeg = (p == 0 ? el0 : el1) * (f32) (180.0 / M_PI);
            u8* o = (p == 0) ? cTop : cBot;
            f32 t = 1.0f - (eDeg + 90.0f) / 180.0f; // 0 at zenith .. 1 at nadir
            o[0] = (u8) (zr + (hr - zr) * t);
            o[1] = (u8) (zg + (hg - zg) * t);
            o[2] = (u8) (zb + (hb - zb) * t);
        }
        for (a = 0; a < DOME_AZ; a++) {
            f32 az0 = (f32) a / DOME_AZ * 2.0f * (f32) M_PI;
            f32 az1 = (f32) (a + 1) / DOME_AZ * 2.0f * (f32) M_PI;
            Vtx* v = &vtx[vi];
            set_vtx(&v[0], R * cosf(el0) * sinf(az0), R * sinf(el0), -R * cosf(el0) * cosf(az0), cTop[0], cTop[1], cTop[2]);
            set_vtx(&v[1], R * cosf(el1) * sinf(az0), R * sinf(el1), -R * cosf(el1) * cosf(az0), cBot[0], cBot[1], cBot[2]);
            set_vtx(&v[2], R * cosf(el1) * sinf(az1), R * sinf(el1), -R * cosf(el1) * cosf(az1), cBot[0], cBot[1], cBot[2]);
            set_vtx(&v[3], R * cosf(el0) * sinf(az1), R * sinf(el0), -R * cosf(el0) * cosf(az1), cTop[0], cTop[1], cTop[2]);
            gSPVertex(g++, (uintptr_t) v, 4, 0);
            gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
            vi += 4;
        }
    }

    // --- 3D stars (space levels only): small white quads at fixed world directions on a slightly inner
    // sphere, so they ride the dome's rotation and read as a real, look-aroundable starfield. ---
    if (space) {
        f32 Rs = R * 0.98f;
        sRng = 0x1234567u; // fixed seed -> stable field
        for (i = 0; i < STAR_MAX; i++) {
            f32 elev = asinf(2.0f * rng01() - 1.0f); // uniform on the sphere
            f32 azim = rng01() * 2.0f * (f32) M_PI;
            f32 ce = cosf(elev);
            f32 dx = ce * sinf(azim), dy = sinf(elev), dz = -ce * cosf(azim);
            f32 sz = 3.0f + rng01() * 3.0f;
            u8 c = (u8) (200 + (s32) (rng01() * 55.0f));
            billboard_quad(&vtx[vi], dx, dy, dz, Rs, sz, sz, c, c, c, 255);
            gSPVertex(g++, (uintptr_t) &vtx[vi], 4, 0);
            gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
            vi += 4;
        }
    }

    // --- clouds (planet levels): soft round puffs TEXTURED with a radial-falloff blob, clustered into a
    // broken band around the horizon. The texture gives feathered edges (no hard/smeared rectangles). They
    // ride the dome's rotation, so the whole sky is one world-anchored 360deg layer. ---
    if (!space && cloudCover > 0.01f && cloudAlpha > 0.01f) {
        s32 clumps = 16;
        s32 per = (s32) ((CLOUD_MAX / clumps) * cloudCover); // density knob
        s32 c, k;
        f32 Rc = R * 0.96f;
        if (per < 1) per = 1;
        build_cloud_tex();
        gDPPipeSync(g++);
        gDPSetCycleType(g++, G_CYC_1CYCLE);
        gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetRenderMode(g++, G_RM_XLU_SURF, G_RM_XLU_SURF2); // alpha blend, no depth
        // colour = white (shade), alpha = blob texel * shade alpha -> soft-edged white puffs
        gDPSetCombineLERP(g++, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0);
        gDPLoadTextureBlock(g++, sCloudTex, G_IM_FMT_I, G_IM_SIZ_8b, CLOUD_TEX_DIM, CLOUD_TEX_DIM, 0,
                            G_TX_CLAMP, G_TX_CLAMP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
        sRng = 0x0C10D5u; // fixed seed -> stable clouds
        for (c = 0; c < clumps; c++) {
            f32 clumpAz = ((f32) c / clumps + (rng01() - 0.5f) * 0.06f) * 2.0f * (f32) M_PI;
            f32 clumpEl = (1.0f + rng01() * 9.0f) * (f32) (M_PI / 180.0); // just above the horizon
            for (k = 0; k < per && vi + 4 <= (DOME_QUADS + STAR_MAX + CLOUD_MAX) * 4; k++) {
                Vtx* v = &vtx[vi];
                f32 az = clumpAz + (rng01() - 0.5f) * 0.20f;
                f32 el = clumpEl + (rng01() - 0.5f) * 0.09f;
                f32 ce = cosf(el);
                f32 dx = ce * sinf(az), dy = sinf(el), dz = -ce * cosf(az);
                f32 sz = 26.0f + rng01() * 34.0f;             // squarer puffs (texture makes them round)
                f32 su = sz * (1.1f + rng01() * 0.6f);        // a touch wider than tall
                f32 sv = sz;
                f32 af = (110.0f + rng01() * 90.0f) * cloudAlpha; // texture feathers the edges, so puffs can be solid
                u8 a = (u8) (af > 255.0f ? 255.0f : af);
                billboard_quad(v, dx, dy, dz, Rc, su, sv, 255, 255, 255, a);
                v[0].v.tc[0] = 0;              v[0].v.tc[1] = 0;
                v[1].v.tc[0] = CLOUD_TEX_DIM << 5; v[1].v.tc[1] = 0;
                v[2].v.tc[0] = CLOUD_TEX_DIM << 5; v[2].v.tc[1] = CLOUD_TEX_DIM << 5;
                v[3].v.tc[0] = 0;              v[3].v.tc[1] = CLOUD_TEX_DIM << 5;
                gSPVertex(g++, (uintptr_t) v, 4, 0);
                gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
                vi += 4;
            }
        }
    }

    gSPEndDisplayList(g);
    gVrSkyDomeGfx = sDomeGfx;
    return sDomeGfx;
}

extern bool vr_is_active(void); // src/port/vr/vr.h - true when the OpenXR session is rendering

// True when the world-anchored dome should provide the sky: VR is live and the CVar is on. (Independent of
// gVrSkyDomeGfx so the game can decide to draw the dome BEFORE it's built the first time.) The renderer asks
// per background triangle, so the CVar lookup is cached for the frame instead of hitting the map every call.
bool vr_sky_dome_active(void) {
    static u32 sCachedFrame = 0xFFFFFFFF;
    static bool sCachedOn = true;
    if (gSysFrameCount != sCachedFrame) {
        sCachedFrame = gSysFrameCount;
        sCachedOn = CVarGetInteger("gVRSkyDome", 1) != 0;
    }
    // Inside Andross' arena the scrolling backdrop IS the level - the boss floats in it. Replacing it
    // with the generic dome leaves the head hanging in empty space, so that stage keeps its own sky.
    if (gCurrentLevel == LEVEL_VENOM_ANDROSS) {
        return false;
    }
    return vr_is_active() && sCachedOn;
}

// Draw the sky dome as REAL WORLD GEOMETRY through the game's own camera, centred on the camera eye. This
// is the key fix: instead of a separate hand-built sky matrix (which kept coming out cross-eyed / stuck to
// the face / at the wrong depth), the dome now rides the exact same modelview + eye projection as the ship
// and terrain - so it inherits their correct depth, stereo and world-anchoring for free. Called from the
// play render right AFTER the camera lookAt is set, before the world objects. Centring on the camera eye
// puts it at infinity (no parallax as you fly) while the lookAt rotates it with the ship and the VR eye
// projection rotates it with the head. VR-only; no-op flat.
void Vr_DrawSkyDome(f32 ex, f32 ey, f32 ez) {
    if (!vr_sky_dome_active()) {
        return;
    }
    build_sky_dome_vr();
    if (gVrSkyDomeGfx == NULL) {
        return;
    }
    Matrix_Push(&gGfxMatrix);
    Matrix_Translate(gGfxMatrix, ex, ey, ez, MTXF_APPLY); // centre on the camera eye
    Matrix_Scale(gGfxMatrix, 8.0f, 8.0f, 8.0f, MTXF_APPLY); // authored at R=1000 -> R=8000, well inside far clip
    Matrix_SetGfxMtx(&gMasterDisp);
    gSPDisplayList(gMasterDisp++, gVrSkyDomeGfx);
    Matrix_Pop(&gGfxMatrix);
    // The dome DL cleared z-buffer / lighting / cull / fog and (for the clouds) turned texturing on; restore
    // a sane world baseline so the ship and terrain that draw next behave (each also runs its own RCP setup).
    gDPPipeSync(gMasterDisp++);
    gSPTexture(gMasterDisp++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gSPSetGeometryMode(gMasterDisp++, G_ZBUFFER | G_LIGHTING | G_CULL_BACK | G_FOG | G_SHADE | G_SHADING_SMOOTH);
    gDPSetRenderMode(gMasterDisp++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
}
