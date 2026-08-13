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
```

## Important source files

- `src/mesh_io.cpp`: OBJ and NSM v1 import.
- `src/geometry.cpp`: validation, BVH, exact distance, pseudo-normal sign.
- `src/reconstruction.cpp`: trilinear, tricubic, Gradient Taylor.
- `src/asset.cpp`: builders, octrees, `.nsdf`, query dispatch.
- `src/c_api.cpp`: C ABI and exception/status translation.

## Tests and validation

`tests/unit_tests.cpp` checks exact distance, BVH against exhaustive triangle
scan, exact-octree conservation, reconstruction derivatives, adaptive
coarse/fine continuity, serialization, corruption rejection, and C ABI status.
`tests/model_tests.cpp` checks migrated PyCoCo, SdfLib, and enhanced Nagata
models.

## Known limitations

- The exact influence superset is a conservative AABB/Lipschitz variant, not
  the paper's tighter GJK construction.
- Adaptive termination uses the reference 19-point trapezoidal RMS estimate;
  the reported probe maximum is not a certified whole-cell error bound.
- Generation is single-threaded CPU code.
- One asset accepts exactly one connected closed surface component; multi-shell
  union/cavity semantics are not inferred.
- Gradient Taylor is discontinuous at cell boundaries.
- Contact search, pair selection, manifold construction, and solver coupling
  remain NexDyn responsibilities and are not implemented here.

## Last verified against

Date: 2026-08-13.

Sources: `src/geometry.cpp`, `src/reconstruction.cpp`, `src/asset.cpp`,
`src/mesh_io.cpp`, `src/c_api.cpp`.

Tests: `tests/unit_tests.cpp`, `tests/c_api_tests.c`,
`tests/model_tests.cpp`, `tests/install_consumer/main.c`.

Commands run: Release static unit/regression passed; Release shared unit passed;
installed-package consumer passed.
