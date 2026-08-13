# Visualization architecture and reference review

## Scope and decision

NexDynSdf visualization is an offline diagnostic layer. The implemented
`nexsdfviz` executable loads a versioned `.nsdf`, samples a world-space plane
through `Asset::query_batch`, and writes a binary RGB PPM. The standard-library
`scripts/ppm_to_png.py` converter produces the PNG files embedded in the
README. Neither tool changes the SDF runtime ABI or adds a rendering dependency
to `NexSdfQuery`.

This architecture deliberately adopts the reproducible sampling and validation
ideas shared by SdfLib and PyCoCoFastSDF, while deferring their heavyweight
interactive rendering stacks. It avoids maintaining a second copy of octree
traversal or reconstruction equations in a shader or Python file.

## SdfLib visualization review

Reviewed base revision: `8db373ef71d6`. The OpenGL renderer/viewer files are
tracked at that revision. `visualize_sdf.py`, `VISUALIZATION_README.md`, and
`src/tools/SdfSampler/main.cpp` were reviewed from the local SdfLib working tree
and are untracked there; conclusions about that offline path therefore apply to
the inspected local files, not to the named commit alone.

SdfLib contains two distinct visualization paths:

1. `src/tools/SdfSampler/main.cpp` samples a serialized SDF on a uniform grid.
   `visualize_sdf.py` reads the resulting raw volume, extracts an isosurface
   with `skimage.measure.marching_cubes`, maps grid coordinates back to world
   coordinates, and renders static Matplotlib PNGs or interactive Plotly HTML.
2. `src/render_engine` and `src/tools/SdfViewer` provide an interactive OpenGL
   viewer. `RenderSdf.cpp` uploads the octree to a shader-storage buffer and
   dispatches `sdfOctreeRender.comp` into a floating-point render texture.
   That compute shader performs octree traversal, SDF evaluation, gradient
   evaluation, ray marching, ambient occlusion, and soft shadows. Other plane
   shaders display a blue-to-white-to-red sign map, the zero line, repeated
   isolines, and grid boundaries. `SdfRender` currently expects an
   `OctreeSdf` after loading the generic SDF file.

Strengths:

- the OpenGL path supports responsive inspection of octree fields and camera
  motion;
- plane shaders expose sign, interpolation cells, and repeated level sets;
- the sampler/Marching Cubes path decouples isosurface extraction from the
  interactive renderer and supports side-by-side fields.

Integration risks for NexDynSdf:

- the compute shader duplicates octree layout, traversal, interpolation, and
  derivative equations, so CPU and GPU behavior can diverge;
- the viewer introduces OpenGL context, GL loader, window, GLM, shader, and
  logging dependencies that are unsuitable as NexDyn runtime dependencies;
- GPU/driver availability and interactive windows make deterministic CI
  validation harder;
- resampling an adaptive or exact field to a dense volume discards its native
  leaf/influence metadata before visualization.

The implemented NexDynSdf distance palette and zero-line overlay follow the
useful plane-diagnostic idea, but all samples come from the public CPU query
implementation.

## PyCoCoFastSDF visualization review

Reviewed revision: `74155fa32704`.

PyCoCoFastSDF uses NumPy plus PyVista/VTK as a scientific validation stack:

- dense cell-centred arrays become `pyvista.ImageData` with dimensions
  `shape + 1`, Fortran-order flattening, then `cell_data_to_point_data()` before
  a zero contour is extracted;
- `demo_gsdf_zero_isosurface.py` samples sparse Gradient SDF blocks over their
  narrow-band bounding box, assigns a positive truncation value to misses,
  and contours point data;
- `batch_generate_comparison_png.py` uses an off-screen two-panel Plotter for
  original OBJ versus reconstructed zero-isosurface screenshots;
- `demo_sdf_normal_error_heatmap.py` locates the closest OBJ triangle, samples
  its face normal or barycentrically interpolated vertex normals, and colors
  the reconstructed surface by angular normal error;
- `demo_sdf_hausdorff_vis.py` uses `vtkImplicitPolyDataDistance` to compute
  OBJ-to-isosurface vertex distances and reports maximum distance, a percentile
  distance, and average surface distance. It can export VTK PolyData for
  ParaView.

Strengths:

- zero-isosurface, original-mesh comparison, normal error, and surface-distance
  heatmaps connect visualization to quantitative validation;
- the off-screen batch renderer produces consistent model galleries;
- VTK locators and contouring provide mature geometric operations.

Interpretation cautions:

- cell-to-point conversion changes a cell-centred scalar field before
  contouring and must not be confused with the field's native query rule;
- assigning a positive value to sparse misses closes a visualization volume
  but is not an SDF query result;
- the displayed normal can be a reconstructed isosurface geometry normal
  rather than the SDF's raw gradient;
- the reviewed Hausdorff tool computes one direction on mesh vertices. A true
  symmetric surface Hausdorff metric requires both directions and a sampling
  policy dense enough to represent triangle interiors;
- hard-coded paths/configuration and VTK/PyVista dependencies make the demos
  less suitable as a zero-dependency library tool.

NexDynSdf therefore adopts off-screen deterministic output and metadata-based
diagnostics now. A future 3D validation layer may optionally consume exported
samples with VTK/PyVista, but it must remain outside the runtime library and
must state the resampling and metric definitions explicitly.

## Implemented `nexsdfviz` modes

| Mode | Quantity | Mapping |
|---|---|---|
| `distance` | query `phi` | blue negative, near-zero white, red positive |
| `normal` | query `unit_normal` | `0.5 * (n + 1)` mapped to RGB |
| `gradient-error` | `abs(length(raw_gradient) - 1)` | sequential 0-to-range palette |
| `depth` | query `cell_depth` | sequential 0-to-range palette |
| `error` | query `measured_leaf_error` | sequential 0-to-range palette |

All modes overlay a dark line wherever horizontally or vertically adjacent
pixel samples have opposite signs. Automatic scalar ranges use the 98th
percentile of valid sampled magnitudes so isolated outliers do not flatten the
image; `--range` fixes the range when cross-image color comparability is more
important. The output PPM header records mode, axis, coordinate, and range.

The zero overlay is a pixel-level sign-crossing diagnostic, not a geometrically
interpolated Marching Squares contour. `gradient-error` visualizes the Eikonal
residual of the stored reconstruction; it does not by itself measure surface
position error. `error` is meaningful only when the representation supplies
measured leaf errors.

## Committed result provenance

All images use `512 x 512` point samples at the center of the named plane:

| Images | Fixture and asset settings | Observed asset metadata |
|---|---|---|
| `pycoco-sphere-gradient-*` | PyCoCo sphere OBJ; dense grid; Gradient Taylor; resolution 64; z slice | 1,520 triangles; 262,144 grid entries |
| `nagata-cone-adaptive-*` | enhanced Nagata cone NSM; adaptive tricubic; start depth 2; maximum depth 7; tolerance 0.002; x slice | 2,432 triangles; 35,529 nodes; measured asset maximum 0.0162801 |
| `sdflib-gear-exact-distance` | SdfLib Gear OBJ; exact influence octree; start depth 2; maximum depth 8; 48 triangles per leaf; z slice | 6,882 triangles; 2,903,649 nodes |

Regenerate the tracked PNG files with:

```powershell
scripts/generate_readme_images.ps1 -Configuration Release -ImageResolution 512
```

The models, source revisions, original paths, licenses, and file hashes are
recorded in `tests/models/MANIFEST.md` and `docs/reference-provenance.md`.

## Last verified against

Date: 2026-08-13.

NexDynSdf sources and outputs:

- `tools/nexsdfviz.cpp`
- `scripts/ppm_to_png.py`
- `scripts/generate_readme_images.ps1`
- `tests/visualization_smoke.cmake`
- `docs/images/*.png`

Reference sources:

- SdfLib `visualize_sdf.py`, `src/tools/SdfSampler/main.cpp`,
  `src/render_engine/RenderSdf.cpp`, `src/tools/SdfViewer/main.cpp`,
  `src/tools/SdfRender/main.cpp`, and the SDF plane/octree shaders
- PyCoCoFastSDF `batch/batch_generate_comparison_png.py`,
  `CoCoSDF/demo_gsdf_zero_isosurface.py`,
  `CoCoFastTraditionalSDF/demo_sdf_normal_error_heatmap.py`, and
  `CoCoFastTraditionalSDF/demo_sdf_hausdorff_vis.py`

Verification commands:

- `ctest --test-dir build-test-static-final -C Release -L integration --output-on-failure`
- `scripts/generate_readme_images.ps1 -Configuration Release -ImageResolution 512`
