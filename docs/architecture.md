# Architecture

## Current implementation status

NexDynSdf is split into mesh import, exact surface evaluation, field building,
asset serialization, read-only runtime querying, and optional offline tools. C
and C++ entry points are packaged in one zero-third-party-dependency library;
NexDyn runtime use should load prebuilt assets through the C ABI.

## Main data flow

```text
OBJ / NSM / STL -> SurfaceMesh -> validation -> ExactSurface
  -> grid / exact influence octree / adaptive octree
  -> versioned .nsdf -> C API / C++ wrapper -> NexDyn adapter
                  \-> nexsdfviz -> PPM -> standard-library PNG conversion
  \-> nexsdfmodelaudit -> catalog-wide parser/topology report
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
- `nexsdfmodelaudit` applies the production OBJ/NSM/STL loaders, duplicate welding,
  and topology policy to every catalogued parser input; it does not implement a
  second permissive test-only loader.

## Known limitations

- Input meshes must be finite, non-degenerate, closed, consistently oriented
  two-manifolds for signed queries.
- OBJ material and texture payloads are ignored after index validation.
- STL attribute bytes are ignored; facet normals are advisory orientation data,
  not query gradients or retained corner normals.
- ENG has no standalone geometry and is accepted only as a validated sidecar
  for an already loaded associated NSM mesh.
- Multiple shells require an explicit solid-union or nested-parity build policy;
  intersecting/touching shells are rejected as ambiguous.
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
  `src/mesh_io.cpp`, `src/c_api.cpp`, `tools/nexsdfviz.cpp`,
  `tools/nexsdfmodelaudit.cpp`
- `include/nexsdf/nexsdf.hpp`, `include/nexsdf/c_api.h`

Tests:

- `tests/unit_tests.cpp`
- `tests/c_api_tests.c`
- `tests/model_tests.cpp`
- `tests/model_catalog_smoke.cmake`
- `tests/visualization_smoke.cmake`
- `tests/benchmark_exact.cpp`
- `tests/install_consumer/main.c`

Commands run in this session:

- `scripts/run_tests.ps1 -Configuration Release`: passed static unit and
  regression suites.
- static integration suite in `build-test-static-final`: passed.
- `scripts/run_tests.ps1 -Configuration Release -Shared`: passed shared unit
  and regression suites.
- shared integration suite in `build-test-shared-final`: passed.
- `scripts/verify_install.ps1 -Configuration Release`: passed and verified all
  34 installed model hashes from a clean prefix.
- `ctest --test-dir build-test-static-final -C Release -L benchmark
  --output-on-failure -V`: passed; zero numerical difference and 48.4474x
  observed BVH speedup on the catalogued Gear model.
- `scripts/generate_readme_images.ps1 -Configuration Release
  -ImageResolution 512`: passed.
- Pressure-lubricated cam generation and in-domain query smoke: passed for all
  7,356 input triangles.

The benchmark command is recorded in `wiki/TestsAndValidation.md` after its
final run.
