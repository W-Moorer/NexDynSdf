# NexDynSdf development roadmap

## Scope

This page records planned work only. It does not describe currently implemented
behavior. The current implementation and verified limitations remain defined by
`docs/architecture.md`, `docs/algorithms.md`, `wiki/SDFBackend.md`, and the
tests.

The work is ordered so that physical and numerical correctness gates precede
performance optimization. A milestone is complete only when its stated tests
and documentation are committed; benchmark improvement alone is not an
acceptance criterion.

## M0: frozen comparison and validation harness (implemented)

Build the quantitative harness before changing algorithms so later results can
be compared against the current implementation.

- Use analytic primitives plus the runtime-ready sphere, cone, cam, and Gear
  catalog models.
- Evaluate dense resolutions 32, 64, 128, and 256 when memory permits, and
  define equivalent depth/tolerance sweeps for both octree representations.
- Measure signed-distance absolute/RMS/percentile error against `ExactSurface`,
  raw-gradient error, unit-normal angular error, Eikonal residual, serialized
  bytes, peak construction memory, build time, and warmed batch-query time.
- Measure zero-isosurface error in both directions. Report sampled symmetric
  Hausdorff and percentile surface distance with the sampling density and
  triangle-interior policy; do not label a one-direction vertex metric as
  Hausdorff distance.
- Record compiler, configuration, CPU/GPU, thread count, model hash, options,
  random seed, warm-up, repetitions, and aggregation rule in machine-readable
  output.

Acceptance requires analytic sign/gradient tests, reproducible output schemas,
and a regression that rejects missing directions, missing reference samples, or
non-finite metrics.

Implementation: `tools/nexsdfvalidate.cpp`,
`scripts/run_validation_matrix.ps1`, `tests/validation_smoke.cmake`, and
`docs/validation.md`. The full matrix remains an explicit, machine-dependent
benchmark run rather than a normal regression test.

## M1: paper-faithful exact influence filtering (implemented)

Introduce a selectable convex-hull support mapping and GJK/Frank-Wolfe
intersection implementation following Pujol and Chica alongside the current
AABB/Lipschitz candidate filter. Promote the paper-faithful mode only after
equivalence is established; retain the current filter as a named reference
mode during validation.

- Express support functions and termination tolerances from geometry scale and
  floating-point error bounds, not model-specific tuning constants.
- Prove in code comments and documentation why a rejected triangle cannot be
  nearest anywhere in the node.
- Compare retained triangle sets per leaf, asset size, construction time, and
  query time with the conservative reference mode.
- Require exact-octree distance, sign, witness, feature, and branch results to
  agree with exhaustive `ExactSurface` queries at deterministic interior,
  boundary, feature, and randomized points.

Acceptance requires zero incorrect discards in the test corpus and no weakening
of corruption, topology, or out-of-domain checks.

Implementation: `src/influence.cpp` plus selectable `InfluenceFilter` build
metadata, NSDF 1.1 serialization, a size-versioned C provenance query,
`tests/influence_tests.cpp`, and `scripts/run_influence_comparison.ps1`.
The AABB/Lipschitz path remains the default reference until a future API policy
change explicitly promotes another filter.

## M2: explicit multi-component and cavity semantics (implemented)

Do not infer the meaning of disconnected or nested shells silently. Extend the
build API and asset metadata with an explicit composition policy, initially:

- separate assets, preserving current behavior;
- solid union of closed components;
- parity-oriented nested shells for explicitly declared cavities.

Construct a component containment graph, validate shell intersections, and
define distance/sign behavior at coincident, touching, or ambiguous shells.
Pseudo-normal sign remains local to a surface feature; global composition
selects the final sign and nearest admissible boundary.

Acceptance requires analytic disjoint-sphere, nested-sphere, shell-with-cavity,
and intersecting-shell fixtures, serialization round trips, C API metadata, and
documented failure for ambiguous input. This milestone is the prerequisite for
making `SlidewayRotatingModel.obj` runtime-ready as one composed asset.

Implementation: explicit `CompositionPolicy`, a containment graph, shell
intersection/touch rejection, union boundary elimination, parity sign/gradient,
NSDF 1.2 metadata, C provenance fields, and analytic disjoint/nested/intersecting
fixtures in `tests/unit_tests.cpp`. Catalog models are not automatically assigned
a policy; their physical meaning still requires an explicit caller choice.

## M3: source-model repair and input-format decision

Regenerate `sdfmodel/cam.nsm` and `sdfmodel/gear.nsm` from their originating
scripts or source surfaces. Prefer reproducible regeneration over destructive
mesh repair.

- Preserve face identifiers and corner normals in NSM output.
- Require finite, non-degenerate, consistently oriented closed two-manifolds
  with zero boundary and non-manifold edges after the documented weld policy.
- Compare regenerated geometry to its source using the M0 bidirectional surface
  metrics and record generator revision and parameters in `models/CATALOG.tsv`.

Format work has a separate decision gate:

- STL may become a mesh input only with explicit ASCII/binary parsing,
  deterministic duplicate welding, topology validation, and a documented
  normal/orientation policy.
- ENG is currently an Enhanced Nagata crease/cache sidecar, not a standalone
  surface. It should become a supported sidecar only after its versioned schema,
  ownership of geometry, and relation to NSM are documented; otherwise it
  remains catalogued auxiliary data.

Acceptance requires parser fuzz/corruption tests, round-trip fixtures where the
format permits them, and no path that bypasses signed-distance topology checks.

## M4: deterministic CPU parallelism and SIMD

Optimize only after M0 provides correctness and resource measurements.

- Parallelize independent exact samples and octree child construction using a
  deterministic partition and merge order.
- Add SIMD-friendly batch BVH traversal and reconstruction evaluation without
  changing the public batch-query contract.
- Keep a scalar reference path and test scalar/parallel/SIMD outputs for the
  representation-appropriate exact or bounded equivalence.
- Expose thread/backend configuration in offline build options and asset
  provenance; do not place mutable global state in read-only runtime queries.

Acceptance requires one-thread versus multi-thread repeatability, ThreadSanitizer
or an available equivalent race audit, unchanged physical sign/domain behavior,
and performance results from the M0 protocol rather than a single tuned model.

## M5: optional GPU generation and query experiments

Treat GPU support as an optional backend, not a dependency of the stable C ABI.

- Start with batched exact sampling and dense-grid generation; keep CPU mesh
  validation and asset serialization authoritative.
- Define precision, device capability, deterministic mode, transfer cost, and
  CPU fallback behavior explicitly.
- Reuse the `.nsdf` conformance reader and public query validation so a GPU path
  cannot introduce a second undocumented asset format.

Acceptance requires CPU/GPU comparison over the full M0 matrix, explicit error
budgets, corruption-safe outputs, and benchmarks that include host/device
transfer and compilation/warm-up policy.

## Delivery order

1. M0 validation harness and baseline archive.
2. M1 paper-faithful exact influence filter.
3. M2 explicit multi-shell composition semantics.
4. M3 NSM regeneration and STL/ENG decision.
5. M4 deterministic CPU parallel/SIMD paths.
6. M5 optional GPU experiments.

Each milestone must update `README.md`, `docs/algorithms.md`, the relevant API
and asset-format documentation, `wiki/SDFBackend.md`, and
`wiki/TestsAndValidation.md` only after the behavior is implemented and tested.

## Last verified against

Date: 2026-08-13.

Sources used to define the current/planned boundary:

- `src/geometry.cpp`, `src/reconstruction.cpp`, `src/asset.cpp`,
  `src/mesh_io.cpp`
- `include/nexsdf/nexsdf.hpp`, `include/nexsdf/c_api.h`
- `models/AUDIT.tsv`, `models/CATALOG.tsv`
- `docs/architecture.md`, `docs/algorithms.md`,
  `docs/reference-provenance.md`

Tests and validation used as the existing baseline:

- `tests/unit_tests.cpp`, `tests/model_tests.cpp`,
  `tests/model_catalog_smoke.cmake`, `tests/benchmark_exact.cpp`
- `scripts/run_tests.ps1 -Configuration Release` passed on 2026-08-13 before
  this documentation-only roadmap was added.
