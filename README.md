# rsxkit

Capa gráfica genérica para el RSX de la PlayStation 3 con
[PSL1GHT](https://github.com/ps3dev/PSL1GHT). Nació dentro de Salsa64 (un
emulador de N64), pero no sabe nada de ningún sistema emulado: sirve a
cualquier núcleo o programa homebrew que necesite presentar imágenes de la
CPU en pantalla. Licencia **MIT** (`LICENSE`), para que también la puedan
usar proyectos que no son GPL.

Estado: hace la inicialización del RSX y la salida de video, el ciclo de
cuadro con doble búfer, un framebuffer 2D escalado y dibujo 3D en destinos
de render a una escala entera (triángulos con una combinación lineal de dos
texturas y el color del vértice, o con un fragment program generado en
tiempo de ejecución para cualquier polinomio en esas entradas). Lo que
viene (conversión de texturas en SPE, filtros de posprocesado) se agregará
con la misma regla: nada específico de un sistema emulado.

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

### Memoria principal y barreras

| Función | Qué hace |
|---|---|
| `int rsxkit_map_main(void *p, uint32_t size, uint32_t *offset)` | Hace visible para el RSX memoria principal (alineada a 1 MB, tamaño múltiplo de 1 MB) y da su offset. Ahí pueden vivir vértices y texturas que escribe la CPU o un SPE, sin copias. |
| `uint32_t rsxkit_fence(void)` | Marca lo enviado hasta ahora. |
| `int rsxkit_fence_done(uint32_t f)` / `void rsxkit_fence_wait(uint32_t f)` | ¿Ya lo terminó el RSX? / esperarlo. Sirve para reutilizar un búfer que el RSX estaba leyendo. `rsxkit_fence_done` solo lee la etiqueta que escribe el RSX y es la única función que puede llamarse desde otro hilo (el que produce los búferes). |

### Destinos de render

Un destino es una superficie X8R8G8B8 en la memoria local; la profundidad
(Z24S8) va aparte para que varios destinos compartan una. Al ligarlos se da
un **espacio lógico** (por ejemplo 320×240) que se estira sobre todo el
destino: un núcleo dibuja en sus coordenadas y la escala (2×, 3×…) la pone
el tamaño del destino.

| Función | Qué hace |
|---|---|
| `rsxkit_target *rsxkit_target_create(w, h)` / `rsxkit_target_destroy(t)` | Destino de color; nace negro. Hasta 8. |
| `rsxkit_depth *rsxkit_depth_create(w, h)` / `rsxkit_depth_destroy(d)` | Profundidad; nace en 1. Debe ser al menos tan grande como el destino. |
| `void rsxkit_bind(t, d, lw, lh)` | Dibujar en `t` (y `d`, o `NULL`) con espacio lógico `lw × lh`, hasta otro `rsxkit_bind` o `rsxkit_frame_begin`. |
| `void rsxkit_clear(flags, rgb, depth, rect)` | Borra color y/o profundidad en un rectángulo lógico (`NULL`: todo). |
| `void rsxkit_target_present(t, src, img_w, img_h, view, out)` | En el cuadro en curso, presenta la región `src` del destino acomodada como una imagen de `img_w × img_h` (como `rsxkit_fb_draw`). |

### Dibujo 3D

```c
rsxkit_vertices(offset_vb, RSXKIT_MEM_MAIN);  /* arreglo de rsxkit_vertex */
rsxkit_bind(target, depth, 320, 240);
rsxkit_draw d = { .blend = RSXKIT_BLEND_OFF, .depth = RSXKIT_DEPTH_TEST | RSXKIT_DEPTH_WRITE,
                  .scissor = { 0, 0, 320, 240 }, .ntex = 1, ... };
d.k[RSXKIT_K_T0][1][0..3] = 1;                /* color = textura × color del vértice */
rsxkit_draw_triangles(&d, first, count);
```

- **Vértice** (`rsxkit_vertex`, 32 bytes): posición homogénea en el espacio
  lógico (x/w, y/w en píxeles lógicos con la y hacia abajo, z/w en [0, 1],
  w > 0 delante), coordenadas de textura `st` y color RGBA de 8 bits. La
  perspectiva la corrige el RSX con la w.
- **Combinación lineal** (por píxel, con `nterms = 0`):
  `color = T0·X0 + T0.aaaa·X1 + T1·X2 + T1.aaaa·X3 + X4`, con
  `X_j = k[j][0] + k[j][1]·C + k[j][2]·C.aaaa` y C el color del vértice
  (los `X_j` los arma el vertex program). Representa exactamente cualquier
  mezcla de dos texturas que no las multiplique entre sí; sirve para los
  combiners de una consola o para el clásico textura × color. Son tres
  fragment programs precompilados (sin textura, una, dos).
- **Términos con programa generado** (`nterms > 0`): ver «Generador de
  fragment programs» más abajo.
- **Texturas** (`rsxkit_texture`): lineales, en memoria local o principal,
  con los formatos de `rsxkit_format`, repetición/espejo/límite, filtro y
  la transformación `u = s·xf[0] + xf[2]`, `v = t·xf[1] + xf[3]`.
- **Estado**: mezcla (opaca, alfa, aditiva, sin color), profundidad (prueba
  LEQUAL, escritura, desplazamiento para calcomanías), recorte de caras
  (frente = antihorario con la y hacia arriba), prueba de alfa, tijeras
  lógicas y niebla por vértice (`fog`: mezcla hacia `fog_color` según
  `C.a`). La z no se recorta contra los planos cercano y lejano: se satura.
- `rsxkit_vertices` invalida las cachés de vértices y texturas del RSX:
  llamarlo cada vez que cambió lo que hay en memoria.
- rsxkit recuerda el estado y solo reenvía lo que cambió.

### Generador de fragment programs

No hay compilador Cg en la consola, así que para lo que la combinación
lineal no representa (una textura por otra, el color del vértice por su
alfa, cualquier producto de las entradas) rsxkit **emite el microcódigo
del fragment program** del RSX en tiempo de ejecución (`rsxkit/fpgen.h`,
`src/fpgen.c`) y lo cachea (`src/fpcache.c`). Un dibujo pide uno con
`nterms > 0`:

```c
rsxkit_draw d = { ..., .ntex = 2, .nterms = 2 };
/* color = T0·T1·X0 + X1 */
d.factors[0][0] = RSXKIT_FP_T0; d.factors[0][1] = RSXKIT_FP_T1;
d.k[0][0][0..3] = 1;                 /* X0 = 1 */
d.k[1][1][0..3] = 1;                 /* X1 = C (color del vértice) */
rsxkit_draw_triangles(&d, first, count);
```

- El programa calcula `color = saturate(Σ_j (Π factors[j]) · X_j)`, con
  los mismos `X_j` del vertex program (`k[j]`, hasta `RSXKIT_K_TERMS` = 7
  términos, uno por varying TEXCOORD1..7) y hasta `RSXKIT_K_FACTORS` = 4
  factores por término: `RSXKIT_FP_T0`, `RSXKIT_FP_T0A` (alfa de la
  textura 0, replicado), `RSXKIT_FP_T1`, `RSXKIT_FP_T1A`, `RSXKIT_FP_C`
  (color del vértice) y `RSXKIT_FP_CA` (su alfa, replicado); 0 termina la
  lista. Un término sin factores suma `X_j`.
- Los cuatro canales van en paralelo: en el canal alfa cada textura
  aporta su alfa y C su alfa, así que un combiner con alfa distinto del
  color se expresa con coeficientes distintos en el cuarto carril de los
  `k[j]`.
- Con `fog`, el color de niebla se suma al **primer término sin
  factores**: incluir uno (con `k` en cero si no hace falta para otra
  cosa).
- `rsxkit_fp_desc` (`ntex`, `nterms`, `factors`) identifica el programa;
  la caché lo busca por hash de la descripción canónica (factores de cada
  término ordenados; el orden de los términos sí cuenta, porque fija qué
  `X_j` lee cada uno). Conviene que quien traduce ponga los términos en
  un orden canónico para que el mismo conjunto dé siempre el mismo
  programa. Un juego usa decenas de programas distintos; la caché guarda
  128 en 96 KB de memoria local del RSX y, si se llena, espera al RSX y
  se vacía (`rsxkit_fp_stats.evictions`).
- Costo: 1 instrucción por textura, 1 por término más 1 por factor
  adicional (y 1 más si el único factor es el color del vértice). Las
  operaciones van en fp16, como las de cgcomp. Cambiar de programa son
  ~20 palabras en el búfer de comandos; `rsxkit_fp_get_stats` cuenta
  programas, generaciones y cargas.
- El codificador (`rsxkit_fpgen_emit`) no depende de PSL1GHT: compila en
  cualquier host y las pruebas unitarias de Salsa64 lo verifican con un
  intérprete del microcódigo anclado a programas compilados por cgcomp
  (`tests/unit/test_fpgen.c`, `tools/fpgen-ref.sh`). Puede usarse solo
  para generar programas fuera de la consola.

Formato del microcódigo: ver la cabecera de `src/fpgen.c` y
`docs/rsx-notes.md` del repositorio de Salsa64.

## Recursos del RSX que usa

- Etiquetas 255 (`rsxkit_finish`), de 254 hacia abajo dos por framebuffer
  (247–254 con 4 framebuffers) y 246 (barreras). Las de 0 a 63 son del
  sistema.
- Programas: el blit (`shaders/rsxkit_blit.*`) y el 3D
  (`rsxkit_lincomb.vcg` con 27 constantes desde la 0, salidas COLOR0 y
  TEXCOORD0..7; `rsxkit_lincomb{0,1,2}.fcg` y los generados, en un pool
  de 96 KB de memoria local); unidades de textura 0 y 1; atributos
  POSITION (0), COLOR0 (3) y TEXCOORD0 (8).
- Búfer de comandos: antes de cada operación rsxkit pide el espacio que
  puede usar, para que las funciones de PSL1GHT no cambien de segmento por
  su cuenta (su callback tiene un error que corrompe el FIFO; ver
  `src/fifo.c`). Quien mezcle llamadas propias a PSL1GHT con rsxkit debería
  hacer lo mismo o usar búferes de comandos grandes.
- Unidad de textura que asigna `cgcomp` al sampler del fragment program
  (normalmente la 0) y los atributos de vértice POSITION (0) y TEXCOORD0 (8),
  en modo inmediato.
- Memoria local: 2 búferes de color + 1 de profundidad del tamaño de la
  pantalla (11 MB a 720p, 24 MB a 1080), `2 × max_w × max_h × 4` bytes por
  framebuffer y `w × h × 4` por destino y por profundidad.

## Origen del código

Código propio. La inicialización del RSX sigue el sample `rsxtest` de
PSL1GHT (MIT); el formato de instrucción del fragment program viene de
`nvfx_shader.h` y `compilerfp.cpp` de cgcomp (PSL1GHT, MIT), cotejado con
los programas que cgcomp compila de `shaders/rsxkit_lincomb*.fcg`. No
contiene código de proyectos GPL ni de proyectos sin licencia clara.
