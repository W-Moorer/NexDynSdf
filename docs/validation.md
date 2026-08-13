# Quantitative validation protocol

`nexsdfvalidate` is the frozen M0 comparison harness for later algorithm and
backend changes. It builds one asset configuration, samples it against
`ExactSurface`, times warmed public batch queries, and writes one versioned TSV
row. `scripts/run_validation_matrix.ps1` applies the same protocol to a matrix
of models, reconstructions, and resolutions.

Schema `nexsdf-validation-v3` separates build and query backend/worker fields
and records depth, leaf-size, tolerance, padding, and derivative-step choices.
The writer validates an existing header before append; v1/v2 files remain
historical results and are not append-compatible with v3.

## Deterministic sample policy

- Field samples use a three-dimensional Halton sequence with bases 2, 3, and 5
  over the complete asset domain. The stored seed determines a fixed index
  offset; it is not a platform random-number-generator seed.
- Surface samples select triangles in proportion to area and then use the
  square-root barycentric transform. This samples triangle interiors rather
  than only mesh vertices.
- Mesh-to-field error is `abs(asset.phi)` at each exact mesh sample.
- Field-to-mesh error starts from the same exact sample, applies at most twelve
  Newton projections with the asset's raw gradient, and evaluates the
  projected point with `ExactSurface`.
- `symmetric_surface_max` is the maximum of both sampled directions. It is a
  sampled symmetric surface-distance estimate, not a certified continuous
  Hausdorff bound.

All scalar error families contain RMS, 95th percentile, and maximum values.
Normal error is the clamped angle in degrees; the Eikonal residual is
`abs(norm(raw_gradient) - 1)`. Query timing contains two unreported warm-up
passes and the requested number of measured repetitions.

## Resource and provenance fields

The schema records the FNV-1a-64 content hash of the source model, complete
build choices, sample counts, seed, compiler, build configuration, separate
build/query backends and worker counts, serialized asset bytes, build and
warmed-query time, and the
process peak working set. The operating-system process peak includes loader and
tool overhead; it must be compared only with otherwise equivalent one-build
processes. Timing and peak memory are observations, never correctness gates.

## Commands

```powershell
scripts/run_validation_matrix.ps1 -Profile Smoke -Configuration Release
scripts/run_validation_matrix.ps1 -Profile Full -Configuration Release
scripts/run_backend_comparison.ps1 -Configuration Release -Threads 8
scripts/run_backend_comparison.ps1 -Configuration Release -Cuda
```

`Smoke` covers cube and sphere meshes at 16 and 32 with all three dense
reconstructions. `Full` covers sphere, cone, cam, and gear with dense
trilinear/tricubic/Gradient Taylor at 32, 64, 128, and 256; exact influence
octrees at depths 4 and 6; and adaptive trilinear/tricubic octrees at
depth/tolerance pairs 6/0.01 and 8/0.0025. A full 256-cubed tricubic or Gradient
Taylor asset may require substantial memory; a resource failure is a failed
configuration and must not be silently replaced with a smaller resolution.

## Regression contract

`tests/validation_smoke.cmake` requires both surface directions, every
distance/derivative/resource/build-option column, and dense/exact/adaptive
rows. It rejects non-finite values and mismatched append schemas, then repeats
the dense run. All deterministic fields must be byte-identical; measured
timing and process peak memory are excluded from the repeatability comparison.

## Last verified against

Date: 2026-08-13.

Sources: `tools/nexsdfvalidate.cpp`, `scripts/run_validation_matrix.ps1`,
`src/asset.cpp`, `src/geometry.cpp`.

Tests and commands:

- `ctest --test-dir build-test-static-final -C Release -R nexsdf.validation
  --output-on-failure`: passed.
- `scripts/run_validation_matrix.ps1 -Profile Smoke -Configuration Release
  -OutputDirectory out-validation-m0`: passed all 12 configurations.
