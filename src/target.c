/* SPDX-License-Identifier: MIT */
/*
 * rsxkit — destinos de render, memoria principal mapeada y barreras.
 *
 * Un destino es una superficie lineal X8R8G8B8 en la memoria local del RSX
 * que después se puede leer como textura para presentarla escalada (como
 * el framebuffer 2D). La profundidad va aparte (Z24S8) para que varios
 * destinos compartan una, como hacen las consolas con su z-buffer.
 */
#include <malloc.h>
#include <string.h>

#include "internal.h"

#define TARGET_MAX 8
/* 128: la presentación lee el destino como textura desde una fila
 * cualquiera, y el offset de una textura va alineado a 128. */
#define SURF_ALIGN 128

struct rsxkit_target {
	int used;
	uint32_t w, h, pitch, offset;
	void *mem;
};

struct rsxkit_depth {
	int used;
	uint32_t w, h, pitch, offset;
	void *mem;
};

static struct rsxkit_target targets[TARGET_MAX];
static struct rsxkit_depth depths[TARGET_MAX];

/* Destino ligado (draw3d.c lo usa para el viewport y las tijeras). */
struct rsxkit_bound rsxkit_bound;

int rsxkit_map_main(void *ptr, uint32_t size, uint32_t *offset)
{
	if (!ptr || ((uintptr_t)ptr & 0xFFFFF) || (size & 0xFFFFF) || !size)
		return RSXKIT_EARG;
	return gcmMapMainMemory(ptr, size, offset) == 0 ? RSXKIT_OK : RSXKIT_ENOMEM;
}

uint32_t rsxkit_fence(void)
{
	struct rsxkit_state *g = &rsxkit_g;
	uint32_t v = ++g->fence_seq;
	rsxkit_reserve(16);
	rsxSetWriteBackendLabel(g->ctx, RSXKIT_LABEL_FENCE, v);
	rsxFlushBuffer(g->ctx);
	return v;
}

int rsxkit_fence_done(uint32_t fence)
{
	volatile uint32_t *label = gcmGetLabelAddress(RSXKIT_LABEL_FENCE);
	return (int32_t)(*label - fence) >= 0;
}

void rsxkit_fence_wait(uint32_t fence)
{
	rsxkit_wait_label(RSXKIT_LABEL_FENCE, fence);
}

static void *surface_alloc(uint32_t w, uint32_t h, uint32_t *pitch, uint32_t *offset)
{
	*pitch = (w * 4 + SURF_ALIGN - 1) & ~(SURF_ALIGN - 1);
	void *mem = rsxMemalign(SURF_ALIGN, *pitch * h);
	if (mem)
		rsxAddressToOffset(mem, offset);
	return mem;
}

rsxkit_target *rsxkit_target_create(uint32_t width, uint32_t height)
{
	if (!width || !height || width > 4096 || height > 4096)
		return NULL;
	for (int i = 0; i < TARGET_MAX; i++) {
		struct rsxkit_target *t = &targets[i];
		if (t->used)
			continue;
		memset(t, 0, sizeof *t);
		t->mem = surface_alloc(width, height, &t->pitch, &t->offset);
		if (!t->mem)
			return NULL;
		memset(t->mem, 0, t->pitch * height);
		t->w = width;
		t->h = height;
		t->used = 1;
		return t;
	}
	return NULL;
}

void rsxkit_target_destroy(rsxkit_target *t)
{
	if (!t || !t->used)
		return;
	rsxkit_finish();
	rsxFree(t->mem);
	t->used = 0;
	if (rsxkit_bound.target == t)
		rsxkit_bound.target = NULL;
}

void rsxkit_target_size(const rsxkit_target *t, uint32_t *width, uint32_t *height)
{
	*width = t->w;
	*height = t->h;
}

rsxkit_depth *rsxkit_depth_create(uint32_t width, uint32_t height)
{
	if (!width || !height || width > 4096 || height > 4096)
		return NULL;
	for (int i = 0; i < TARGET_MAX; i++) {
		struct rsxkit_depth *d = &depths[i];
		if (d->used)
			continue;
		memset(d, 0, sizeof *d);
		d->mem = surface_alloc(width, height, &d->pitch, &d->offset);
		if (!d->mem)
			return NULL;
		/* Z24S8 en big-endian: profundidad en los 24 bits altos. */
		uint32_t *p = d->mem;
		for (uint32_t k = 0; k < d->pitch * height / 4; k++)
			p[k] = 0xFFFFFF00u;
		d->w = width;
		d->h = height;
		d->used = 1;
		return d;
	}
	return NULL;
}

void rsxkit_depth_destroy(rsxkit_depth *d)
{
	if (!d || !d->used)
		return;
	rsxkit_finish();
	rsxFree(d->mem);
	d->used = 0;
	if (rsxkit_bound.depth == d)
		rsxkit_bound.depth = NULL;
}

void rsxkit_target_shutdown(void)
{
	for (int i = 0; i < TARGET_MAX; i++) {
		if (targets[i].used)
			rsxFree(targets[i].mem);
		if (depths[i].used)
			rsxFree(depths[i].mem);
		targets[i].used = depths[i].used = 0;
	}
	memset(&rsxkit_bound, 0, sizeof rsxkit_bound);
}

void rsxkit_bind(rsxkit_target *t, rsxkit_depth *d, float lw, float lh)
{
	struct rsxkit_state *g = &rsxkit_g;
	if (!t || !t->used)
		return;
	if (d && (!d->used || d->w < t->w || d->h < t->h))
		d = NULL;
	rsxkit_reserve(256);

	gcmSurface sf;
	memset(&sf, 0, sizeof sf);
	sf.colorFormat = GCM_SURFACE_X8R8G8B8;
	sf.colorTarget = GCM_SURFACE_TARGET_0;
	sf.colorLocation[0] = GCM_LOCATION_RSX;
	sf.colorOffset[0] = t->offset;
	sf.colorPitch[0] = t->pitch;
	for (int i = 1; i < GCM_MAX_MRT_COUNT; i++) {
		sf.colorLocation[i] = GCM_LOCATION_RSX;
		sf.colorPitch[i] = 64;
	}
	sf.depthFormat = GCM_SURFACE_ZETA_Z24S8;
	sf.depthLocation = GCM_LOCATION_RSX;
	/* Sin profundidad propia se pone la de la pantalla, que nunca se toca:
	 * draw3d.c apaga la prueba y la escritura. */
	sf.depthOffset = d ? d->offset : g->depth_offset;
	sf.depthPitch = d ? d->pitch : g->depth_pitch;
	sf.type = GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias = GCM_SURFACE_CENTER_1;
	sf.width = t->w;
	sf.height = t->h;
	rsxSetSurface(g->ctx, &sf);

	f32 scale[4] = { t->w * 0.5f, t->h * -0.5f, 0.5f, 0.0f };
	f32 offset[4] = { t->w * 0.5f, t->h * 0.5f, 0.5f, 0.0f };
	rsxSetViewport(g->ctx, 0, 0, t->w, t->h, 0.0f, 1.0f, scale, offset);
	for (int i = 0; i < 8; i++)
		rsxSetViewportClip(g->ctx, i, t->w, t->h);
	rsxSetScissor(g->ctx, 0, 0, t->w, t->h);

	rsxkit_bound.target = t;
	rsxkit_bound.depth = d;
	rsxkit_bound.lw = lw > 0.0f ? lw : (float)t->w;
	rsxkit_bound.lh = lh > 0.0f ? lh : (float)t->h;
	rsxkit_bound.sx = t->w / rsxkit_bound.lw;
	rsxkit_bound.sy = t->h / rsxkit_bound.lh;
	rsxkit_bound.scissor_valid = 0;
	g->screen_bound = 0;
	g->state_valid = 0;
}

/* Rectángulo lógico → píxeles del destino, recortado. */
void rsxkit_logical_rect(const float r[4], uint32_t out[4])
{
	const struct rsxkit_target *t = rsxkit_bound.target;
	float x0 = r ? r[0] * rsxkit_bound.sx : 0.0f, y0 = r ? r[1] * rsxkit_bound.sy : 0.0f;
	float x1 = r ? r[2] * rsxkit_bound.sx : (float)t->w, y1 = r ? r[3] * rsxkit_bound.sy : (float)t->h;
	if (x0 < 0.0f) x0 = 0.0f;
	if (y0 < 0.0f) y0 = 0.0f;
	if (x1 > (float)t->w) x1 = (float)t->w;
	if (y1 > (float)t->h) y1 = (float)t->h;
	out[0] = (uint32_t)(x0 + 0.5f);
	out[1] = (uint32_t)(y0 + 0.5f);
	out[2] = x1 > x0 ? (uint32_t)(x1 + 0.5f) : out[0];
	out[3] = y1 > y0 ? (uint32_t)(y1 + 0.5f) : out[1];
}

void rsxkit_clear(uint32_t flags, uint32_t rgb, float depth, const float rect[4])
{
	struct rsxkit_state *g = &rsxkit_g;
	if (!rsxkit_bound.target)
		return;
	uint32_t r[4];
	rsxkit_logical_rect(rect, r);
	if (r[2] <= r[0] || r[3] <= r[1])
		return;
	uint32_t mask = 0;
	if (flags & RSXKIT_CLEAR_COLOR)
		mask |= GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A;
	if ((flags & RSXKIT_CLEAR_DEPTH) && rsxkit_bound.depth)
		mask |= GCM_CLEAR_Z | GCM_CLEAR_S;
	if (!mask)
		return;
	rsxkit_reserve(64);
	/* El borrado respeta las tijeras. */
	rsxSetScissor(g->ctx, r[0], r[1], r[2] - r[0], r[3] - r[1]);
	rsxkit_bound.scissor_valid = 0;
	if (depth < 0.0f) depth = 0.0f;
	if (depth > 1.0f) depth = 1.0f;
	rsxSetClearColor(g->ctx, rgb & 0x00FFFFFFu);
	rsxSetClearDepthStencil(g->ctx, (uint32_t)(depth * 16777215.0f) << 8);
	rsxSetColorMask(g->ctx, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
	rsxSetDepthWriteEnable(g->ctx, GCM_TRUE);
	rsxClearSurface(g->ctx, mask);
	g->state_valid = 0;
}

void rsxkit_target_present(rsxkit_target *t, const rsxkit_rect *src, uint32_t img_w, uint32_t img_h,
			   const rsxkit_view *view, rsxkit_rect *out)
{
	if (!t || !t->used)
		return;
	static const rsxkit_view def = { RSXKIT_SCALE_FIT, RSXKIT_FILTER_LINEAR, 0.0f };
	if (!view)
		view = &def;
	rsxkit_rect s = src ? *src : (rsxkit_rect){ 0, 0, t->w, t->h };
	if (s.x < 0 || s.y < 0 || !s.w || !s.h || (uint32_t)s.x + s.w > t->w || (uint32_t)s.y + s.h > t->h)
		s = (rsxkit_rect){ 0, 0, t->w, t->h };
	rsxkit_rect r = rsxkit_layout(img_w ? img_w : s.w, img_h ? img_h : s.h, view);
	if (out)
		*out = r;
	/* La textura empieza en la esquina de la región (offset alineado a
	 * 128: el destino y su pitch lo están; la x se redondea hacia abajo). */
	uint32_t offset = t->offset + (uint32_t)s.y * t->pitch + (((uint32_t)s.x * 4) & ~127u);
	/* Que el RSX termine de escribir el destino antes de leerlo. */
	rsxSetWaitForIdle(rsxkit_g.ctx);
	rsxInvalidateTextureCache(rsxkit_g.ctx, GCM_INVALIDATE_TEXTURE);
	rsxkit_blit(offset, t->pitch, s.w, s.h, 1.0f, 1.0f, RSXKIT_XRGB8888, &r, view->filter);
}
