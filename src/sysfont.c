/* SPDX-License-Identifier: MIT */
/*
 * rsxkit — la fuente del sistema de la PS3 (rsxkit/sysfont.h) con libfont y
 * su FreeType (módulos del firmware). Escrito a partir de las cabeceras de
 * PSL1GHT (font/font.h, font/fontFT.h, fontset.h); docs en
 * lib/rsxkit/README.md y en las notas del RSX del proyecto.
 *
 * Cómo sale un glifo: libfont lo dibuja con cobertura de 8 bits en una
 * superficie propia de 64 × 64 con la pluma en (PEN_X, PEN_Y); de ahí se
 * copia a la caja de ui.h el renglón alrededor de la línea base y las
 * columnas con tinta. Dónde cae la línea base en la superficie (si libfont
 * toma la y de la pluma como línea base o como tope del renglón) no se
 * supone: se mide al abrir con la «H» (su última fila con tinta es la
 * línea base) y vale igual para todos los glifos.
 *
 * Trampas:
 *  - Las funciones de memoria que se le pasan a libfont van por su
 *    descriptor de 32 bits (__get_opd32), como los callbacks de sysutil; con
 *    el puntero de 64 bits la PRX salta a cualquier lado.
 *  - font.h declara «fontontSetScalePixel» (errata); el símbolo exportado es
 *    fontSetScalePixel: se declara aquí.
 *  - Ninguna función de libfont se llama sin haber cargado antes los tres
 *    módulos con sysModuleLoad (sin módulo, el stub salta a basura).
 */
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <font/font.h>
#include <font/fontFT.h>
#include <ppu-asm.h>
#include <sysmodule/sysmodule.h>

#include "rsxkit/rsxkit.h"
#include "rsxkit/sysfont.h"

/* La cabecera de PSL1GHT lo declara mal (fontontSetScalePixel). */
extern s32 fontSetScalePixel(font *f, f32 w, f32 h);

#define FILE_CACHE  (1u << 20)
#define SURF        64
#define PEN_X       16
#define PEN_Y       24
#define BOX         RSXKIT_GLYPH_BOX
#define ENTRIES     8

static const uint16_t modules[] = { SYSMODULE_FONT, SYSMODULE_FREETYPE, SYSMODULE_FONTFT };

/* Juegos de fuentes por escritura, del preferido al de respaldo (valores de
 * CELL_FONT_TYPE_*; los «RODIN2» traen además Latin2). Los de negrita
 * (0x0007xxxx) solo existen con NewRodin, YG y Rodin. */
static const uint32_t sets[5][4] = {
	{ 0x00000204, 0x00000104, 0x00000101, 0 },           /* latín */
	{ 0x00000209, 0x00000109, 0x00000108, 0x00000100 },  /* japonés */
	{ 0x00000209, 0x00000109, 0x00000108, 0x00000100 },  /* coreano */
	{ 0x0000020b, 0x0000010b, 0x0000010a, 0 },           /* chino tradicional */
	{ 0x0000020d, 0x0000010d, 0x0000010c, 0 },           /* chino simplificado */
};
static const uint32_t bold_sets[5][3] = {
	{ 0x00070204, 0x00070104, 0 },
	{ 0x00070201, 0x00070101, 0 },
	{ 0x00070201, 0x00070101, 0 },
	{ 0 },
	{ 0 },
};

static struct {
	int modules;              /* cargados */
	int lib_ready, renderer_ready, font_open, bound;
	void *cache;
	fontEntry entries[ENTRIES];
	const fontLibrary *lib;
	fontRenderer renderer;
	font f;
	fontRenderSurface surf;
	int base;                 /* fila de la línea base en la superficie */
	rsxkit_glyph_source src;
	rsxkit_sysfont_info info;
} sf;

static uint8_t surface[SURF * SURF] __attribute__((aligned(128)));

/* ---- Memoria de libfont: con cabecera para saber el tamaño ---------------- */

#define HDR 16

static void *f_malloc(void *obj, u32 size)
{
	(void)obj;
	uint8_t *p = memalign(16, (size_t)size + HDR);
	if (!p)
		return NULL;
	*(uint32_t *)p = size;
	sf.info.lib_bytes += size;
	if (sf.info.lib_bytes > sf.info.lib_peak)
		sf.info.lib_peak = sf.info.lib_bytes;
	return p + HDR;
}

static void f_free(void *obj, void *ptr)
{
	(void)obj;
	if (!ptr)
		return;
	uint8_t *p = (uint8_t *)ptr - HDR;
	sf.info.lib_bytes -= *(uint32_t *)p;
	free(p);
}

static void *f_realloc(void *obj, void *ptr, u32 size)
{
	if (!ptr)
		return f_malloc(obj, size);
	if (!size) {
		f_free(obj, ptr);
		return NULL;
	}
	uint32_t old = *(uint32_t *)((uint8_t *)ptr - HDR);
	void *n = f_malloc(obj, size);
	if (!n)
		return NULL;
	memcpy(n, ptr, old < size ? old : size);
	f_free(obj, ptr);
	return n;
}

static void *f_calloc(void *obj, u32 num, u32 size)
{
	uint64_t total = (uint64_t)num * size;
	if (total > 0xFFFFFFFFu)
		return NULL;
	void *p = f_malloc(obj, (u32)total);
	if (p)
		memset(p, 0, (size_t)total);
	return p;
}

/* ---- Apertura y cierre ---------------------------------------------------- */

static int fail(int step, s32 err)
{
	sf.info.step = step;
	sf.info.error = err;
	return RSXKIT_EFONT;
}

static void close_font(void)
{
	if (sf.bound)
		fontUnbindRenderer(&sf.f);
	if (sf.font_open)
		fontCloseFont(&sf.f);
	sf.bound = sf.font_open = 0;
	sf.info.open = 0;
}

void rsxkit_sysfont_close(void)
{
	close_font();
	if (sf.renderer_ready)
		fontDestroyRenderer(&sf.renderer);
	if (sf.lib_ready) {
		fontEndLibrary(sf.lib);
		fontEnd();
	}
	sf.renderer_ready = sf.lib_ready = 0;
	sf.lib = NULL;
	free(sf.cache);
	sf.cache = NULL;
	sf.info.cache_bytes = 0;
	while (sf.modules > 0)
		sysModuleUnload(modules[--sf.modules]);
}

static int start_library(void)
{
	for (; sf.modules < (int)(sizeof modules / sizeof modules[0]); sf.modules++) {
		s32 rc = sysModuleLoad(modules[sf.modules]);
		if (rc != 0)
			return fail(1 + sf.modules, rc);
	}
	sf.cache = memalign(128, FILE_CACHE);
	if (!sf.cache)
		return fail(4, 0);
	sf.info.cache_bytes = FILE_CACHE;
	fontConfig cfg;
	fontConfig_initialize(&cfg);
	cfg.fileCache.buffer = sf.cache;
	cfg.fileCache.size = FILE_CACHE;
	cfg.userFontEntryMax = ENTRIES;
	cfg.userFontEntries = sf.entries;
	s32 rc = fontInit(&cfg);
	if (rc != 0)
		return fail(5, rc);
	fontLibraryConfigFT ft;
	fontLibraryConfigFT_initialize(&ft);
	ft.memoryIF.object = NULL;
	ft.memoryIF.malloc_func = (fontMallocCallback)(uintptr_t)__get_opd32(f_malloc);
	ft.memoryIF.free_func = (fontFreeCallback)(uintptr_t)__get_opd32(f_free);
	ft.memoryIF.realloc_func = (fontReallocCallback)(uintptr_t)__get_opd32(f_realloc);
	ft.memoryIF.calloc_func = (fontCallocCallback)(uintptr_t)__get_opd32(f_calloc);
	rc = fontInitLibraryFreeType(&ft, &sf.lib);
	if (rc != 0 || !sf.lib) {
		fontEnd();
		return fail(6, rc);
	}
	sf.lib_ready = 1;
	fontRendererConfig rcfg;
	memset(&rcfg, 0, sizeof rcfg);
	rcfg.bufferingPolicy.buffer = NULL;
	rcfg.bufferingPolicy.initSize = 256u << 10;
	rcfg.bufferingPolicy.maxSize = 1u << 20;
	rcfg.bufferingPolicy.expandSize = 64u << 10;
	rcfg.bufferingPolicy.resetSize = 256u << 10;
	rc = fontCreateRenderer(sf.lib, &rcfg, &sf.renderer);
	if (rc != 0)
		return fail(7, rc);
	sf.renderer_ready = 1;
	return RSXKIT_OK;
}

/* Dibuja `cp` en la superficie (limpia). 0 o el código de libfont. */
static s32 draw(uint32_t cp, fontGlyphMetrics *m)
{
	fontImageTransInfo ti;
	memset(surface, 0, sizeof surface);
	memset(m, 0, sizeof *m);
	return fontRenderCharGlyphImage(&sf.f, cp, &sf.surf, (f32)PEN_X, (f32)PEN_Y, m, &ti);
}

static int covered_raw(uint32_t cp);

static int open_type(uint32_t type)
{
	fontType t = { type, FONT_MAP_UNICODE };
	s32 rc = fontOpenFontset(sf.lib, &t, &sf.f);
	if (rc != 0)
		return fail(8, rc);
	sf.font_open = 1;
	fontSetResolutionDpi(&sf.f, 72, 72);
	if ((rc = fontSetScalePixel(&sf.f, RSXKIT_SYSFONT_PX, RSXKIT_SYSFONT_PX)) != 0)
		return fail(9, rc);
	if ((rc = fontBindRenderer(&sf.f, &sf.renderer)) != 0)
		return fail(10, rc);
	sf.bound = 1;
	if ((rc = fontSetupRenderScalePixel(&sf.f, RSXKIT_SYSFONT_PX, RSXKIT_SYSFONT_PX)) != 0)
		return fail(11, rc);
	fontHorizontalLayout lay;
	memset(&lay, 0, sizeof lay);
	if (fontGetHorizontalLayout(&sf.f, &lay) == 0) {
		sf.info.line_height = lay.lineHeight;
		sf.info.baseline_y = lay.baseLineY;
	}
	fontRenderSurfaceInit(&sf.surf, surface, SURF, 1, SURF, SURF);
	fontRenderSurfaceSetScissor(&sf.surf, 0, 0, SURF, SURF);
	/* Calibración: la «H» se apoya en la línea base. */
	fontGlyphMetrics m;
	if ((rc = draw('H', &m)) != 0)
		return fail(12, rc);
	int top = -1, bottom = -1;
	for (int y = 0; y < SURF; y++)
		for (int x = 0; x < SURF; x++)
			if (surface[y * SURF + x] >= 0x80) {
				if (top < 0)
					top = y;
				bottom = y;
				break;
			}
	sf.info.cal_top = top;
	sf.info.cal_bottom = bottom;
	if (top < 0 || bottom - top < 6)
		return fail(13, 0);
	sf.base = bottom + 1;
	sf.info.id_check = covered_raw('A');
	sf.info.type = type;
	sf.info.open = 1;
	return RSXKIT_OK;
}

/* ¿La fuente tiene `cp`? fontGetFontIdCode falla, o da el glifo 0 (.notdef),
 * con lo que no está. Si con la «A» ya dice que no, esa lectura no sirve en
 * este firmware y queda solo el código de error del dibujo. */
static int covered_raw(uint32_t cp)
{
	u32 id = 0, code = 0;
	if (fontGetFontIdCode(&sf.f, cp, &id, &code) != 0)
		return 0;
	return code != 0;
}

static int covered(uint32_t cp)
{
	return !sf.info.id_check || covered_raw(cp);
}

static int render(void *user, uint32_t cp, uint8_t *alpha, uint32_t pitch, rsxkit_glyph_info *g)
{
	(void)user;
	if (!sf.info.open || cp < 0x20 || !covered(cp)) {
		sf.info.missing++;
		return -1;
	}
	fontGlyphMetrics m;
	if (draw(cp, &m) != 0) {
		sf.info.missing++;
		return -1;
	}
	sf.info.glyphs++;
	/* El renglón de la caja en la superficie. */
	int y0 = sf.base - RSXKIT_SYSFONT_BASELINE;
	int c0 = SURF, c1 = -1;
	for (int y = 0; y < BOX; y++) {
		int sy = y0 + y;
		if (sy < 0 || sy >= SURF)
			continue;
		for (int x = 0; x < SURF; x++)
			if (surface[sy * SURF + x]) {
				if (x < c0)
					c0 = x;
				if (x > c1)
					c1 = x;
			}
	}
	float adv = m.horizontal.advance;
	if (c1 < c0) {  /* sin tinta: un espacio */
		g->left = 0;
		g->width = 0;
	} else {
		int w = c1 - c0 + 1;
		g->left = c0 - PEN_X;
		g->width = w > BOX ? BOX : w;
		for (int y = 0; y < BOX; y++) {
			int sy = y0 + y;
			if (sy < 0 || sy >= SURF)
				continue;
			memcpy(alpha + (size_t)y * pitch, surface + sy * SURF + c0, (size_t)g->width);
		}
	}
	if (!(adv > 0.0f && adv < 3.0f * RSXKIT_SYSFONT_PX))  /* métrica rara: lo que mide la tinta */
		adv = c1 < c0 ? RSXKIT_SYSFONT_PX * 0.3f : (float)(g->left + g->width + 2);
	g->advance = adv;
	return 0;
}

int rsxkit_sysfont_open(rsxkit_sysfont_script script, int bold)
{
	if ((unsigned)script > RSXKIT_SYSFONT_SCHINESE)
		return RSXKIT_EARG;
	sf.info.step = 0;
	sf.info.error = 0;
	int rc;
	if (!sf.renderer_ready && (rc = start_library()) != RSXKIT_OK) {
		int step = sf.info.step;
		s32 err = sf.info.error;
		rsxkit_sysfont_close();
		sf.info.step = step, sf.info.error = err;
		return rc;
	}
	close_font();
	rc = RSXKIT_EFONT;
	for (int pass = bold ? 0 : 1; pass < 2 && rc != RSXKIT_OK; pass++) {
		const uint32_t *list = pass == 0 ? bold_sets[script] : sets[script];
		int n = pass == 0 ? 3 : 4;
		for (int i = 0; i < n && list[i] && rc != RSXKIT_OK; i++) {
			close_font();
			rc = open_type(list[i]);
		}
	}
	if (rc != RSXKIT_OK) {
		int step = sf.info.step;
		s32 err = sf.info.error;
		rsxkit_sysfont_close();
		sf.info.step = step, sf.info.error = err;
		return rc;
	}
	sf.src.name = "sistema";
	sf.src.user = NULL;
	sf.src.render = render;
	return RSXKIT_OK;
}

const rsxkit_glyph_source *rsxkit_sysfont_source(void)
{
	return sf.info.open ? &sf.src : NULL;
}

int rsxkit_sysfont_has(uint32_t cp)
{
	if (!sf.info.open || !covered(cp))
		return 0;
	fontGlyphMetrics m;
	if (draw(cp, &m) != 0)
		return 0;
	for (int i = 0; i < SURF * SURF; i++)
		if (surface[i])
			return 1;
	return 0;
}

void rsxkit_sysfont_info_get(rsxkit_sysfont_info *info)
{
	*info = sf.info;
}
