# rsxkit

A generic graphics layer for the PlayStation 3's RSX, for homebrew built with
[PSL1GHT](https://github.com/ps3dev/PSL1GHT). MIT licensed.

rsxkit started inside [Salsa64](https://github.com/ricowp81/Salsa64), a Nintendo 64
emulator for the PS3, and knows nothing about any emulated system. Salsa64 is a
real-world example of using it (2D framebuffer presentation, 3D drawing with
generated fragment programs, render targets, post-processing, menus).

**Status: v0.1.0.** The API is in use and works on real hardware, but it may still
change before 1.0. Pin a version.

## What it does

- RSX and video initialisation, resolution selection, double-buffered frames with
  vsync flips, video mode changes without restarting the RSX.
- A scaled 2D framebuffer: upload a CPU image (RGBA5551, XRGB1555, RGB565,
  RGBA8888, XRGB8888) and draw it fit / integer-scaled / stretched, linear or nearest.
- 3D drawing into render targets (colour and depth) at an integer scale of a
  logical coordinate space: triangles with a linear combination of two textures and
  the vertex colour, or with a **fragment program generated at run time**
  (`rsxkit/fpgen.h`, no Cg compiler needed on the console, programs are cached).
- Blending, culling, depth, scissor, fog and alpha test; render-to-texture; reading
  a target back to main memory; vertex interpolation between frames.
- Bitmap-font text (`rsxkit/text.h`) and a small 2D layer for menus: rectangles,
  scalable text, animated waves (`rsxkit/ui.h`).
- A post-processing chain (`rsxkit/post.h`): full-screen passes with precompiled
  fragment programs on intermediate surfaces, plus a box-filter reduction. The
  library gives you the machinery, not concrete filters (CRT, scalers): those are
  yours to write as Cg programs.
- Waits with timeouts and a diagnostic hook, so a stuck RSX can be detected.

## Building

Requires the PSL1GHT toolchain (`ppu-gcc`, `ppu-ar`, `bin2s`) and `cgcomp` with
NVIDIA's Cg Toolkit (used for the library's precompiled shaders). With `PSL1GHT`
set:

```sh
make                  # -> build/librsxkit.a
make OUT=/some/dir    # output elsewhere
```

## Using it

Add `-I<rsxkit>/include` and link `librsxkit.a` **before** the PSL1GHT libraries:

```
-lrsxkit -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
```

(`-lrt` matters: without it `printf` does not reach the TTY.)

```c
#include <rsxkit/rsxkit.h>

rsxkit_init(NULL);                           /* 720p if available */
rsxkit_fb *fb = rsxkit_fb_create(640, 480);  /* maximum image size */
const rsxkit_view view = { RSXKIT_SCALE_FIT, RSXKIT_FILTER_LINEAR, 4.0f / 3.0f };

for (;;) {
	/* ... produce the image in `pixels` ... */
	rsxkit_frame_begin(0x000000);
	rsxkit_fb_upload(fb, pixels, 320, 240, 320 * 2, RSXKIT_RGBA5551);
	rsxkit_fb_draw(fb, &view, NULL);
	rsxkit_frame_end();                      /* flip on vsync */
}
rsxkit_shutdown();
```

All calls come from one thread. There are no allocations after `rsxkit_init` and
`rsxkit_fb_create`.

## Example

`examples/rsxkit-demo` is a complete program: a texture generated on the CPU and
kept in RSX-mapped main memory, drawn on a quad that spins in perspective, a
vertex-coloured triangle, an alpha-blended quad, an offscreen render target
presented with nearest filtering (toggle with Square), and on-screen text.
Cross/Circle quits.

```sh
make -C examples/rsxkit-demo pkg     # build/rsxkit-demo.self and .pkg (APPID RSXK00001)
```

It builds the library first. The package installs from the XMB (Package Manager)
as "rsxkit demo". Release assets include a prebuilt `.pkg`.

## Documentation

The headers in `include/rsxkit/` are the API reference; the comments and
`README.es.md` (a longer description, with one section per area of the API) are in
Spanish. The C API itself is plain C11.

Third-party material: see [THIRD_PARTY.md](THIRD_PARTY.md).

## License

MIT, see [LICENSE](LICENSE). Each source file carries
`SPDX-License-Identifier: MIT`. No GPL code and no code from other emulators is
included.
