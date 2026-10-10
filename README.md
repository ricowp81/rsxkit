# rsxkit

Capa gráfica genérica para el RSX de la PlayStation 3 con
[PSL1GHT](https://github.com/ps3dev/PSL1GHT). Nació dentro de Salsa64 (un
emulador de N64), pero no sabe nada de ningún sistema emulado: sirve a
cualquier núcleo o programa homebrew que necesite presentar imágenes de la
CPU en pantalla. Licencia **MIT** (`LICENSE`), para que también la puedan
usar proyectos que no son GPL.

Estado: primera pieza. Hoy hace la inicialización del RSX y la salida de
video, el ciclo de cuadro con doble búfer y un framebuffer 2D escalado. Lo
que viene (generador de fragment programs en tiempo de ejecución,
conversión de texturas en SPE, filtros de posprocesado) se agregará con la
misma regla: nada específico de un sistema emulado.

## Compilar

Requiere el toolchain de PSL1GHT (`ppu-gcc`, `ppu-ar`, `bin2s`) y `cgcomp`
con el Cg Toolkit de NVIDIA para los dos shaders del blit.

```bash
make                    # → build/librsxkit.a
make OUT=/otra/ruta     # salida en otro directorio
```

Para usarla: `-I<rsxkit>/include` y enlazar `librsxkit.a` antes de
`-lrsx -lgcm_sys -lsysutil -lio`.

## Uso

```c
#include <rsxkit/rsxkit.h>

rsxkit_init(NULL);                          /* 720p si se puede */
rsxkit_fb *fb = rsxkit_fb_create(640, 480); /* tamaño máximo de imagen */
const rsxkit_view view = { RSXKIT_SCALE_FIT, RSXKIT_FILTER_LINEAR, 4.0f / 3.0f };

for (;;) {
	/* ... producir la imagen en `pixels` ... */
	rsxkit_frame_begin(0x000000);
	rsxkit_fb_upload(fb, pixels, 320, 240, 320 * 2, RSXKIT_RGBA5551);
	rsxkit_fb_draw(fb, &view, NULL);
	rsxkit_frame_end();                     /* flip en vsync */
}

rsxkit_shutdown();
```

Todas las llamadas desde el mismo hilo. No hay asignaciones de memoria
después de `rsxkit_init` y `rsxkit_fb_create`.

## API

### Inicialización

| Función | Qué hace |
|---|---|
| `int rsxkit_init(const rsxkit_config *cfg)` | Reserva la memoria principal mapeada al RSX y el búfer de comandos, elige la primera resolución disponible de `cfg->modes` (por omisión 720p, 1080, 480, 576), crea dos búferes de color X8R8G8B8 y uno de profundidad en la memoria local del RSX. `cfg` puede ser `NULL`. Devuelve `RSXKIT_OK` o un código negativo (`RSXKIT_EINIT`, `RSXKIT_EVIDEO`, `RSXKIT_ENOMEM`). |
| `void rsxkit_shutdown(void)` | Espera al RSX y libera lo reservado. |
| `const rsxkit_screen *rsxkit_screen_info(void)` | Ancho y alto de la superficie de salida y proporción de la tele (`aspect`: 16/9 o 4/3, según la configuración de video del sistema). |

### Cuadro

| Función | Qué hace |
|---|---|
| `void rsxkit_frame_begin(uint32_t clear_rgb)` | Empieza el cuadro en el búfer trasero y lo limpia con `0xRRGGBB`. |
| `void rsxkit_frame_end(void)` | Programa el flip para el siguiente vsync. Hay como máximo un flip en vuelo: si la aplicación produce cuadros más rápido que la tele, aquí espera; si va más lenta, no espera nunca. |
| `void rsxkit_finish(void)` | Espera a que el RSX termine todo lo enviado. |
| `const uint32_t *rsxkit_back_buffer(uint32_t *pitch)` | Búfer de color del cuadro en curso (X8R8G8B8), para pruebas automáticas. Llamar después de `rsxkit_finish`. Leer la memoria del RSX desde el PPE es muy lento. |

### Framebuffer 2D

Una imagen en memoria principal se copia a una textura lineal en la
memoria local del RSX y se dibuja como un rectángulo. Cada `rsxkit_fb`
tiene dos copias: se sube una mientras el RSX puede estar leyendo la otra
del cuadro anterior; si no, `rsxkit_fb_upload` espera a la etiqueta del
RSX que marca el fin de ese dibujo.

| Función | Qué hace |
|---|---|
| `rsxkit_fb *rsxkit_fb_create(uint32_t max_w, uint32_t max_h)` | Reserva dos texturas de `max_w × max_h` a 32 bits. Hasta 4 framebuffers a la vez. `NULL` si no hay memoria. |
| `void rsxkit_fb_destroy(rsxkit_fb *fb)` | Espera a que el RSX deje de usarlo y lo libera. |
| `int rsxkit_fb_upload(fb, pixels, w, h, pitch, format)` | Copia `h` líneas de `w` píxeles (separadas por `pitch` bytes). `RSXKIT_EARG` si no cabe o el formato no existe. |
| `void rsxkit_fb_draw(fb, view, rect)` | Dibuja la última imagen subida en el cuadro en curso; si `rect` no es `NULL`, recibe el rectángulo de pantalla usado. |
| `rsxkit_rect rsxkit_layout(w, h, view)` | El rectángulo que usaría `rsxkit_fb_draw`, sin dibujar. |

Formatos (`rsxkit_format`), en el orden de bytes de la PS3 (big-endian). El
RSX los lee tal cual: el formato de textura y su reordenamiento de canales
("remap") hacen la conversión, sin pasar los píxeles por el PPE. El alfa se
ignora al presentar.

| Formato | Píxel | Textura del RSX |
|---|---|---|
| `RSXKIT_RGBA5551` | 16 bits: R[15:11] G[10:6] B[5:1] A[0] (el de la N64) | R5G5B5A1 (`0x97`; en PSL1GHT, `GCM_TEXTURE_FORMAT_R5G6B5A1`) |
| `RSXKIT_XRGB1555` | 16 bits: X[15] R[14:10] G[9:5] B[4:0] (0RGB1555 de libretro) | A1R5G5B5 |
| `RSXKIT_RGB565` | 16 bits: R[15:11] G[10:5] B[4:0] | R5G6B5 |
| `RSXKIT_RGBA8888` | 32 bits: bytes R, G, B, A | A8R8G8B8 con los canales rotados |
| `RSXKIT_XRGB8888` | 32 bits: bytes X, R, G, B | A8R8G8B8 |

Vista (`rsxkit_view`):

- `scale`: `RSXKIT_SCALE_FIT` (lo más grande posible con la proporción
  `aspect`, centrado), `RSXKIT_SCALE_INTEGER` (alto múltiplo entero del de
  la imagen) o `RSXKIT_SCALE_STRETCH` (toda la pantalla).
- `filter`: `RSXKIT_FILTER_LINEAR` o `RSXKIT_FILTER_NEAREST`.
- `aspect`: ancho/alto con que debe verse la imagen (4/3 para una consola
  de tele de definición estándar); `0` = píxeles cuadrados. Tiene en cuenta
  que en 480/576 los píxeles de la superficie no son cuadrados.

## Recursos del RSX que usa

- Etiquetas 255 (`rsxkit_finish`) y de 254 hacia abajo, dos por
  framebuffer (247–254 con 4 framebuffers). Las de 0 a 63 son del sistema.
- Unidad de textura que asigna `cgcomp` al sampler del fragment program
  (normalmente la 0) y los atributos de vértice POSITION (0) y TEXCOORD0 (8),
  en modo inmediato.
- Memoria local: 2 búferes de color + 1 de profundidad del tamaño de la
  pantalla (11 MB a 720p, 24 MB a 1080) y `2 × max_w × max_h × 4` bytes por
  framebuffer.

## Origen del código

Código propio. La inicialización del RSX sigue el sample `rsxtest` de
PSL1GHT (MIT). No contiene código de proyectos GPL ni de proyectos sin
licencia clara.
