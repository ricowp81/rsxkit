/* SPDX-License-Identifier: MIT */
/*
 * rsxkit — el 2D de rsxkit/ui.h en el RSX: memoria mapeada con el atlas y
 * dos búferes de vértices, y el lote dibujado sobre la pantalla del cuadro
 * con el 3D de draw3d.c (textura × color del vértice, mezcla alfa). La
 * geometría la arma ui_geom.c.
 */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "rsxkit/ui.h"

#define UI_BYTES     (2u << 20)
#define ATLAS_PITCH  (RSXKIT_UI_ATLAS_W * 4)
#define ATLAS_BYTES  ((ATLAS_PITCH * RSXKIT_UI_ATLAS_H + 0xFFFFu) & ~0xFFFFu)
#define VB_CAP       (RSXKIT_UI_MAX_QUADS * 6)
#define VB_BYTES     (VB_CAP * (uint32_t)sizeof(rsxkit_vertex))

_Static_assert(ATLAS_BYTES + 2 * VB_BYTES <= UI_BYTES, "la memoria del 2D no alcanza");
_Static_assert(ATLAS_PITCH % 64 == 0, "pitch de textura múltiplo de 64");

static struct {
	int ready, active, half;
	uint8_t *mem;
	uint32_t off;
	rsxkit_vertex *vb[2];
	uint32_t vb_off[2];
	uint32_t fence[2];
	rsxkit_draw draw;
} ui;

int rsxkit_ui_init(void)
{
	if (ui.ready)
		return RSXKIT_OK;
	ui.mem = memalign(1u << 20, UI_BYTES);
	if (!ui.mem)
		return RSXKIT_ENOMEM;
	int rc = rsxkit_map_main(ui.mem, UI_BYTES, &ui.off);
	if (rc != RSXKIT_OK) {
		free(ui.mem);
		ui.mem = NULL;
		return rc;
	}
	rsxkit_ui_atlas(ui.mem, ATLAS_PITCH);
	for (int i = 0; i < 2; i++) {
		ui.vb[i] = (rsxkit_vertex *)(ui.mem + ATLAS_BYTES + i * VB_BYTES);
		ui.vb_off[i] = ui.off + ATLAS_BYTES + i * VB_BYTES;
	}

	rsxkit_draw *d = &ui.draw;
	memset(d, 0, sizeof *d);
	d->blend = RSXKIT_BLEND_ALPHA;
	d->cull = RSXKIT_CULL_NONE;
	d->ntex = 1;
	rsxkit_texture *t = &d->tex[0];
	t->offset = ui.off;
	t->pitch = ATLAS_PITCH;
	t->width = RSXKIT_UI_ATLAS_W;
	t->height = RSXKIT_UI_ATLAS_H;
	t->location = RSXKIT_MEM_MAIN;
	t->format = RSXKIT_RGBA8888;
	t->wrap_s = t->wrap_t = RSXKIT_WRAP_CLAMP;
	t->filter = RSXKIT_FILTER_LINEAR;
	t->xf[0] = 1.0f / RSXKIT_UI_ATLAS_W;
	t->xf[1] = 1.0f / RSXKIT_UI_ATLAS_H;
	/* color = T0 · C */
	for (int c = 0; c < 4; c++)
		d->k[RSXKIT_K_T0][1][c] = 1.0f;
	ui.ready = 1;
	return RSXKIT_OK;
}

void rsxkit_ui_begin(float lw, float lh)
{
	if (!ui.ready)
		return;
	ui.half ^= 1;
	/* El RSX ya no lee este búfer (lo usó hace dos cuadros). */
	if (ui.fence[ui.half])
		rsxkit_fence_wait(ui.fence[ui.half]);
	rsxkit_bind_frame(lw, lh);
	ui.draw.scissor[0] = ui.draw.scissor[1] = 0.0f;
	ui.draw.scissor[2] = lw;
	ui.draw.scissor[3] = lh;
	rsxkit_ui_batch(ui.vb[ui.half], VB_CAP, lw, lh);
	ui.active = 1;
}

void rsxkit_ui_end(void)
{
	if (!ui.active)
		return;
	ui.active = 0;
	uint32_t n = rsxkit_ui_count();
	if (n) {
		/* Una llamada por grupo (rsxkit_ui_groups): con el estado igual,
		 * cada una son unas pocas palabras en el búfer de comandos. */
		const uint32_t *first;
		uint32_t groups = rsxkit_ui_groups(&first);
		rsxkit_vertices(ui.vb_off[ui.half], RSXKIT_MEM_MAIN);
		for (uint32_t g = 0; g < groups; g++) {
			uint32_t end = g + 1 < groups ? first[g + 1] : n;
			if (end > first[g])
				rsxkit_draw_triangles(&ui.draw, first[g], end - first[g]);
		}
	}
	ui.fence[ui.half] = rsxkit_fence();
	rsxkit_ui_batch(NULL, 0, 0.0f, 0.0f);
}
