/* SPDX-License-Identifier: MIT */
/*
 * rsxkit — inicialización del RSX, salida de video y ciclo de cuadro.
 *
 * Basado en la inicialización de tests/phase0/rsx-triangle de Salsa64, que
 * a su vez sigue el sample rsxtest de PSL1GHT (MIT): búfer de comandos en
 * memoria principal mapeada, dos búferes de color X8R8G8B8 y uno de
 * profundidad en la memoria local del RSX, flip en vsync.
 */
#include <malloc.h>
#include <string.h>
#include <sys/systime.h>
#include <sysutil/video.h>

#include "internal.h"

#define DEFAULT_CMD_SIZE  0x80000u
#define DEFAULT_HOST_SIZE (16u << 20)

struct rsxkit_state rsxkit_g;

static const int32_t default_modes[] = {
	VIDEO_RESOLUTION_720, VIDEO_RESOLUTION_1080, VIDEO_RESOLUTION_480, VIDEO_RESOLUTION_576,
};

void rsxkit_wait_label(uint8_t index, uint32_t value)
{
	volatile uint32_t *label = gcmGetLabelAddress(index);
	if ((int32_t)(*label - value) >= 0)
		return;
	rsxFlushBuffer(rsxkit_g.ctx);  /* por si la escritura sigue en el búfer */
	while ((int32_t)(*label - value) < 0)
		sysUsleep(30);
}

static int init_video(const rsxkit_config *cfg)
{
	const int32_t *modes = cfg && cfg->modes ? cfg->modes : default_modes;
	uint32_t n = cfg && cfg->modes ? cfg->num_modes : sizeof default_modes / sizeof default_modes[0];
	videoResolution res;
	int32_t id = -1;

	for (uint32_t i = 0; i < n; i++) {
		if (videoGetResolutionAvailability(VIDEO_PRIMARY, modes[i], VIDEO_ASPECT_AUTO, 0) == 1 &&
		    videoGetResolution(modes[i], &res) == 0) {
			id = modes[i];
			break;
		}
	}
	if (id < 0)
		return RSXKIT_EVIDEO;

	videoConfiguration vc;
	memset(&vc, 0, sizeof vc);
	vc.resolution = (u8)id;
	vc.format = VIDEO_BUFFER_FORMAT_XRGB;
	vc.aspect = VIDEO_ASPECT_AUTO;
	vc.pitch = res.width * 4;
	if (videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0) != 0)
		return RSXKIT_EVIDEO;

	videoState st;
	int wide = id == VIDEO_RESOLUTION_720 || id == VIDEO_RESOLUTION_1080;
	if (videoGetState(VIDEO_PRIMARY, 0, &st) == 0 && st.displayMode.aspect != VIDEO_ASPECT_AUTO)
		wide = st.displayMode.aspect == VIDEO_ASPECT_16_9;

	rsxkit_g.screen.width = res.width;
	rsxkit_g.screen.height = res.height;
	rsxkit_g.screen.aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
	return RSXKIT_OK;
}

int rsxkit_init(const rsxkit_config *cfg)
{
	struct rsxkit_state *g = &rsxkit_g;
	uint32_t cmd = cfg && cfg->cmd_size ? cfg->cmd_size : DEFAULT_CMD_SIZE;
	uint32_t host = cfg && cfg->host_size ? cfg->host_size : DEFAULT_HOST_SIZE;

	memset(g, 0, sizeof *g);
	g->host = memalign(1024 * 1024, host);
	if (!g->host || rsxInit(&g->ctx, cmd, host, g->host) != 0 || !g->ctx)
		return RSXKIT_EINIT;
	int rc = init_video(cfg);
	if (rc != RSXKIT_OK)
		return rc;

	rsxkit_finish();
	gcmSetFlipMode(GCM_FLIP_VSYNC);

	uint32_t w = g->screen.width, h = g->screen.height;
	g->color_pitch = w * 4;
	g->depth_pitch = w * 4;
	for (int i = 0; i < 2; i++) {
		g->color_buf[i] = rsxMemalign(64, h * g->color_pitch);
		if (!g->color_buf[i])
			return RSXKIT_ENOMEM;
		rsxAddressToOffset(g->color_buf[i], &g->color_offset[i]);
		gcmSetDisplayBuffer(i, g->color_offset[i], g->color_pitch, w, h);
	}
	g->depth_buf = rsxMemalign(64, h * g->depth_pitch);
	if (!g->depth_buf)
		return RSXKIT_ENOMEM;
	rsxAddressToOffset(g->depth_buf, &g->depth_offset);

	/* Sin arreglos de vértices: rsxkit dibuja en modo inmediato. */
	for (int i = 0; i < 16; i++)
		rsxBindVertexArrayAttrib(g->ctx, i, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	return RSXKIT_OK;
}

void rsxkit_shutdown(void)
{
	struct rsxkit_state *g = &rsxkit_g;
	if (!g->ctx)
		return;
	gcmSetWaitFlip(g->ctx);
	rsxFinish(g->ctx, 1);
	rsxkit_fb_shutdown();
	for (int i = 0; i < 2; i++)
		if (g->color_buf[i])
			rsxFree(g->color_buf[i]);
	if (g->depth_buf)
		rsxFree(g->depth_buf);
	g->ctx = NULL;
}

const rsxkit_screen *rsxkit_screen_info(void)
{
	return &rsxkit_g.screen;
}

void rsxkit_finish(void)
{
	struct rsxkit_state *g = &rsxkit_g;
	uint32_t v = ++g->finish_seq;
	rsxSetWriteBackendLabel(g->ctx, RSXKIT_LABEL_FINISH, v);
	rsxFlushBuffer(g->ctx);
	rsxkit_wait_label(RSXKIT_LABEL_FINISH, v);
}

static void set_surface(void)
{
	struct rsxkit_state *g = &rsxkit_g;
	gcmSurface sf;
	memset(&sf, 0, sizeof sf);
	sf.colorFormat = GCM_SURFACE_X8R8G8B8;
	sf.colorTarget = GCM_SURFACE_TARGET_0;
	sf.colorLocation[0] = GCM_LOCATION_RSX;
	sf.colorOffset[0] = g->color_offset[g->cur];
	sf.colorPitch[0] = g->color_pitch;
	for (int i = 1; i < GCM_MAX_MRT_COUNT; i++) {
		sf.colorLocation[i] = GCM_LOCATION_RSX;
		sf.colorPitch[i] = 64;
	}
	sf.depthFormat = GCM_SURFACE_ZETA_Z24S8;
	sf.depthLocation = GCM_LOCATION_RSX;
	sf.depthOffset = g->depth_offset;
	sf.depthPitch = g->depth_pitch;
	sf.type = GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias = GCM_SURFACE_CENTER_1;
	sf.width = g->screen.width;
	sf.height = g->screen.height;
	rsxSetSurface(g->ctx, &sf);
}

void rsxkit_set_2d_state(void)
{
	struct rsxkit_state *g = &rsxkit_g;
	uint32_t w = g->screen.width, h = g->screen.height;
	f32 scale[4] = { w * 0.5f, h * -0.5f, 0.5f, 0.0f };
	f32 offset[4] = { w * 0.5f, h * 0.5f, 0.5f, 0.0f };

	rsxSetColorMask(g->ctx, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
	rsxSetColorMaskMrt(g->ctx, 0);
	rsxSetViewport(g->ctx, 0, 0, w, h, 0.0f, 1.0f, scale, offset);
	rsxSetScissor(g->ctx, 0, 0, w, h);
	rsxSetZMinMaxControl(g->ctx, GCM_FALSE, GCM_TRUE, GCM_FALSE);
	for (int i = 0; i < 8; i++)
		rsxSetViewportClip(g->ctx, i, w, h);
	rsxSetDepthTestEnable(g->ctx, GCM_FALSE);
	rsxSetDepthWriteEnable(g->ctx, GCM_FALSE);
	rsxSetCullFaceEnable(g->ctx, GCM_FALSE);
	rsxSetBlendEnable(g->ctx, GCM_FALSE);
	rsxSetShadeModel(g->ctx, GCM_SHADE_MODEL_SMOOTH);
}

void rsxkit_frame_begin(uint32_t clear_rgb)
{
	struct rsxkit_state *g = &rsxkit_g;
	set_surface();
	rsxkit_set_2d_state();
	rsxSetClearColor(g->ctx, clear_rgb & 0x00FFFFFFu);
	rsxSetClearDepthStencil(g->ctx, 0xFFFFFF00u);
	rsxClearSurface(g->ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A |
				      GCM_CLEAR_S | GCM_CLEAR_Z);
}

void rsxkit_frame_end(void)
{
	struct rsxkit_state *g = &rsxkit_g;
	/* Un flip en vuelo como máximo: el búfer que se va a reutilizar es el
	 * que estaba en pantalla antes del último flip. */
	if (g->flip_pending) {
		while (gcmGetFlipStatus() != 0)
			sysUsleep(200);
	}
	gcmResetFlipStatus();
	gcmSetFlip(g->ctx, g->cur);
	rsxFlushBuffer(g->ctx);
	gcmSetWaitFlip(g->ctx);
	g->flip_pending = 1;
	g->cur ^= 1;
}

const uint32_t *rsxkit_back_buffer(uint32_t *pitch_bytes)
{
	if (pitch_bytes)
		*pitch_bytes = rsxkit_g.color_pitch;
	return rsxkit_g.color_buf[rsxkit_g.cur];
}
