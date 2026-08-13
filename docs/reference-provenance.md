# Reference provenance

## Source implementations

The implementation was developed by adapting documented algorithms and data
contracts from the following local reference repositories. Their complete
trees are not vendored.

| Reference | Reviewed revision | Reused concept | Rights status in this repository |
|---|---|---|---|
| `E:/workspace/PyCoCoFastSDF` | `74155fa32704` | cell-centred first-order Taylor query, sparse block terminology, OBJ regression models | repository owner's prior first-party work; no separate third-party license |
| `E:/workspace/SdfLib` | `8db373e` | exact triangle-influence octree, adaptive polynomial octree, trilinear and tricubic Hermite data layout, Gear model | third-party MIT, Eduard Pujol |
| `E:/workspace/enhanced-nagata-sdf` | `3443478` plus inspected local NSM data | NSM v1 binary layout and per-corner-normal regression models | repository owner's prior first-party work; no separate third-party license |
| `E:/workspace/SDFmodel` | `e512f4692229` plus inspected generated outputs | scripts that generated the retained cam, gear, validation NSM, and cam STL assets | repository owner's prior first-party work; no separate third-party license |

The visualization review additionally covered SdfLib's tracked OpenGL viewers,
plane shaders, and compute-shader ray marcher at `8db373ef71d6`, plus the local
SdfLib worktree's untracked sampler and Python Marching Cubes/Plotly files. The
reviewed PyCoCoFastSDF zero-isosurface, off-screen batch screenshot,
normal-error heatmap, and surface-distance scripts are tracked at
`74155fa32704`. The resulting NexDynSdf visualizer is an independent
implementation that uses the public query API; no OpenGL/VTK/PyVista code is
vendored or linked. Detailed findings and exact reviewed files are in
`docs/visualization.md`.

All 34 persisted model assets found in the reviewed source locations are
collected under `models/<source>/`. `models/CATALOG.tsv` records the full source
revision, tracked/generated state, original path, byte count, and SHA-256;
`models/AUDIT.tsv` records the production-loader and topology audit. The
PyCoCoFastSDF, enhanced-nagata-sdf, and SDFmodel assets are prior work of the
NexDynSdf repository owner and are treated as first-party sources. Only the
copied SdfLib Gear model requires a separate third-party license notice, kept
at `models/licenses/SdfLib-LICENSE.txt`.

## Papers

- Christiane Sommer, Lu Sang, David Schubert, and Daniel Cremers,
  *Gradient-SDF: A Semi-Implicit Surface Representation for 3D
  Reconstruction*, Proceedings of the IEEE/CVF Conference on Computer Vision
  and Pattern Recognition (CVPR), 6270–6279, 2022,
  [doi:10.1109/CVPR52688.2022.00618](https://doi.org/10.1109/CVPR52688.2022.00618).
- Eduard Pujol and Antonio Chica, *Triangle Influence Supersets for Fast
  Distance Computation*, Computer Graphics Forum 42(6), article e14861, 2023,
  [doi:10.1111/cgf.14861](https://doi.org/10.1111/cgf.14861).
- Eduard Pujol and Antonio Chica, *Adaptive approximation of signed distance
  fields through piecewise continuous interpolation*, Computers & Graphics
  114, 337–346, 2023,
  [doi:10.1016/j.cag.2023.06.020](https://doi.org/10.1016/j.cag.2023.06.020).

The IEEE/TUM publisher record gives the Gradient-SDF proceedings pagination as
6270–6279. The reviewed CVF open-access artifact and the local PyCoCoFastSDF
PDF display 6280–6289; the formal citation above follows the DOI-linked
publisher record.

The exact octree uses a conservative AABB/Lipschitz triangle-influence
superset. It is a deliberately weaker but still exact conservative filter than
the paper's tighter convex-hull/GJK test. This distinction is exposed in asset
metadata and must not be described as the original GJK filter.

NexDynSdf's Gradient Taylor query implements the first-order form in equations
(14)–(15) of the Gradient-SDF paper and matches the reviewed PyCoCoFastSDF
`GradientSDFVolume.taylor_query` formula. Its data-generation context differs:
NexDynSdf samples signed distance and unit gradient from a static exact triangle
mesh, whereas the paper integrates distance and gradient observations from
RGB-D input and also develops tracking and photometric bundle adjustment. The
current implementation therefore claims the local reconstruction rule, not the
paper's complete reconstruction system or sparse hashed storage architecture.
