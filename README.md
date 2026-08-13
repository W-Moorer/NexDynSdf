# NexDynSdf

NexDynSdf is a standalone signed-distance-field generator and read-only query
library. It is intentionally independent from NexDyn contact and solver code.
NexDyn can consume generated `.nsdf` assets through the stable C API or the
same-toolchain C++ wrapper. The C ABI is the integration boundary intended for
long-lived runtime compatibility.

Implemented representations and reconstructions:

- exact conservative triangle-influence octree;
- adaptive piecewise octree;
- dense regular grid;
- trilinear scalar reconstruction;
- tricubic Hermite scalar reconstruction;
- PyCoCo-compatible cell-centred gradient Taylor reconstruction.

The project accepts triangulated or polygonal OBJ files and NSM v1 binary mesh
files containing per-triangle face identifiers and per-corner normals.

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
build/Release/nexsdfgen.exe tests/models/pycoco/cube.obj out/cube.nsdf `
  --representation grid --reconstruction trilinear --resolution 48
build/Release/nexsdfinspect.exe out/cube.nsdf 2 0 0
```

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
