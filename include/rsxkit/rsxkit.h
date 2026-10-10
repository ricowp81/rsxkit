/* SPDX-License-Identifier: MIT */
/*
 * rsxkit — capa gráfica genérica para el RSX de la PS3 (PSL1GHT).
 *
 * No sabe nada de ningún sistema emulado: inicializa el RSX y la salida de
 * video, lleva el ciclo de cuadro (doble búfer y flip en vsync) y presenta
 * imágenes 2D de la CPU escaladas a la pantalla. Documentación de la API en
 * lib/rsxkit/README.md.
 *
 * Uso típico, una vez por cuadro:
 *
 *     rsxkit_frame_begin(0x000000);
 *     rsxkit_fb_upload(fb, pixels, w, h, pitch, RSXKIT_RGBA5551);
 *     rsxkit_fb_draw(fb, &view, NULL);
 *     rsxkit_frame_end();
 *
 * No es seguro entre hilos: todas las llamadas desde el mismo hilo.
 */
#ifndef RSXKIT_RSXKIT_H
#define RSXKIT_RSXKIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Inicialización ------------------------------------------------------ */

typedef struct rsxkit_config {
	uint32_t cmd_size;       /* búfer de comandos en bytes; 0 = 512 KB */
	uint32_t host_size;      /* memoria principal mapeada al RSX (incluye el
	                            búfer de comandos); 0 = 16 MB */
	const int32_t *modes;    /* VIDEO_RESOLUTION_* en orden de preferencia;
	                            NULL = 720p, 1080i/p, 480, 576 */
	uint32_t num_modes;
} rsxkit_config;

/* Devuelve 0 o un código negativo (RSXKIT_E*). cfg puede ser NULL. */
int rsxkit_init(const rsxkit_config *cfg);
/* Espera al RSX y libera todo. */
void rsxkit_shutdown(void);

enum {
	RSXKIT_OK = 0,
	RSXKIT_EINIT = -1,     /* rsxInit o la memoria mapeada */
	RSXKIT_EVIDEO = -2,    /* ninguna resolución disponible o videoConfigure */
	RSXKIT_ENOMEM = -3,    /* memoria local del RSX */
	RSXKIT_EARG = -4,      /* argumento fuera de rango */
};

typedef struct rsxkit_screen {
	uint32_t width, height;  /* píxeles de la superficie de salida */
	float aspect;            /* ancho / alto de la imagen en la tele (4:3 o 16:9) */
} rsxkit_screen;

const rsxkit_screen *rsxkit_screen_info(void);

/* ---- Cuadro -------------------------------------------------------------- */

/* Empieza a dibujar en el búfer trasero y lo limpia con un color 0xRRGGBB. */
void rsxkit_frame_begin(uint32_t clear_rgb);
/* Programa el flip en el próximo vsync. Solo bloquea si el flip anterior no
 * ha ocurrido: si la aplicación va más rápido que la tele, la frena. */
void rsxkit_frame_end(void);
/* Espera a que el RSX termine todo lo enviado. */
void rsxkit_finish(void);
/* Búfer de color en que se está dibujando (X8R8G8B8), para pruebas: leerlo
 * desde el PPE es lento. Llamar a rsxkit_finish antes. */
const uint32_t *rsxkit_back_buffer(uint32_t *pitch_bytes);

/* ---- Framebuffer 2D ------------------------------------------------------ */

/* Formatos de píxel de origen. Todos se leen tal cual desde la textura
 * (el RSX convierte y reordena canales); ninguno pasa por el PPE. El orden
 * de bytes es el de la PS3 (big-endian) y el alfa se ignora al presentar. */
typedef enum rsxkit_format {
	RSXKIT_RGBA5551,  /* 16 bits: R[15:11] G[10:6] B[5:1] A[0] */
	RSXKIT_XRGB1555,  /* 16 bits: X[15] R[14:10] G[9:5] B[4:0] */
	RSXKIT_RGB565,    /* 16 bits: R[15:11] G[10:5] B[4:0] */
	RSXKIT_RGBA8888,  /* 32 bits: bytes R, G, B, A */
	RSXKIT_XRGB8888,  /* 32 bits: bytes X, R, G, B */
	RSXKIT_FORMAT_COUNT
} rsxkit_format;

typedef enum rsxkit_scale {
	RSXKIT_SCALE_FIT,      /* lo más grande posible con la proporción `aspect` */
	RSXKIT_SCALE_INTEGER,  /* múltiplo entero del tamaño, centrado */
	RSXKIT_SCALE_STRETCH,  /* toda la pantalla */
} rsxkit_scale;

typedef enum rsxkit_filter {
	RSXKIT_FILTER_LINEAR,
	RSXKIT_FILTER_NEAREST,
} rsxkit_filter;

typedef struct rsxkit_view {
	rsxkit_scale scale;
	rsxkit_filter filter;
	float aspect;  /* ancho / alto con que debe verse la imagen (4/3 para una
	                  consola de tele); 0 = píxeles cuadrados */
} rsxkit_view;

typedef struct rsxkit_rect {
	int32_t x, y;
	uint32_t w, h;
} rsxkit_rect;

typedef struct rsxkit_fb rsxkit_fb;

/* Reserva memoria del RSX para imágenes de hasta max_width × max_height
 * (dos copias: una se dibuja mientras se sube la otra). Hasta 4 a la vez.
 * NULL si no hay memoria. */
rsxkit_fb *rsxkit_fb_create(uint32_t max_width, uint32_t max_height);
void rsxkit_fb_destroy(rsxkit_fb *fb);

/* Copia una imagen de la memoria principal (pitch en bytes) a la textura.
 * Si el RSX aún dibuja con esa copia, espera. Devuelve 0 o RSXKIT_EARG. */
int rsxkit_fb_upload(rsxkit_fb *fb, const void *pixels, uint32_t width, uint32_t height,
		     uint32_t pitch, rsxkit_format format);

/* Dibuja la última imagen subida en el cuadro en curso. Si `rect` no es
 * NULL, recibe el rectángulo de pantalla que ocupó. */
void rsxkit_fb_draw(rsxkit_fb *fb, const rsxkit_view *view, rsxkit_rect *rect);

/* Rectángulo de pantalla para una imagen de width × height con `view`. */
rsxkit_rect rsxkit_layout(uint32_t width, uint32_t height, const rsxkit_view *view);

#ifdef __cplusplus
}
#endif

#endif
