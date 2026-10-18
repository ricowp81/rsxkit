# Changelog

## v0.1.0 - 2026-10-18

First tagged release. The code is the library as used by Salsa64.

- RSX and video initialisation, double-buffered frames, video mode changes.
- 2D framebuffer presentation (five pixel formats, fit / integer / stretch).
- 3D drawing into colour/depth render targets at an integer scale of a logical
  space; fixed linear-combination programs and run-time generated fragment programs
  with a cache.
- Render-to-texture, readback of a target to main memory, between-frame vertex
  interpolation.
- Bitmap-font text, 2D menu layer, post-processing chain machinery.
- Bounded waits with a diagnostic hook.
- New: `examples/rsxkit-demo` (builds an installable `.pkg`), English README,
  `README.es.md`, this changelog.

The API may still change before 1.0.
