/* SPDX-License-Identifier: MIT */
/*
 * rsxkit demo: minimal program using the RSX layer.
 *
 *  - rsxkit_init / frame_begin / frame_end: video setup and double-buffered frames.
 *  - A texture generated on the CPU, kept in main memory mapped to the RSX
 *    (rsxkit_map_main), drawn on a quad that spins in perspective with
 *    rsxkit_draw_triangles; a vertex-coloured triangle; an alpha-blended quad.
 *  - Square toggles an offscreen render target (rsxkit_target_*) that is
 *    drawn at 640x360 and presented with nearest filtering (chunky pixels).
 *  - Text on screen with rsxkit_ui_*.
 *
 * Cross or Circle (or the XMB exit) quits. If the file
 * /dev_hdd0/rsxkit-demo.auto exists, the demo checks a few pixels of the
 * frame, prints "RESULT rsxkit-demo: OK" or "FAIL" to the TTY and exits by
 * itself after a few seconds.
 */
#include <io/pad.h>
#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/process.h>
#include <sysutil/sysutil.h>

#include <rsxkit/rsxkit.h>
#include <rsxkit/ui.h>

SYS_PROCESS_PARAM(1001, 0x100000);

#define LW 1280.0f
#define LH 720.0f
#define TEX_W 128
#define TEX_H 128
#define CLEAR 0x102040u
#define MAIN_BYTES (2u << 20)
#define VB_VERTS 64
#define OFF_W 640
#define OFF_H 360

static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *user)
{
	(void)param, (void)user;
	if (status == SYSUTIL_EXIT_GAME)
		running = 0;
}

static void pads(int *quit, int *square)
{
	padInfo info;
	padData d;
	*quit = *square = 0;
	if (ioPadGetInfo(&info) != 0)
		return;
	for (int i = 0; i < MAX_PADS; i++)
		if (info.status[i] && ioPadGetData(i, &d) == 0) {
			if (d.BTN_CROSS || d.BTN_CIRCLE)
				*quit = 1;
			if (d.BTN_SQUARE)
				*square = 1;
		}
}

/* Perspective projection of a point rotated around the Y axis. */
static void project(rsxkit_vertex *v, float x, float y, float ang, float cx, float cy, float u, float w, uint32_t rgba)
{
	float c = cosf(ang), s = sinf(ang);
	float X = x * c, Z = 900.0f + x * s;           /* camera at z = 0 */
	float inv = 900.0f / Z;
	float px = cx + X * inv, py = cy + y * inv;
	float cw = Z / 900.0f;                          /* clip w > 0 in front */
	v->pos[0] = px * cw; v->pos[1] = py * cw; v->pos[2] = 0.5f * cw; v->pos[3] = cw;
	v->st[0] = u; v->st[1] = w;
	v->rgba[0] = rgba >> 24; v->rgba[1] = rgba >> 16; v->rgba[2] = rgba >> 8; v->rgba[3] = rgba;
	v->pad = 0;
}

static void flat(rsxkit_vertex *v, float x, float y, float s, float t, uint32_t rgba)
{
	v->pos[0] = x; v->pos[1] = y; v->pos[2] = 0.5f; v->pos[3] = 1.0f;
	v->st[0] = s; v->st[1] = t;
	v->rgba[0] = rgba >> 24; v->rgba[1] = rgba >> 16; v->rgba[2] = rgba >> 8; v->rgba[3] = rgba;
	v->pad = 0;
}

static uint32_t px_at(const uint32_t *fb, uint32_t pitch, uint32_t x, uint32_t y)
{
	return fb[(size_t)y * (pitch / 4) + x] & 0x00FFFFFFu;
}

int main(void)
{
	ioPadInit(7);
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);

	FILE *af = fopen("/dev_hdd0/rsxkit-demo.auto", "r");
	int autotest = af != NULL;
	if (af)
		fclose(af);

	int rc = rsxkit_init(NULL);
	if (rc != RSXKIT_OK) {
		printf("rsxkit_init failed: %d\nRESULT rsxkit-demo: FAIL\n", rc);
		return 1;
	}
	rc = rsxkit_ui_init();
	const rsxkit_screen *scr = rsxkit_screen_info();
	printf("rsxkit demo: screen %ux%u, ui init %d\n", (unsigned)scr->width, (unsigned)scr->height, rc);

	/* Main memory visible to the RSX: [texture][vertex buffer]. */
	uint8_t *mem = memalign(1u << 20, MAIN_BYTES);
	uint32_t off = 0;
	if (!mem || rsxkit_map_main(mem, MAIN_BYTES, &off) != RSXKIT_OK) {
		printf("rsxkit_map_main failed\nRESULT rsxkit-demo: FAIL\n");
		return 1;
	}
	uint32_t *tex = (uint32_t *)mem;                       /* RGBA8888, pitch TEX_W*4 */
	for (int y = 0; y < TEX_H; y++)
		for (int x = 0; x < TEX_W; x++) {
			int check = ((x >> 4) ^ (y >> 4)) & 1;
			uint32_t r = check ? 255 : 230, g = check ? 160 : 60 + y, b = check ? 30 : 255 - x;
			tex[y * TEX_W + x] = r << 24 | g << 16 | b << 8 | 255u;
		}
	rsxkit_vertex *vb = (rsxkit_vertex *)(mem + (1u << 20));
	const uint32_t vb_off = off + (1u << 20);

	rsxkit_draw textured, vcolor, blended;
	memset(&textured, 0, sizeof textured);
	textured.ntex = 1;
	textured.blend = RSXKIT_BLEND_OFF;
	textured.cull = RSXKIT_CULL_NONE;
	textured.tex[0] = (rsxkit_texture){ .offset = off, .pitch = TEX_W * 4, .width = TEX_W, .height = TEX_H,
					    .location = RSXKIT_MEM_MAIN, .format = RSXKIT_RGBA8888,
					    .wrap_s = RSXKIT_WRAP_REPEAT, .wrap_t = RSXKIT_WRAP_REPEAT,
					    .filter = RSXKIT_FILTER_LINEAR,
					    .xf = { 1.0f / TEX_W, 1.0f / TEX_H, 0.0f, 0.0f } };
	for (int c = 0; c < 4; c++)
		textured.k[RSXKIT_K_T0][1][c] = 1.0f;          /* colour = T0 * vertex colour */
	blended = textured;
	blended.blend = RSXKIT_BLEND_ALPHA;
	memset(&vcolor, 0, sizeof vcolor);
	vcolor.blend = RSXKIT_BLEND_OFF;
	vcolor.cull = RSXKIT_CULL_NONE;
	for (int c = 0; c < 4; c++)
		vcolor.k[RSXKIT_K_ONE][1][c] = 1.0f;           /* colour = vertex colour */

	rsxkit_target *tgt = rsxkit_target_create(OFF_W, OFF_H);
	if (!tgt)
		printf("offscreen target not available\n");
	const rsxkit_view view = { RSXKIT_SCALE_FIT, RSXKIT_FILTER_NEAREST, 16.0f / 9.0f };

	int offscreen = 0, last_square = 0, checks_ok = 1, checked_direct = 0, checked_off = 0;
	for (uint32_t frame = 0; running; frame++) {
		sysUtilCheckCallback();
		int quit, square;
		pads(&quit, &square);
		if (quit)
			break;
		if (square && !last_square && tgt)
			offscreen ^= 1;
		last_square = square;
		if (autotest && tgt && frame == 40)
			offscreen = 1;

		float ang = frame * 0.02f;
		rsxkit_vertex *v = vb;
		const uint32_t quad_u[6][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1} };
		const float qx[2] = { -1, 1 };
		for (int i = 0; i < 6; i++) {                   /* spinning textured quad */
			float x = qx[quad_u[i][0]], y = qx[quad_u[i][1]];
			project(v++, x * 200.0f, y * 200.0f, ang, 640.0f, 360.0f, quad_u[i][0] * 2.0f, quad_u[i][1] * 2.0f, 0xFFFFFFFFu);
		}
		flat(v++, 140, 620, 0, 0, 0xFF0000FFu);         /* triangle with vertex colours */
		flat(v++, 340, 620, 0, 0, 0x00FF00FFu);
		flat(v++, 240, 460, 0, 0, 0x0000FFFFu);
		for (int i = 0; i < 6; i++) {                   /* half-transparent quad, right side */
			float x = qx[quad_u[i][0]], y = qx[quad_u[i][1]];
			flat(v++, 1000 + x * 110.0f, 360 + y * 110.0f, quad_u[i][0] * 128.0f, quad_u[i][1] * 128.0f, 0xFFFFFF90u);
		}
		__sync_synchronize();

		rsxkit_frame_begin(CLEAR);
		if (offscreen) {
			rsxkit_bind(tgt, NULL, LW, LH);
			rsxkit_clear(RSXKIT_CLEAR_COLOR, CLEAR, 0.0f, NULL);
		} else {
			rsxkit_bind_frame(LW, LH);
		}
		rsxkit_vertices(vb_off, RSXKIT_MEM_MAIN);
		textured.scissor[2] = vcolor.scissor[2] = blended.scissor[2] = LW;
		textured.scissor[3] = vcolor.scissor[3] = blended.scissor[3] = LH;
		rsxkit_draw_triangles(&textured, 0, 6);
		rsxkit_draw_triangles(&vcolor, 6, 3);
		rsxkit_draw_triangles(&blended, 9, 6);
		if (offscreen)
			rsxkit_target_present(tgt, NULL, OFF_W, OFF_H, &view, NULL);

		if (autotest && ((frame == 30 && !offscreen) || (frame == 60 && offscreen))) {
			rsxkit_finish();
			uint32_t pitch;
			const uint32_t *bb = rsxkit_back_buffer(&pitch);
			uint32_t w = scr->width, h = scr->height;
			uint32_t corner = px_at(bb, pitch, 2, 2);
			uint32_t centre = px_at(bb, pitch, w / 2, h / 2);
			uint32_t tri = px_at(bb, pitch, w * 240 / 1280, h * 580 / 720);
			printf("%s: corner %06x centre %06x triangle %06x\n", offscreen ? "offscreen" : "direct",
			       (unsigned)corner, (unsigned)centre, (unsigned)tri);
			int ok = corner == CLEAR && centre != CLEAR && tri != CLEAR;
			checks_ok &= ok;
			if (offscreen)
				checked_off = 1;
			else
				checked_direct = 1;
		}

		rsxkit_ui_begin(LW, LH);
		rsxkit_ui_rect(20, 20, 560, 120, 0x00000090u);
		rsxkit_ui_text(36, 30, "rsxkit demo v0.1.0", 2.0f, 0xFFFFFFFFu, 0);
		rsxkit_ui_text(36, 72, offscreen ? "Square: offscreen 640x360 target (ON)"
						 : "Square: offscreen 640x360 target (off)", 1.5f, 0xE0E0FFFFu, 0);
		rsxkit_ui_text(36, 100, "Cross / Circle: exit", 1.5f, 0xE0E0FFFFu, 0);
		char buf[48];
		snprintf(buf, sizeof buf, "frame %u", (unsigned)frame);
		rsxkit_ui_text(1100, 30, buf, 1.5f, 0xFFFFFFFFu, 0);
		rsxkit_ui_end();
		rsxkit_frame_end();

		if (autotest && frame >= 90)
			break;
	}

	int ok = checks_ok && (!autotest || (checked_direct && (checked_off || !tgt)));
	if (autotest)
		printf("RESULT rsxkit-demo: %s\n", ok ? "OK" : "FAIL");
	rsxkit_finish();
	ioPadEnd();
	rsxkit_shutdown();
	return ok ? 0 : 1;
}
