# Algorithms

## Exact surface reference

Unsigned distance is the minimum point-triangle distance. The offline exact
reference uses a median-split triangle BVH with AABB lower-bound pruning. Sign
is determined locally from the closest feature: face normal, adjacent-face
edge pseudo-normal, or angle-weighted vertex pseudo-normal. Mesh components
are oriented outward before pseudo-normal construction. Witness point, face
identifier, closest feature, and a deterministic branch signature are retained.

## Reconstruction axis

- `Trilinear`: eight shared scalar corner samples; gradient is the analytic
  derivative of the same polynomial.
- `TricubicHermite`: 64 corner jets formed from value, first derivatives and
  mixed derivatives. The default central-difference step is one quarter of the
  local cell width; `derivative_step` can specify a world-unit step. Gradient
  and Hessian are analytic derivatives of the same polynomial.
- `GradientTaylor`: one cell-centre value and unit gradient, evaluated as
  `phi_j + dot(x-x_j, g_j)` to match PyCoCo's query definition.

## Representation axis

- `DenseGrid`: shared regular-grid samples. Trilinear stores one scalar per
  grid vertex; tricubic stores one eight-component jet per grid vertex;
  Gradient Taylor stores one value/unit-gradient tuple per cell centre.
- `ExactInfluenceOctree`: leaves retain a conservative superset of triangles
  that can define the nearest surface. Three selectable filters are available:
  `AabbLipschitz`, `PaperGjk`, and `PaperFrankWolfe`. Leaf queries remain exact
  and return witness/feature data.
- `AdaptivePiecewiseOctree`: leaves store trilinear or tricubic polynomials and
  subdivide when the weighted 19-point trapezoidal RMS estimate exceeds the
  requested tolerance. The maximum absolute error at those probes is recorded
  separately. Fine-leaf boundary jets are restricted from the coarser adjacent
  polynomial; tests cover C0 trilinear and C1 tricubic coarse/fine interfaces.

Representation and reconstruction are separate configuration axes. Exact
influence octrees do not use interpolation.

## Execution backends

`CpuScalar` is the offline reference backend. `CpuParallel` divides dense-grid
samples, adaptive error probes, and upper exact-octree child filters into fixed
contiguous partitions. Workers own disjoint output slots; topology and child
arrays are merged in their original serial order after all workers join. This
keeps floating-point evaluation order local to each sample and makes repeated
one-thread/multi-thread assets numerically identical apart from recorded
backend provenance.

`AutoSimd` batch queries use SSE2 two-lane interpolation for dense trilinear
assets and paired AABB pruning for exact-surface BVH queries. Exact feature
ties are confirmed through the scalar ordering, so the returned face, witness,
feature, and branch signature remain deterministic. Disabling
`NEXSDF_ENABLE_SIMD` makes `AutoSimd` fall back to the scalar batch path.
The public batch default is scalar; `AutoSimd` must be selected explicitly.

The optional `CudaExperimental` backend uses double-precision brute-force
triangle sampling plus winding-number sign for single-shell dense trilinear
generation, and a CUDA reconstruction kernel for compatible batch queries.
CPU topology validation, domain construction, branch provenance, and `.nsdf`
serialization remain authoritative. Each query currently includes device
allocation and host/device transfer; this is an experimental conformance path,
not an assertion that small batches are faster on a GPU.

## Exact triangle influence filters

The reference `AabbLipschitz` filter compares each triangle AABB lower bound
with `abs(phi(center)) + half_diagonal`. The two paper filters implement the
Pujol-Chica construction. For the closest triangle `Tc` at a selected cell
corner, eight spheres are centered at the cell corners and have radii equal to
their unsigned distance to `Tc`. Their convex hull is a superset of every point
that can be closer to some point in the cell than `Tc`. A candidate `Tf` may
therefore be rejected only when it provably does not intersect that convex
hull.

`PaperGjk` uses support maps for the sphere hull, candidate triangle, and their
Minkowski difference. A candidate is rejected only on a negative support-plane
certificate. `PaperFrankWolfe` erodes all sphere radii by their minimum, then
minimizes distance to the resulting Minkowski difference; it rejects only when
the support point supplies a separating axis outside the minimum-radius sphere.
Termination margins are `256 * epsilon * geometry_scale`. Duplicate support,
degenerate arithmetic, or iteration exhaustion retain the triangle. These
choices may reduce filtering efficiency but cannot create an incorrect
discard.

Like the paper's reported 1-corner heuristic, the reference triangle is the
nearest triangle at the cell corner closest to the candidate centroid. Using a
subset of reference triangles can retain redundant candidates; it does not
invalidate a rejection certified against the selected reference.

## Error semantics

`measured_leaf_error` is available only for adaptive assets and is the maximum
absolute error observed at the 19 probe points of the selected leaf. It is not
a mathematically certified error bound over every point in the leaf. Dense and
exact assets set `has_measured_error=false`.

## Multi-component composition

`SeparateAssets` preserves the original single-component contract and rejects
multi-component input. `SolidUnion` accepts disjoint closed components and
removes every shell contained by another component from the admissible nearest
boundary set; the sign is negative inside any active outer component.
`NestedParity` retains every shell, counts component containments, and is
negative at odd depth. Its gradient is recomputed from the witness and final
global sign, so an inner cavity normal points from material into the positive
cavity independently of imported winding.

The single-shell `SeparateAssets` path derives sign directly from the closest
feature's face, edge, or vertex pseudo-normal. Global solid-angle containment
is reserved for explicit multi-shell composition, where a local closest-feature
sign cannot express union or cavity semantics. Thus ordinary exact queries keep
BVH query complexity without weakening the multi-shell sign rule.

Construction builds a component containment graph after per-component outward
orientation. Component pairs are first AABB-pruned and then tested by
triangle-triangle separating axes, including coplanar in-plane axes. Surface
intersection or touching, cyclic containment, and a representative point whose
two normal offsets disagree all fail closed as ambiguous. The tolerances are
derived from machine epsilon and the mesh bounding-box scale.

## Known numerical boundaries

- A signed field requires a finite, non-degenerate, closed, consistently
  oriented two-manifold after exact duplicate-coordinate welding. Multiple
  shells require an explicit `SolidUnion` or `NestedParity` policy.
- The signed distance is non-differentiable on medial axes and at feature
  branch changes. `feature` and `branch_signature` expose these cases.
- Gradient Taylor is piecewise first order and is not continuous across cell
  boundaries.
- The CUDA generator is deliberately limited to dense trilinear,
  single-component assets and scales as grid samples times triangle count.
