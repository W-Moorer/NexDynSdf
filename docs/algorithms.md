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
  that can define the nearest surface. The current filter uses an AABB lower
  bound against a center-distance-plus-cell-radius Lipschitz upper bound. Leaf
  queries remain exact and return witness/feature data.
- `AdaptivePiecewiseOctree`: leaves store trilinear or tricubic polynomials and
  subdivide when the weighted 19-point trapezoidal RMS estimate exceeds the
  requested tolerance. The maximum absolute error at those probes is recorded
  separately. Fine-leaf boundary jets are restricted from the coarser adjacent
  polynomial; tests cover C0 trilinear and C1 tricubic coarse/fine interfaces.

Representation and reconstruction are separate configuration axes. Exact
influence octrees do not use interpolation.

## Error semantics

`measured_leaf_error` is available only for adaptive assets and is the maximum
absolute error observed at the 19 probe points of the selected leaf. It is not
a mathematically certified error bound over every point in the leaf. Dense and
exact assets set `has_measured_error=false`.

## Known numerical boundaries

- A signed field requires a finite, non-degenerate, closed, consistently
  oriented, single-component two-manifold after exact duplicate-coordinate
  welding. Multiple shells are rejected because their union/cavity semantics
  cannot be inferred from local pseudo-normal sign alone.
- The signed distance is non-differentiable on medial axes and at feature
  branch changes. `feature` and `branch_signature` expose these cases.
- Gradient Taylor is piecewise first order and is not continuous across cell
  boundaries.
