/* SPDX-License-Identifier: MIT */
/*
 * rsxkit — capa gráfica genérica para el RSX de la PS3 (PSL1GHT).
 *
 * No sabe nada de ningún sistema emulado: inicializa el RSX y la salida de
 * video, lleva el ciclo de cuadro (doble búfer y flip en vsync), presenta
 * imágenes 2D de la CPU escaladas a la pantalla y dibuja triángulos en
 * destinos de render con una combinación lineal de dos texturas y el color
 * del vértice. Documentación de la API en lib/rsxkit/README.md.
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

#include "rsxkit/fpgen.h"

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
/* Cambia el modo de video sin reiniciar el RSX: elige el primer modo de
 * cfg->modes que la tele dé (cfg NULL o sin modes: la lista por omisión de
 * rsxkit_init; cmd_size y host_size se ignoran), rehace videoConfigure y los
 * búferes de color y profundidad de la pantalla. Conserva el contexto, el
 * búfer de comandos, la memoria mapeada, los framebuffers, los destinos y
 * la 2D (rsxkit_screen_info() cambia: rehacer lo que dependa de su tamaño).
 * Llamar entre cuadros, sin destino ligado. Devuelve 0 o un código negativo;
 * si falla, deja el modo anterior (si tampoco se pudo, RSXKIT_ENOMEM y no
 * hay pantalla válida). */
int rsxkit_video_reconfigure(const rsxkit_config *cfg);

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

/* ---- Memoria principal y barreras ------------------------------------------ */

/* Hace visible para el RSX `size` bytes de memoria principal (dirección
 * alineada a 1 MB, tamaño múltiplo de 1 MB) y da su offset. Vértices y
 * texturas pueden vivir ahí: los escribe la CPU o un SPE sin copiarlos. */
int rsxkit_map_main(void *ptr, uint32_t size, uint32_t *offset);

/* Barrera: marca lo enviado hasta ahora; rsxkit_fence_done dice si el RSX
 * ya lo terminó y rsxkit_fence_wait lo espera. rsxkit_fence_done solo lee
 * la etiqueta que escribe el RSX: es la única función que se puede llamar
 * desde otro hilo (un productor que espera a que el RSX suelte su búfer). */
uint32_t rsxkit_fence(void);
int rsxkit_fence_done(uint32_t fence);
void rsxkit_fence_wait(uint32_t fence);

/* ---- Destinos de render ------------------------------------------------------ */

typedef struct rsxkit_target rsxkit_target;  /* color X8R8G8B8, memoria local */
typedef struct rsxkit_depth rsxkit_depth;    /* profundidad Z24S8, memoria local */

/* NULL si no hay memoria. El color nace negro y la profundidad en 1. */
rsxkit_target *rsxkit_target_create(uint32_t width, uint32_t height);
void rsxkit_target_destroy(rsxkit_target *t);
void rsxkit_target_size(const rsxkit_target *t, uint32_t *width, uint32_t *height);
rsxkit_depth *rsxkit_depth_create(uint32_t width, uint32_t height);
void rsxkit_depth_destroy(rsxkit_depth *d);

/* Dibujar en t (y su profundidad d, que puede ser NULL) hasta otro
 * rsxkit_bind o rsxkit_frame_begin. Las coordenadas de los vértices y
 * tijeras están en un espacio lógico de lw × lh (y hacia abajo) que se
 * estira sobre todo el destino: un núcleo de 320×240 puede dibujar a 3×
 * sin cambiar sus coordenadas. */
void rsxkit_bind(rsxkit_target *t, rsxkit_depth *d, float lw, float lh);
/* Lo mismo sobre la pantalla del cuadro en curso (después de
 * rsxkit_frame_begin), sin profundidad: menús y superposiciones. */
void rsxkit_bind_frame(float lw, float lh);

#define RSXKIT_CLEAR_COLOR 0x1u
#define RSXKIT_CLEAR_DEPTH 0x2u
/* Borra en el destino ligado el rectángulo lógico (x0, y0, x1, y1), o todo
 * si rect es NULL. depth en [0, 1]. */
void rsxkit_clear(uint32_t flags, uint32_t rgb, float depth, const float rect[4]);

/* Presenta en el cuadro en curso la región `src` del destino (píxeles del
 * destino; NULL = todo), acomodada como una imagen de img_w × img_h con
 * `view`. */
void rsxkit_target_present(rsxkit_target *t, const rsxkit_rect *src, uint32_t img_w, uint32_t img_h,
			   const rsxkit_view *view, rsxkit_rect *out);

/* ---- Dibujo 3D ------------------------------------------------------------------ */

/* Vértice de 32 bytes: posición homogénea en el espacio lógico (x/w, y/w en
 * píxeles lógicos, z/w en [0, 1], w > 0 delante), coordenadas de textura y
 * color RGBA de 8 bits. */
typedef struct rsxkit_vertex {
	float pos[4];
	float st[2];
	uint8_t rgba[4];
	uint32_t pad;
} rsxkit_vertex;

enum { RSXKIT_MEM_LOCAL = 0, RSXKIT_MEM_MAIN = 1 };
enum { RSXKIT_WRAP_CLAMP = 0, RSXKIT_WRAP_REPEAT = 1, RSXKIT_WRAP_MIRROR = 2 };

/* Textura lineal (pitch múltiplo de 64, offset alineado a 128). Las
 * coordenadas del vértice se llevan a la textura con
 * u = s·xf[0] + xf[2], v = t·xf[1] + xf[3] (normalizadas). */
typedef struct rsxkit_texture {
	uint32_t offset;
	uint32_t pitch;
	uint16_t width, height;
	uint8_t location;   /* RSXKIT_MEM_* */
	uint8_t format;     /* rsxkit_format */
	uint8_t wrap_s, wrap_t;
	uint8_t filter;     /* rsxkit_filter */
	uint8_t pad[3];
	float xf[4];
} rsxkit_texture;

typedef enum rsxkit_blend {
	RSXKIT_BLEND_OFF,    /* reemplaza */
	RSXKIT_BLEND_ALPHA,  /* src·a + dst·(1 − a) */
	RSXKIT_BLEND_ADD,    /* src·a + dst */
	RSXKIT_BLEND_NONE,   /* no escribe color */
} rsxkit_blend;

typedef enum rsxkit_cull {
	RSXKIT_CULL_NONE,
	RSXKIT_CULL_FRONT,  /* frente: antihorario con la y hacia arriba */
	RSXKIT_CULL_BACK,
} rsxkit_cull;

#define RSXKIT_DEPTH_TEST   0x1u
#define RSXKIT_DEPTH_WRITE  0x2u
#define RSXKIT_DEPTH_OFFSET 0x4u  /* acercar un poco (calcomanías) */

/* Lo que evalúa el RSX por píxel, con
 *   X_j = k[j][0] + k[j][1]·C + k[j][2]·C.aaaa    (C = color del vértice)
 * (los X_j los arma el vertex program; con niebla, X_j.rgb se escala por
 * (1 − C.a) y el término constante suma fog·C.a):
 * - nterms = 0: la combinación lineal fija, con programas precompilados,
 *     color = T0·X0 + T0.aaaa·X1 + T1·X2 + T1.aaaa·X3 + X4
 *   (términos RSXKIT_K_*; el constante es X4).
 * - nterms > 0: un fragment program generado para estos términos
 *   (rsxkit/fpgen.h; se genera una vez y queda en caché),
 *     color = Σ_j (Π factors[j]) · X_j
 *   con los factores RSXKIT_FP_* (texturas, sus alfas, color del vértice
 *   y su alfa; 0 termina la lista). Con niebla, el color de niebla se suma
 *   al primer término sin factores: incluir uno. Hasta RSXKIT_K_TERMS
 *   términos de RSXKIT_K_FACTORS factores. */
enum { RSXKIT_K_T0, RSXKIT_K_T0A, RSXKIT_K_T1, RSXKIT_K_T1A, RSXKIT_K_ONE, RSXKIT_K_LINCOMB };
#define RSXKIT_K_TERMS   RSXKIT_FP_MAX_TERMS
#define RSXKIT_K_FACTORS RSXKIT_FP_MAX_FACTORS

typedef struct rsxkit_draw {
	uint8_t blend;      /* rsxkit_blend */
	uint8_t cull;       /* rsxkit_cull */
	uint8_t depth;      /* RSXKIT_DEPTH_* */
	uint8_t alpha_ref;  /* 0: sin prueba de alfa; si no, pasa alfa >= ref/255 */
	uint8_t ntex;       /* texturas: 0, 1 o 2 */
	uint8_t fog;
	uint8_t nterms;     /* 0: combinación lineal fija; si no, términos con `factors` */
	uint8_t pad;
	float scissor[4];   /* x0, y0, x1, y1 lógicos */
	float fog_color[4];
	rsxkit_texture tex[2];
	uint8_t factors[RSXKIT_K_TERMS][RSXKIT_K_FACTORS];  /* RSXKIT_FP_*, 0 = fin */
	uint8_t pad2[4];
	float k[RSXKIT_K_TERMS][3][4];
} rsxkit_draw;

/* Programas generados: cuántos distintos hay en la caché, cuántos se
 * generaron en total (con los desalojos, si la caché se llenó) y cuántas
 * veces se cargó un programa distinto del anterior (incluidos los
 * precompilados). */
typedef struct rsxkit_fp_stats {
	uint32_t programs;
	uint32_t generated;
	uint32_t evictions;
	uint32_t loads;
} rsxkit_fp_stats;
void rsxkit_fp_get_stats(rsxkit_fp_stats *out);

/* Arreglo de rsxkit_vertex que usan los dibujos siguientes. */
void rsxkit_vertices(uint32_t offset, uint8_t location);
/* Triángulos sueltos: vértices first .. first + count − 1 del arreglo. */
void rsxkit_draw_triangles(const rsxkit_draw *d, uint32_t first, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
