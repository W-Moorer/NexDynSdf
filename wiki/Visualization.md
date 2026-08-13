# Visualization

## Current boundary

`nexsdfviz` is an offline, headless diagnostic tool. It loads `.nsdf` assets,
samples a plane with `Asset::query_batch`, and writes a deterministic PPM. A
standard-library Python converter creates PNG files. The core query library has
no OpenGL, VTK, PyVista, NumPy, or image-library dependency.

Implemented modes are signed distance, unit normal, raw-gradient Eikonal
error, octree cell depth, and adaptive measured leaf error. Every mode includes
a pixel-level sampled sign-crossing overlay.

## Reference assessment

SdfLib provides both an OpenGL octree renderer and a uniform-resampling plus
Marching Cubes route. PyCoCoFastSDF provides PyVista/VTK zero-isosurface,
original-versus-reconstruction, normal-error, and one-way surface-distance
views. NexDynSdf adopts their reproducible diagnostic intent, but not their
heavy rendering dependencies or duplicated query implementations.

The complete per-file analysis, metric cautions, result settings, and image
provenance are maintained in `docs/visualization.md`.

## Known limitations

- Current output is a two-dimensional plane diagnostic, not an interactive 3D
  zero-isosurface viewer.
- The dark contour is detected from adjacent sampled signs and is not a
  subpixel Marching Squares reconstruction.
- Automatic ranges are image-local 98th percentiles; use `--range` before
  comparing colors between assets.
- Normal and gradient-error views report the stored reconstruction's query
  result, not a comparison against an independently sampled source surface.
- GPU ray marching and VTK/PyVista validation remain optional future tool-layer
  work and are not linked into the runtime library.

## Reproduce

```powershell
scripts/generate_readme_images.ps1 -Configuration Release -ImageResolution 512
ctest --test-dir build-test-static-final -C Release -L integration --output-on-failure
```

## Last verified against

Date: 2026-08-13.

Sources: `tools/nexsdfviz.cpp`, `scripts/ppm_to_png.py`,
`scripts/generate_readme_images.ps1`, `docs/visualization.md`.

Tests and outputs: `tests/visualization_smoke.cmake`, `docs/images/*.png`.

Commands: Release integration visualization smoke test passed; the 512-pixel
README image generation script completed for PyCoCo sphere, enhanced Nagata
cone, and SdfLib Gear fixtures.
