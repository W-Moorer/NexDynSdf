# Architecture

## Current implementation status

NexDynSdf is split into mesh import, exact surface evaluation, field building,
asset serialization, read-only runtime querying, and optional offline tools. C
and C++ entry points are packaged in one zero-third-party-dependency library;
NexDyn runtime use should load prebuilt assets through the C ABI.

## Main data flow

```text
OBJ / NSM -> SurfaceMesh -> validation -> ExactSurface
  -> grid / exact influence octree / adaptive octree
  -> versioned .nsdf -> C API / C++ wrapper -> NexDyn adapter
                  \-> nexsdfviz -> PPM -> standard-library PNG conversion
```

## Public boundary

- `include/nexsdf/nexsdf.hpp`: C++ types, builders, asset and query API.
- `include/nexsdf/c_api.h`: opaque-handle C ABI; exceptions do not cross it.
- `.nsdf`: explicit little-endian versioned asset; no serialized C++ objects.

The file contains a count table, FNV-1a checksum, validated tree topology, and
representation-specific payload. Approximate assets omit the source mesh;
exact influence assets retain it for witness, feature, and pseudo-normal data.

## Runtime contract

- Load/metadata/query/batch/close are available through the C ABI.
- Query objects are immutable; concurrent read-only queries do not mutate the
  asset. Each query uses local traversal storage.
- `raw_gradient` is the derivative of the selected reconstruction;
  `unit_normal` is normalized separately.
- Domain exit is fail-closed: no unchecked extrapolation.
- Generation/import remain offline operations in the C++ API and CLI.
- Visualization is a separate tool target. It uses batch public queries and is
  neither installed as nor linked into the runtime library.

## Known limitations

- Input meshes must be finite, non-degenerate, closed, consistently oriented
  two-manifolds for signed queries.
- OBJ material and texture payloads are ignored after index validation.
- The exact influence filter is conservative but weaker than SdfLib's GJK
  influence-region test.
- Multiple disconnected or nested shells in one mesh are rejected. They must
  be baked as separate assets until an explicit union/cavity sign policy exists.
- Gradient Taylor is intentionally discontinuous across cell boundaries.
- The builder is single-threaded and CPU-only. Internal exact sampling uses a
  BVH, but no SIMD/GPU/parallel generation path is implemented.
- No MuJoCo contact search/manifold adapter is implemented in this repository;
  this module provides only SDF generation, assets, and point queries.
- `.nsdf` format major version 1 is validated on load but not yet guaranteed as
  a permanent forward-compatible interchange standard.

## Last verified against

Date: 2026-08-13.

Sources:

- `src/geometry.cpp`, `src/reconstruction.cpp`, `src/asset.cpp`,
  `src/mesh_io.cpp`, `src/c_api.cpp`, `tools/nexsdfviz.cpp`
- `include/nexsdf/nexsdf.hpp`, `include/nexsdf/c_api.h`

Tests:

- `tests/unit_tests.cpp`
- `tests/c_api_tests.c`
- `tests/model_tests.cpp`
- `tests/visualization_smoke.cmake`
- `tests/benchmark_exact.cpp`
- `tests/install_consumer/main.c`

Commands run in this session:

- `scripts/run_tests.ps1 -Configuration Release` — passed static unit and
  regression suites.
- static integration suite in `build-test-static-final` — passed.
- `scripts/run_tests.ps1 -Configuration Release -Shared` — passed shared unit
  and regression suites.
- shared integration suite in `build-test-shared-final` — passed.
- `scripts/verify_install.ps1 -Configuration Release` — passed.
- `ctest --test-dir build-test-static-final -C Release -L benchmark
  --output-on-failure -V` — passed; zero numerical difference and 48.4583x
  observed BVH speedup on the migrated Gear fixture.
- `scripts/generate_readme_images.ps1 -Configuration Release
  -ImageResolution 512` — passed.

The benchmark command is recorded in `wiki/TestsAndValidation.md` after its
final run.
