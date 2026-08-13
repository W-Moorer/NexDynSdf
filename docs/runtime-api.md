# Runtime API

## Recommended NexDyn boundary

Use `include/nexsdf/c_api.h` across module or DLL boundaries. It contains only
C scalar types, fixed-layout structs, and an opaque `nexsdf_asset` handle.
No C++ exception crosses this boundary.

Typical lifetime:

```c
nexsdf_asset* asset = NULL;
if (nexsdf_asset_open("part.nsdf", &asset) != NEXSDF_STATUS_OK) {
    /* nexsdf_last_error() is thread-local diagnostic text. */
}

double point[3] = {x, y, z};
nexsdf_query_result result;
nexsdf_status status = nexsdf_query(asset, point, &result);
if (status == NEXSDF_STATUS_OK &&
    (result.flags & NEXSDF_QUERY_VALID) != 0) {
    /* result.phi, result.raw_gradient, result.unit_normal */
}

nexsdf_asset_close(asset);
```

## Query fields

- `phi`: selected representation/reconstruction value in input length units.
- `raw_gradient`: derivative of that same scalar function.
- `unit_normal`: normalized `raw_gradient` when its norm is nonzero.
- `hessian`: analytic reconstruction Hessian when
  `NEXSDF_QUERY_HAS_HESSIAN` is set.
- `witness`, `face_id`, `feature`: exact-query data when
  `NEXSDF_QUERY_HAS_WITNESS` is set.
- `measured_leaf_error`: adaptive leaf's maximum error at its 19 independent
  probes, only when `NEXSDF_QUERY_HAS_MEASURED_ERROR` is set.
- `branch_signature`: deterministic local branch identifier. It can be used to
  notice feature/leaf changes, but it is not a persistent object identifier.
- `cell_depth`: octree leaf depth; zero for dense assets.

Imported corner normals are retained as mesh metadata in exact assets. They do
not replace the SDF derivative. Exact sign and non-smooth feature handling use
geometric face/edge/vertex pseudo-normals.

At medial axes or symmetric centers the SDF is not differentiable. Approximate
reconstructions may return a zero raw gradient there; callers must not assume a
unit normal exists merely because the scalar value is valid.

## Batch behavior

`nexsdf_query_batch` accepts interleaved points through
`xyz_stride_bytes`. Results are tightly packed. If any point is outside the
asset domain, the function returns `NEXSDF_STATUS_OUT_OF_DOMAIN`, while every
result still carries its own flags so the caller can identify valid entries.

## Threading

Loaded assets are immutable. Read-only queries use local traversal state and
can be issued concurrently against one handle. Handle destruction must be
externally synchronized with outstanding queries. `nexsdf_last_error()` is
thread-local and is overwritten by the next C API call on that thread.

## Status mapping

- `INVALID_ARGUMENT`: null pointers, invalid stride, or incompatible options.
- `IO_ERROR`: file open/read/write failure.
- `INVALID_FORMAT`: unsupported non-NSDF input format at the API boundary.
- `OUT_OF_DOMAIN`: query coordinate outside the baked domain.
- `CORRUPT_ASSET`: checksum, count table, numeric field, payload, or tree
  validation failure.
- `INTERNAL_ERROR`: allocation failure or unexpected runtime failure.

## Last verified against

Date: 2026-08-13.

Sources: `include/nexsdf/c_api.h`, `src/c_api.cpp`, `src/asset.cpp`.

Tests: `tests/c_api_tests.c`, `tests/unit_tests.cpp`,
`tests/install_consumer/main.c`.

Commands: Release static/shared unit suites and installed-package consumer
passed in this session.
