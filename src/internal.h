/* SPDX-License-Identifier: MIT */
/* rsxkit — estado compartido entre los módulos. */
#ifndef RSXKIT_INTERNAL_H
#define RSXKIT_INTERNAL_H

#include <rsx/rsx.h>

#include "rsxkit/rsxkit.h"

#define RSXKIT_FB_MAX       4
/* Etiquetas del RSX: 255 para rsxkit_finish y hacia abajo las de cada copia
 * de cada framebuffer. Las de 0 a 63 son del sistema. */
#define RSXKIT_LABEL_FINISH 255
#define RSXKIT_LABEL_FB(i, slot) (254 - 2 * (i) - (slot))
#define RSXKIT_LABEL_FENCE  246

struct rsxkit_state {
	gcmContextData *ctx;
	void *host;
	rsxkit_screen screen;
	int32_t mode_id;        /* VIDEO_RESOLUTION_* en uso */
	uint32_t color_pitch, depth_pitch;
	uint32_t color_offset[2], depth_offset;
	uint32_t *color_buf[2];
	uint32_t *depth_buf;
	uint32_t cur;           /* búfer en que se dibuja */
	uint32_t finish_seq;
	uint32_t fence_seq;
	int flip_pending;
	/* Estado del dibujo 3D (draw3d.c) que hay que olvidar cuando otro
	 * módulo cambia programas, arreglos de vértices o superficie. */
	int arrays_bound;       /* hay arreglos de vértices activos */
	int prog_3d;            /* el vertex program cargado es el 3D */
	int fp_3d;              /* fragment program 3D cargado (-1: otro) */
	int state_valid;        /* el estado fijo 3D está puesto */
	int screen_bound;       /* la superficie es la pantalla (no un destino) */
};

extern struct rsxkit_state rsxkit_g;

/* Destino ligado con rsxkit_bind (target.c). */
struct rsxkit_bound {
	struct rsxkit_target *target;
	struct rsxkit_depth *depth;
	float lw, lh;        /* espacio lógico */
	float sx, sy;        /* píxeles del destino por unidad lógica */
	int scissor_valid;
	uint32_t scissor[4]; /* último puesto, en píxeles */
};
extern struct rsxkit_bound rsxkit_bound;
void rsxkit_logical_rect(const float r[4], uint32_t out[4]);

/* Asegura `words` palabras libres en el segmento en curso del búfer de
 * comandos (fifo.c). Cada operación pública lo llama antes de escribir. */
void rsxkit_reserve(uint32_t words);
#define RSXKIT_RESERVE_DRAW 512u

/* Espera a que la etiqueta llegue a `value` (comparación con vuelta). */
void rsxkit_wait_label(uint8_t index, uint32_t value);
/* Viewport y estado fijo del dibujo 2D sobre la superficie en curso. */
void rsxkit_set_2d_state(void);
/* Vuelve a la pantalla (búfer trasero) como superficie, con el estado 2D. */
void rsxkit_bind_screen(void);
void rsxkit_fb_shutdown(void);
/* Antes de dibujar en modo inmediato con otros programas: apaga los
 * arreglos de vértices del 3D y olvida su estado. */
void rsxkit_leave_3d(void);
void rsxkit_3d_shutdown(void);
void rsxkit_target_shutdown(void);
/* Copia un fragment program a la memoria del RSX, con interpolación de
 * perspectiva en todas sus entradas (fprog.c). NULL si no hay memoria. */
void *rsxkit_fp_upload(rsxFragmentProgram *fp, u32 *offset);
/* Programas generados (fpcache.c): el de una descripción, generado y
 * subido la primera vez; NULL si no es válida o no hay memoria. `hdr` y
 * `offset` son lo que pide rsxLoadFragmentProgramLocation. */
struct rsxkit_fp_cached {
	rsxkit_fp_desc desc;
	rsxFragmentProgram hdr;
	u32 offset;
};
const struct rsxkit_fp_cached *rsxkit_fp_lookup(const rsxkit_fp_desc *desc);
/* Cambia cada vez que la caché se vacía: lo que apuntaba a una entrada ya
 * no vale. */
uint32_t rsxkit_fp_generation(void);
void rsxkit_fp_count_load(void);
void rsxkit_fp_shutdown(void);
/* Dibuja la región [u0, u1] × [v0, v1] (coordenadas normalizadas) de una
 * textura lineal de la memoria local como un rectángulo en la pantalla
 * (fb.c). */
void rsxkit_blit(uint32_t offset, uint32_t pitch, uint32_t tex_w, uint32_t tex_h, float u0, float v0, float u1,
		 float v1, rsxkit_format format, const rsxkit_rect *r, rsxkit_filter filter);

#endif
