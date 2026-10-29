# Changelog

## Unreleased

Synced from Salsa64 and SalsaDS (2026-10-29): both emulators now build the same
library.

- Custom fragment programs per draw (`rsxkit_draw.program`), loaded only when the
  program changes; `rsxkit_3d_program_sync(1)` reloads and fences it on every draw.
- Second per-vertex colour in the vertex `pad` word (`rsxkit_vertices_color_aux`).
- Stencil test and separate pass / depth-fail / stencil-fail operations,
  `rsxkit_clear_stencil`, and `RSXKIT_DEPTH_EQUAL`, `_GEQUAL`, `_ALWAYS`.
- Alpha render targets: `rsxkit_target_texture` returns `RSXKIT_ARGB8888` for them
  (3D and post-processing read their alpha), `rsxkit_target_create_rgba`,
  `RSXKIT_CLEAR_ALPHA`, `RSXKIT_DSTA_FRAG`, and the blend modes
  `RSXKIT_BLEND_ALPHA_MAX`, `_REPLACE_MIN_ALPHA` and `_KEEP_MIN_ALPHA`.
- `rsxkit_3d_incremental` to keep state between frames.

Synced from Salsa64 (2026-10-28).

- External bitmap fonts with a glyph cache, and the PS3 system font.
- Mesh passes with affine constants, surface clears and `rsxkit_unmap_main`.
- Video setup can reuse the values currently set by the XMB (mode and pitch), with
  optional tiling.
- `rsxkit_depth_load`: load a Z24S8 depth buffer from main memory.
- Reading a target back at 1:1 uses nearest sampling (linear only when reducing),
  so palette indices stored in a channel are copied exactly.
- Scissors clipped to the target, empty-scissor draws skipped, and the RSX command
  stream is dumped to the log when the RSX stops making progress.
- `README.es.md` updated with the new API; the English README still describes v0.1.0.

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
