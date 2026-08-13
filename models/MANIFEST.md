# Model catalog and provenance

All six validated models are collected in this directory so examples, tests,
benchmarks, and visualization scripts share one canonical path.

- `pycoco/` and `nagata/` contain prior work authored by the NexDynSdf
  repository owner and migrated unchanged from the owner's earlier projects.
  They are first-party assets and require no separate third-party license file.
- `sdflib/Gear.obj` is copied from SdfLib and remains subject to the SdfLib MIT
  license in `licenses/SdfLib-LICENSE.txt`.

| File | Source revision and path | SHA-256 | Regression role |
|---|---|---|---|
| `pycoco/cube.obj` | PyCoCoFastSDF `74155fa32704`, `obj_library/cube.obj` | `fbf9c67bb27c8f2960bfc8b4d0b7903881aa3519ae0743c160cb167a08cfcce5` | dense tessellated box, OBJ import, exact octree |
| `pycoco/sphere.obj` | PyCoCoFastSDF `74155fa32704`, `obj_library/sphere.obj` | `d2184379a421deea963af82423b97a7205219a19414b1029ad912c977caeec40` | smooth OBJ with vertex normals |
| `sdflib/Gear.obj` | SdfLib `8db373ef71d65be24badf6ae10750a932bbc223b`, `models/Gear.obj` | `b8b305c6948dcfce9a40644b87ab49154f2498840b6d00333953dbd2c756f6b9` | 6,882-triangle exact-query benchmark and parser regression |
| `nagata/box.nsm` | enhanced-nagata-sdf `3443478b1a65885866081a7d2f259aee72a09290`, `models/box.nsm` | `9e29e01b96f898a4fdf0607a7e84a290e6e552e5af6fb6bab5a4b856942ff1f1` | duplicate-coordinate seams and per-corner normals |
| `nagata/sphere.nsm` | enhanced-nagata-sdf `3443478b1a65885866081a7d2f259aee72a09290`, `models/sphere.nsm` | `f416634147c44113c6215b0daf96a43b3b25c6488bed9cc8f8b4815a8dce82a4` | smooth per-corner normals |
| `nagata/cone.nsm` | enhanced-nagata-sdf `3443478b1a65885866081a7d2f259aee72a09290`, `models/cone.nsm` | `6dd4471f130b1cc3ae56fd9e02782026b7d0c0e47706d80180bbf45a7f217549` | cone/edge corner normals |

Third-party license hash:

- `licenses/SdfLib-LICENSE.txt`: `1a7dfa83f0119f95b40fa235e4bd48773962ffb72c15efe9e5cf3c17080b6183`
