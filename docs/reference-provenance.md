# Reference provenance

## Source implementations

The implementation was developed by adapting documented algorithms and data
contracts from the following local reference repositories. Their complete
trees are not vendored.

| Reference | Reviewed revision | Reused concept | License |
|---|---|---|---|
| `E:/workspace/PyCoCoFastSDF` | `74155fa32704` | cell-centred first-order Taylor query, sparse block terminology, OBJ regression models | MIT, Wang Lijing |
| `E:/workspace/SdfLib` | `8db373e` | exact triangle-influence octree, adaptive polynomial octree, trilinear and tricubic Hermite data layout, Gear model | MIT, Eduard Pujol |
| `E:/workspace/enhanced-nagata-sdf` | `3443478` plus inspected local NSM data | NSM v1 binary layout and per-corner-normal regression models | MIT, Enhanced Nagata SDF Project Authors |

Copied test assets retain their original names, are kept under
`tests/models/<source>/`, and are recorded in `tests/models/MANIFEST.md` with
source paths and SHA-256 hashes.

## Papers

- Eduard Pujol and Antonio Chica, *Triangle Influence Supersets for Fast
  Distance Computation*, Computer Graphics Forum 42(6), 2023,
  DOI `10.1111/cgf.14861`.
- Eduard Pujol and Antonio Chica, *Adaptive approximation of signed distance
  fields through piecewise continuous interpolation*, Computers & Graphics
  114, 2023, DOI `10.1016/j.cag.2023.06.020`.

The exact octree uses a conservative AABB/Lipschitz triangle-influence
superset. It is a deliberately weaker but still exact conservative filter than
the paper's tighter convex-hull/GJK test. This distinction is exposed in asset
metadata and must not be described as the original GJK filter.
