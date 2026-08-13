# SDF Backend

## Purpose

NexDynSdf generates versioned signed-distance assets independently of NexDyn
contact and solver code. NexDyn integration is intended to use the C ABI in
`include/nexsdf/c_api.h`.

## Current implementation status

Implemented representations are dense grids, conservative exact influence
octrees, and adaptive piecewise polynomial octrees. Implemented
reconstructions are trilinear, tricubic Hermite, and cell-centred Gradient
Taylor. OBJ and little-endian NSM v1 imports are supported.

## Algorithm flow

```text
OBJ / NSM v1
  -> exact duplicate-coordinate welding
  -> two-manifold/orientation validation
  -> per-component outward orientation
  -> BVH exact reference and feature pseudo-normals
  -> dense / exact influence / adaptive representation
  -> checksummed .nsdf
  -> C or C++ read-only queries
  -> optional headless diagnostic slices through the same batch query path
```

## Important source files

- `src/mesh_io.cpp`: OBJ and NSM v1 import.
- `src/geometry.cpp`: validation, BVH, exact distance, pseudo-normal sign.
- `src/influence.cpp`: Pujol-Chica sphere-hull support maps and conservative
  GJK/Frank-Wolfe intersection certificates.
- `src/reconstruction.cpp`: trilinear, tricubic, Gradient Taylor.
- `src/asset.cpp`: builders, octrees, `.nsdf`, query dispatch.
- `src/c_api.cpp`: C ABI and exception/status translation.
- `tools/nexsdfviz.cpp`: deterministic distance/normal/depth/error slices.
- `tools/nexsdfmodelaudit.cpp`: complete catalog parser/topology audit using
  the production mesh import and exact-surface preparation path.
- `scripts/generate_readme_images.ps1`: reproducible reference-fixture images.
- `models/`: canonical first-party and SdfLib model collection used by tests,
  benchmarks, examples, and visualization generation.

## Tests and validation

`tests/unit_tests.cpp` checks exact distance, BVH against exhaustive triangle
scan, exact-octree conservation, reconstruction derivatives, adaptive
coarse/fine continuity, serialization, corruption rejection, and C ABI status.
`tests/model_tests.cpp` checks the core PyCoCo, SdfLib, and Nagata generation
fixtures. `tests/model_catalog_smoke.cmake` additionally parses all 32 OBJ/NSM
inputs, verifies the expected 29 runtime-ready assets, and checks all 34
catalog hashes and sizes. `tests/visualization_smoke.cmake` exercises all five
visualization modes against a generated asset. `tests/influence_tests.cpp`
compares all three filters with an exhaustive Gear scan at 257 deterministic
points and checks serialization.

## Known limitations

- Adaptive termination uses the reference 19-point trapezoidal RMS estimate;
  the reported probe maximum is not a certified whole-cell error bound.
- Generation is single-threaded CPU code.
- Multi-shell assets require explicit union or nested-parity semantics;
  intersecting/touching shells are rejected.
- Gradient Taylor is discontinuous at cell boundaries.
- Contact search, pair selection, manifold construction, and solver coupling
  remain NexDyn responsibilities and are not implemented here.

## Planned work

M0 quantitative validation, M1 paper-faithful GJK/Frank-Wolfe filtering, and M2
explicit multi-shell/cavity composition are implemented. Remaining NSM/STL/ENG, CPU parallel/SIMD, and
optional GPU work is maintained in `docs/roadmap.md`.

## Last verified against

Date: 2026-08-13.

Sources: `src/geometry.cpp`, `src/influence.cpp`, `src/reconstruction.cpp`, `src/asset.cpp`,
`src/mesh_io.cpp`, `src/c_api.cpp`, `tools/nexsdfviz.cpp`,
`tools/nexsdfmodelaudit.cpp`, `scripts/generate_readme_images.ps1`,
`scripts/update_model_catalog.ps1`, `scripts/update_model_audit.ps1`,
`models/MANIFEST.md`, `docs/roadmap.md`.

Tests: `tests/unit_tests.cpp`, `tests/c_api_tests.c`, `tests/influence_tests.cpp`,
`tests/model_tests.cpp`, `tests/model_catalog_smoke.cmake`,
`tests/visualization_smoke.cmake`, `tests/install_consumer/main.c`.

Commands run: Release static and shared unit/regression/integration passed;
README image generation and installed-package consumption passed. The final
benchmark result is recorded in `wiki/TestsAndValidation.md`.
