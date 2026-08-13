# Tests and Validation

## Test suites

- `unit`: exact geometry, all reconstructions, both octrees, coarse/fine
  continuity, `.nsdf` round trips/corruption, and pure C ABI.
- `regression`: core SDF generation fixtures plus a complete 34-asset catalog
  integrity audit (33 OBJ/NSM/STL parser inputs, 32 default-policy runtime-ready
  assets), and the
  deterministic quantitative-validation schema smoke test. The Gear influence
  regression also requires AABB, GJK, and Frank-Wolfe leaves to equal an
  exhaustive scan at 257 deterministic points.
- `integration`: generates a real `.nsdf` and validates all five headless
  visualization output modes.
- `benchmark`: Gear scalar and SIMD-friendly batch exact-surface BVH against
  exhaustive triangle queries; numerical equality is required, speed is
  reported without a machine-specific threshold.

## Commands

```powershell
scripts/run_tests.ps1 -Configuration Release
scripts/run_tests.ps1 -Configuration Release -Shared
scripts/verify_install.ps1 -Configuration Release
ctest --test-dir build-test-static-final -C Release -L integration --output-on-failure
ctest --test-dir build -C Release -L benchmark --output-on-failure -V
scripts/run_validation_matrix.ps1 -Profile Smoke -Configuration Release
scripts/run_backend_comparison.ps1 -Configuration Release -Threads 8
scripts/run_backend_comparison.ps1 -Configuration Release -Cuda
```

## Quantitative field and surface protocol

`nexsdfvalidate` freezes the M0 measurement protocol before influence,
composition, or backend changes. It compares public asset queries with the
exact triangle surface at deterministic Halton domain samples and area-weighted
triangle-interior samples. Both mesh-to-field and Newton-projected
field-to-mesh directions are retained; their maximum is explicitly a sampled
symmetric estimate rather than a certified continuous Hausdorff distance.

The versioned TSV also records raw-gradient and normal error, Eikonal residual,
asset bytes, process peak working set, build/query timing, compiler,
configuration, backend, worker count, model hash, seed, warm-up, and sample
counts. See `docs/validation.md` for definitions and resource caveats.

## M1 influence comparison observation

`scripts/run_influence_comparison.ps1` ran Gear at depth 5 with a 64-triangle
leaf threshold, 4,096 validation points, and 10 warmed query repetitions. All
filters had zero maximum distance error and approximately `7.11e-15` sampled
symmetric surface error.

| Filter | Candidate indices | Asset bytes | Build seconds | Queries/s |
|---|---:|---:|---:|---:|
| AABB/Lipschitz | 3,920,309 | 20,291,884 | 2.47 | 103,010 |
| Paper GJK | 464,907 | 4,292,932 | 13.68 | 182,552 |
| Paper Frank-Wolfe | 470,513 | 4,327,836 | 18.64 | 239,910 |

These host-specific observations are not pass thresholds. They expose the
offline-build versus runtime-size/query tradeoff.

## CPU and optional CUDA backend protocol

The backend comparison uses the same M0 schema and records both build and query
backend/worker counts (`nexsdf-validation-v3`). Scalar and deterministic parallel CPU builds must
preserve exact-octree topology and query identity; dense scalar/parallel output
must be bitwise equal. The auto-SIMD dense-trilinear path has explicit scalar
and gradient tolerances. The optional CUDA integration test uses double
precision and a 1,024-point budget of `2e-12` for phi and `2e-11` for gradient.
CUDA timing includes allocation, host/device transfer, and kernel execution.

Host observation for the 32-cubed cube case (4,096 points, 20 repetitions,
MSVC Release, Ryzen 9 5900X, RTX 3070) showed CPU scalar build `0.055-0.070 s`,
8-thread CPU build `0.013-0.021 s`, and CUDA build `0.142 s`. Query observations
were `4.83-5.59 M/s` scalar, `5.64-7.18 M/s` for the 8-thread scalar kernel,
`7.30-7.92 M/s` auto-SIMD, and `2.82 M/s` CUDA with allocation and transfer
included. All four paths returned the same validation errors. Repeated runs
vary; these are observations, not acceptance thresholds.

On the documented Windows/MSVC host, ThreadSanitizer is unavailable. The
regression therefore exercises fixed partitions repeatedly, checks serialized
shape and per-query identity, and relies on disjoint output ownership plus
exception-safe joins; a sanitizer run remains required on a supported
Clang/Linux CI host before promoting parallel mode from opt-in to the default.

## Model collection

All 34 persisted assets found in the reviewed PyCoCoFastSDF,
enhanced-nagata-sdf, SDFmodel, and SdfLib source locations are stored under the
root `models/` directory. This includes all 24 PyCoCo OBJ files and the
runtime-ready `PressureLubricatedCam.obj`, rather than only the original six
regression fixtures. Source revisions, tracked/generated state, ownership,
hashes, and sizes are recorded in `models/CATALOG.tsv`; current import and
topology results are in `models/AUDIT.tsv`.

PyCoCo, Nagata, and SDFmodel models are the repository owner's prior
first-party work and carry no separate third-party license. Only the SdfLib
Gear model retains a model-specific MIT license. Tests do not download mutable
external assets. `scripts/update_model_catalog.ps1` fails if any copied asset
differs from its local source, while the catalog regression fails if an asset,
size, hash, parse count, runtime-ready count, or committed audit row drifts.
`scripts/update_model_audit.ps1` deliberately uses the production loaders to
regenerate the canonical UTF-8/LF audit report. The repaired cam/gear assets
are additionally regenerated byte-for-byte in the catalog regression; STL and
ENG corruption cases must fail closed, and cam NSM/STL signed distances agree
within the float32 STL precision budget.
README images are regenerated from those fixtures with
`scripts/generate_readme_images.ps1`; the visualizer integration test does not
compare screenshots by appearance and instead verifies successful deterministic
output of each supported mode by repeating generation and comparing SHA-256.

## Known limitations

- Performance numbers are machine-specific observations, not pass thresholds.
- The validation suite covers the catalogued model set and analytic cube cases;
  it does not certify every possible degenerate CAD export.

## Last verified against

Date: 2026-08-13.

Sources: `CMakeLists.txt`, `scripts/run_tests.ps1`,
`scripts/verify_install.ps1`, `scripts/run_validation_matrix.ps1`,
`scripts/run_backend_comparison.ps1`,
`tools/nexsdfvalidate.cpp`, `models/MANIFEST.md`.

Tests: `tests/unit_tests.cpp`, `tests/c_api_tests.c`,
`tests/model_tests.cpp`, `tests/model_catalog_smoke.cmake`,
`tests/visualization_smoke.cmake`,
`tests/validation_smoke.cmake`,
`tests/influence_tests.cpp`,
`tests/benchmark_exact.cpp`, `tests/cuda_tests.cpp`.

Results recorded before documentation finalization:

- Release static `unit`: passed (2 tests).
- Release static `regression`: passed (4 tests, including the complete model
  catalog audit).
- Release static `integration`: passed (1 test).
- Release shared `unit`: passed (2 tests).
- Release shared `regression`: passed (4 tests, including the complete model
  catalog audit).
- Release shared `integration`: passed (1 test).
- Installed-package consumer: passed.
- Optional CUDA `gpu;integration`: passed (1 test).
- SIMD-disabled Release unit/regression suites: passed.

The final benchmark measurement is appended after the final benchmark run.

Final benchmark observation on this host:

- SdfLib Gear fixture: 6,882 triangles, 1,024 deterministic query points.
- Scalar and SIMD-friendly batch BVH versus exhaustive maximum error: `0`.
- Scalar BVH: `0.0059261 s`; batch BVH: `0.0070203 s`; exhaustive:
  `0.276218 s`.
- Observed scalar/exhaustive speedup: `46.6105x`; observed batch/exhaustive
  speedup: `39.3457x`. These are observations, not pass thresholds or portable
  performance guarantees.

Final CLI smoke checks generated, loaded, and queried dense trilinear,
adaptive tricubic NSM, and exact influence octree assets successfully.
The headless visualizer generated all five diagnostic modes, and the README
image pipeline completed for the PyCoCo sphere and pressure-lubricated cam,
Nagata cone, and SdfLib Gear.
The pressure-lubricated cam smoke generated a 1,728-node dense trilinear asset
from all 7,356 triangles and returned an in-domain signed query successfully.
