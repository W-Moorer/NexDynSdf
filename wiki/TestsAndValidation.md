# Tests and Validation

## Test suites

- `unit`: exact geometry, all reconstructions, both octrees, coarse/fine
  continuity, `.nsdf` round trips/corruption, and pure C ABI.
- `regression`: six migrated reference models from PyCoCoFastSDF, SdfLib, and
  enhanced-nagata-sdf.
- `integration`: generates a real `.nsdf` and validates all five headless
  visualization output modes.
- `benchmark`: Gear exact-surface BVH against exhaustive triangle queries;
  numerical equality is required, speed is reported without a machine-specific
  threshold.

## Commands

```powershell
scripts/run_tests.ps1 -Configuration Release
scripts/run_tests.ps1 -Configuration Release -Shared
scripts/verify_install.ps1 -Configuration Release
ctest --test-dir build-test-static-final -C Release -L integration --output-on-failure
ctest --test-dir build -C Release -L benchmark --output-on-failure -V
```

## Migrated fixtures

Source revisions, licenses, and hashes are recorded in
`tests/models/MANIFEST.md`. Tests do not download mutable external assets.
README images are regenerated from those fixtures with
`scripts/generate_readme_images.ps1`; the visualizer integration test does not
compare screenshots by appearance and instead verifies successful deterministic
output of each supported mode by repeating generation and comparing SHA-256.

## Known limitations

- Performance numbers are machine-specific observations, not pass thresholds.
- The validation suite covers the migrated model set and analytic cube cases;
  it does not certify every possible degenerate CAD export.

## Last verified against

Date: 2026-08-13.

Sources: `CMakeLists.txt`, `scripts/run_tests.ps1`,
`scripts/verify_install.ps1`.

Tests: `tests/unit_tests.cpp`, `tests/c_api_tests.c`,
`tests/model_tests.cpp`, `tests/visualization_smoke.cmake`,
`tests/benchmark_exact.cpp`.

Results recorded before documentation finalization:

- Release static `unit`: passed (2 tests).
- Release static `regression`: passed (1 test).
- Release static `integration`: passed (1 test).
- Release shared `unit`: passed (2 tests).
- Release shared `regression`: passed (1 test).
- Release shared `integration`: passed (1 test).
- Installed-package consumer: passed.

The final benchmark measurement is appended after the final benchmark run.

Final benchmark observation on this host:

- SdfLib Gear fixture: 6,882 triangles, 1,024 deterministic query points.
- BVH versus exhaustive maximum scalar error: `0`.
- BVH time: `0.0105957 s`; exhaustive time: `0.51345 s`.
- Observed speedup: `48.4583x`. This is an observation, not a pass threshold or
  a portable performance guarantee.

Final CLI smoke checks generated, loaded, and queried dense trilinear,
adaptive tricubic NSM, and exact influence octree assets successfully.
The headless visualizer generated all five diagnostic modes, and the README
image pipeline completed for the PyCoCo sphere, Nagata cone, and SdfLib Gear.
