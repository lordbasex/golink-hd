/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * go-link HD's 3D: low-poly scenes drawn in software, with integers only,
 * like the rest of the engine, so every build draws the same pixels (see
 * docs/3d.md). The 2D engine never calls it; only 3D games do.
 *
 * Units: positions are 16.16 meters (FX_ONE is one meter), y points up;
 * angles are the engine's (4096 a turn, gfx.h); light and colors' strengths
 * are 256 for 1.0.
 *
 * A game builds its world once: textures (tex3d.c makes them in code),
 * pieces (boxes, cylinders, balls, quads) with a material, lamps, and then
 * r3d_bake() lights it: the sun's soft shadows, the sky's light that does
 * not reach corners, and the lamps, worked out on a grid of small cubes
 * (voxels) and kept in each corner of each triangle. Every frame only reads
 * it. What moves (a character, a cat) is drawn as an instance: a mesh at a
 * position and a turn, lit by the sun and the sky as it goes.
 */
#ifndef HD_R3D_H
#define HD_R3D_H

#include <stdint.h>
#include "gfx.h"

#define R3D_MAX_TRIS 196608
#define R3D_MAX_DRAWN 65536 /* triangles on screen in one frame, after clipping */
#define R3D_MAX_LM_FACES 16384 /* faces with a light map */
#define R3D_LM_TEXELS (1 << 21) /* light map texels (5 cm each): about 5,000 square meters */
#define R3D_MAX_TEX 96
#define R3D_MAX_LIGHTS 96
#define R3D_MAX_SOLIDS 24576

typedef struct
{
   int32_t x, y, z;
} r3d_vec;

/* A built piece of the world: its first triangle and how many. */
typedef struct
{
   int32_t first, count;
} r3d_mesh;

/* ---- textures ---- */

/* A picture of 2^wlog x 2^hlog pixels (0xAARRGGBB, kept by the caller), repeated across faces; its id. */
int32_t r3d_tex_add(int32_t wlog, int32_t hlog, uint32_t *px);
/* Its pixels, to change them while playing (a screen that shows code). */
uint32_t *r3d_tex_pixels(int32_t id);

/* ---- building (once, before playing) ---- */

void r3d_clear(void);
/* Where the next pieces go: a place and a turn around y (0..4095), until the next r3d_place. */
void r3d_place(int32_t x, int32_t y, int32_t z, int32_t yaw);
/* Starts and ends a mesh: the triangles added in between. */
void r3d_begin(void);
r3d_mesh r3d_end(void);

/*
 * The material of the next pieces: a texture (-1 none) laid `texels_per_m`
 * texels a meter along the piece's own axes, or stretched once over each
 * face with 0 (a poster, a screen). The piece's color multiplies it.
 */
void r3d_material(int32_t tex, int32_t texels_per_m);
/* The next pieces glow by themselves (screens, bulbs): 0 off, else their strength (256 is their color). */
void r3d_glow(int32_t strength);
/* The next pieces stop light and cast shadows (1, the default) or not (0: glass, thin things). */
void r3d_occlude(int32_t on);
/* The next pieces are see-through (glass): 0 off, else how much they cover, 1..255; they are drawn after everything else. */
void r3d_glass(int32_t cover);
/* The next pieces darken what is under them instead of covering it (a shadow, a stain); their texture's alpha is how much. */
void r3d_decal(int32_t on);
/* Faces bigger than `cell` (16.16 meters) are cut into cells so the baked light can show shadows on them; 0 never. */
void r3d_detail(int32_t cell);

/* A box from (x0, y0, z0) to (x1, y1, z1), in the place's own axes; flags leave faces out (R3D_SKIP_*). */
enum { R3D_SKIP_BOTTOM = 1, R3D_SKIP_TOP = 2, R3D_SKIP_X = 4, R3D_INSIDE = 8, R3D_SKIP_Z = 16, R3D_SKIP_SIDES = R3D_SKIP_X | R3D_SKIP_Z };
/* Only the faces toward -z and +z: a picture, a screen, a window pane. */
#define R3D_PANE (R3D_SKIP_BOTTOM | R3D_SKIP_TOP | R3D_SKIP_X)
/* Only the top face: a floor, a rug. */
#define R3D_TOP_ONLY (R3D_SKIP_BOTTOM | R3D_SKIP_SIDES)
void r3d_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t color, int32_t flags);
/* A cylinder standing on (x, y, z): radius r at the bottom, r_top at the top (0: a cone), height h, `sides` faces (3..32). */
void r3d_cylinder(int32_t x, int32_t y, int32_t z, int32_t r, int32_t r_top, int32_t h, int32_t sides, uint32_t color);
/* A low-poly ball (a leaf, a bulb, a head): center, radii, `rings` rows (2..12). */
void r3d_ball(int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz, int32_t rings, uint32_t color);
/* A flat quad, one side: the side where (b - a) x (c - a) points. */
void r3d_quad(r3d_vec a, r3d_vec b, r3d_vec c, r3d_vec d, uint32_t color);

/* A lamp: where, how far its light reaches, its color and strength (256 is a desk lamp's). */
void r3d_light(int32_t x, int32_t y, int32_t z, int32_t radius, uint32_t color, int32_t strength);

/* The light of the world, for r3d_bake and for what moves. */
typedef struct
{
   r3d_vec sun;            /* toward the sun, any length */
   uint32_t sun_color;     /* 0xRRGGBB */
   int32_t sun_strength;   /* 256 = 1.0 */
   int32_t sun_soft;       /* how wide the sun is, 0..256: softer shadows */
   uint32_t sky_color;
   int32_t sky_strength;
   uint32_t ground_color;  /* light bounced from below */
   int32_t ground_strength;
   int32_t ao_reach;       /* how far a corner looks for walls, 16.16 meters */
   r3d_vec lo, hi;         /* where shadows are worked out (the grid's box); hi.x <= lo.x: around everything */
} r3d_light_cfg;

/* Lights every triangle built so far; call it once, after building. */
void r3d_bake(const r3d_light_cfg *c);
/*
 * After r3d_bake, the baked region's solids (the voxel grid) for a game's
 * rules: whether a point is inside one (outside the region: never), and how
 * far a straight line from `from` toward `to` goes before one, 0..65536 of
 * its length (a follow camera pulled in front of walls).
 */
int32_t r3d_solid_at(r3d_vec p);
int32_t r3d_reach(r3d_vec from, r3d_vec to);
int32_t r3d_triangles(void);

/* ---- drawing (every frame) ---- */

typedef struct
{
   r3d_vec eye;            /* 16.16 meters */
   int32_t yaw, pitch;     /* 0..4095: yaw 0 looks toward +z, pitch > 0 looks down */
   int32_t fov;            /* the vertical field of view, 0..4095 (e.g. 512 is 45 degrees) */
   int32_t near_z;         /* 16.16 meters */
   uint32_t sky_top, sky_bottom; /* 0xRRGGBB behind everything, top to bottom */
   int32_t exposure;       /* 256 = as baked */
   int32_t smooth;         /* 1 softens the edges (anti-aliasing) */
   const r3d_light_cfg *light; /* for what moves */
} r3d_view;

/* What moves this frame: a mesh at a place and a turn. */
typedef struct
{
   r3d_mesh mesh;
   r3d_vec at;
   int32_t yaw;
   uint32_t tint; /* 0 as built, else 0xAARRGGBB mixed toward RGB by A */
   int32_t rot[9]; /* a whole turn (r3d_rotation), rows of 2.14; all zero: only the yaw */
} r3d_instance;

/* A turn as a matrix: yaw around y, then pitch around x (the top toward +z... the bottom toward -z), then roll around z. */
void r3d_rotation(int32_t yaw, int32_t pitch, int32_t roll, int32_t out[9]);
/* a then b: out = a x b. */
void r3d_rot_mul(const int32_t a[9], const int32_t b[9], int32_t out[9]);
r3d_vec r3d_rot_apply(const int32_t m[9], r3d_vec v);

/* The world (every triangle in `scene`) and the instances, seen from the view, into the surface. */
void r3d_draw(hd_surface *s, const r3d_view *v, const r3d_mesh *scene, const r3d_instance *inst, int32_t count);
/* How many triangles the last r3d_draw put on screen. */
int32_t r3d_drawn(void);
/* Where a point of the world lands on screen (16.16 meters in, pixels out); 0 when it is behind the eye. */
int r3d_project(const hd_surface *s, const r3d_view *v, r3d_vec p, int32_t *x, int32_t *y);

/* ---- textures made in code (tex3d.c) ---- */

enum
{
   TEX3D_PARQUET = 0, /* oak boards in a herringbone */
   TEX3D_PLANKS,      /* long boards */
   TEX3D_TILES,       /* plain square tiles with grout */
   TEX3D_PATTERN,     /* patterned cement tiles */
   TEX3D_SUBWAY,      /* small wall tiles */
   TEX3D_PLASTER,     /* a painted wall's soft grain */
   TEX3D_FABRIC,      /* woven cloth */
   TEX3D_KNIT,        /* a blanket, a jumper */
   TEX3D_RUG,         /* a rug with a border */
   TEX3D_RUG2,        /* a striped kilim */
   TEX3D_GRASS,
   TEX3D_CONCRETE,
   TEX3D_BRICK,
   TEX3D_FACADE,      /* a city building's windows */
   TEX3D_WOOD,        /* furniture grain */
   TEX3D_MARBLE,
   TEX3D_METAL,       /* brushed */
   TEX3D_LEAVES,
   TEX3D_CORK,
   TEX3D_ROUND_SHADOW, /* a soft round shadow, for r3d_decal */
   TEX3D_COUNT
};
/* Makes every texture above once (deterministic noise) and returns their ids, by TEX3D_*. */
const int32_t *tex3d_build(void);
/* A picture painted in code for a poster or a frame: `kind` picks one of several, `seed` varies it. */
int32_t tex3d_poster(int32_t kind, uint32_t seed);

#endif
