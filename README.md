# NexDynSdf

NexDynSdf is a standalone signed-distance-field generator and read-only query
library. It is intentionally independent from NexDyn contact and solver code.
NexDyn can consume generated `.nsdf` assets through the stable C API or the
same-toolchain C++ wrapper. The C ABI is the integration boundary intended for
long-lived runtime compatibility.

## Reproducible results

The images below are generated from the canonical model catalog by
`scripts/generate_readme_images.ps1`. They are sampled through the same
`Asset::query_batch` path used by consumers; no private file-layout reader or
GPU shader reimplements the SDF query.

| Gradient Taylor distance/sign | Gradient Taylor unit normal |
|---|---|
| ![PyCoCo sphere Gradient Taylor signed-distance slice](docs/images/pycoco-sphere-gradient-distance.png) | ![PyCoCo sphere Gradient Taylor unit-normal slice](docs/images/pycoco-sphere-gradient-normal.png) |

The PyCoCoFastSDF sphere is baked to a `64^3` dense Gradient Taylor field. In
the distance view, blue is negative (inside), red is positive (outside), and
the dark line is the sampled zero crossing. The normal view maps normalized
`x/y/z` gradient components to RGB.

| Adaptive tricubic leaf depth | Adaptive 19-probe measured error |
|---|---|
| ![Nagata cone adaptive-octree depth slice](docs/images/nagata-cone-adaptive-depth.png) | ![Nagata cone adaptive-octree measured-error slice](docs/images/nagata-cone-adaptive-error.png) |

The NSM cone fixture retains its per-corner normals. These diagnostic slices
show where the adaptive tricubic tree refines and the per-leaf maximum error
measured at its 19 independent probes. The latter is not a certified
whole-cell bound.

![SdfLib Gear exact influence-octree signed-distance slice](docs/images/sdflib-gear-exact-distance.png)

The SdfLib Gear fixture is evaluated by the exact conservative
triangle-influence octree (`6,882` source triangles). See
[`docs/visualization.md`](docs/visualization.md) for the reference-module
review, rendering semantics, exact generation settings, and limitations.

Implemented representations and reconstructions:

- exact conservative triangle-influence octree;
- adaptive piecewise octree;
- dense regular grid;
- trilinear scalar reconstruction;
- tricubic Hermite scalar reconstruction;
- PyCoCo-compatible cell-centred gradient Taylor reconstruction.

The project accepts triangulated or polygonal OBJ files and NSM v1 binary mesh
files containing per-triangle face identifiers and per-corner normals.

## Models

All example and validation models are consolidated under [`models/`](models/):

| Directory | Contents | Ownership |
|---|---|---|
| `models/pycoco/` | cube and sphere OBJ | repository owner's prior first-party work; no separate license file |
| `models/nagata/` | box, sphere, and cone NSM with per-corner normals | repository owner's prior first-party work; no separate license file |
| `models/sdflib/` | Gear OBJ | third-party SdfLib model; MIT license retained |

The exact revisions, original paths, hashes, and regression roles are recorded
in [`models/MANIFEST.md`](models/MANIFEST.md). The only model-specific
third-party license is [`models/licenses/SdfLib-LICENSE.txt`](models/licenses/SdfLib-LICENSE.txt).

## Build

```powershell
cmake -S . -B build -DNEXSDF_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release -L unit --output-on-failure
ctest --test-dir build -C Release -L regression --output-on-failure
```

The canonical PowerShell entry point builds and runs the labeled suites:

```powershell
scripts/run_tests.ps1 -Configuration Release
scripts/run_tests.ps1 -Configuration Release -Shared
scripts/verify_install.ps1 -Configuration Release
```

## Generate and inspect

```powershell
build/Release/nexsdfgen.exe models/pycoco/cube.obj out/cube.nsdf `
  --representation grid --reconstruction trilinear --resolution 48
build/Release/nexsdfinspect.exe out/cube.nsdf 2 0 0
```

Generate a headless diagnostic slice and convert it to PNG without third-party
Python packages:

```powershell
build/Release/nexsdfviz.exe out/cube.nsdf out/cube.ppm `
  --axis z --mode distance --resolution 512
python scripts/ppm_to_png.py out/cube.ppm out/cube.png

# Rebuild every committed README image from the model catalog.
scripts/generate_readme_images.ps1 -Configuration Release
```

`nexsdfviz` supports `distance`, `normal`, `gradient-error`, `depth`, and
`error` modes. It is an offline tool and is not part of the runtime ABI.

Valid representation/reconstruction pairs are:

| Representation | Reconstruction |
|---|---|
| `grid` | `trilinear`, `tricubic`, `gradient` |
| `exact-octree` | `exact` |
| `adaptive-octree` | `trilinear`, `tricubic` |

The adaptive `--tolerance` is the 19-point trapezoidal RMS termination
threshold. Query metadata separately exposes the maximum absolute error at
those 19 probes; this measurement is not a proven whole-cell error bound.

## Consume from NexDyn

As a subproject:

```cmake
add_subdirectory(path/to/NexDynSdf)
target_link_libraries(nexdyn_target PRIVATE NexSdf::Query)
```

Or after `cmake --install`:

```cmake
find_package(NexDynSdf 0.1 REQUIRED CONFIG)
target_link_libraries(nexdyn_target PRIVATE NexSdf::Query)
```

On Windows, static-library consumers must use the same MSVC runtime library.
For a stable cross-module boundary, build shared and use `nexsdf/c_api.h`.

## Sign and units

- world coordinates and signed distance are expressed in the input length unit;
- outside is positive and inside is negative;
- the raw gradient is the derivative of the selected reconstruction;
- the unit normal is returned separately and never overwrites the raw gradient.
- points outside the baked domain return an explicit out-of-domain status;
  extrapolation is not performed.

See `docs/architecture.md`, `docs/algorithms.md`, and
`docs/reference-provenance.md` for implementation boundaries and attribution.
The exact runtime contract and file layout are in `docs/runtime-api.md` and
`docs/asset-format.md`.
