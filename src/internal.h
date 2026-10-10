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

struct rsxkit_state {
	gcmContextData *ctx;
	void *host;
	rsxkit_screen screen;
	uint32_t color_pitch, depth_pitch;
	uint32_t color_offset[2], depth_offset;
	uint32_t *color_buf[2];
	uint32_t *depth_buf;
	uint32_t cur;           /* búfer en que se dibuja */
	uint32_t finish_seq;
	int flip_pending;
};

extern struct rsxkit_state rsxkit_g;

/* Espera a que la etiqueta llegue a `value` (comparación con vuelta). */
void rsxkit_wait_label(uint8_t index, uint32_t value);
/* Viewport y estado fijo del dibujo 2D sobre la superficie en curso. */
void rsxkit_set_2d_state(void);
void rsxkit_fb_shutdown(void);

#endif
