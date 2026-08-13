# NSDF Asset Format v1

## Scope

`.nsdf` is an explicit little-endian runtime asset. It does not serialize C++
objects, pointers, STL containers, compiler padding, or host endianness.

## Payload order

The v1 writer stores:

1. eight-byte magic `N X S D F 00 0D 0A` and format major/minor;
2. representation and reconstruction identifiers, followed in minor version 1
   by the exact influence-filter identifier;
3. domain, resolution/depth, and error metadata;
4. logical and stored node/triangle/coefficient counts;
5. optional exact mesh vertices and triangles, including face IDs and corner
   normals;
6. octree nodes;
7. exact-leaf triangle candidate indices;
8. approximate reconstruction coefficients;
9. a 64-bit FNV-1a checksum over all preceding bytes.

Approximate dense/adaptive assets omit the source mesh. Exact influence assets
retain it because exact queries return witness point, closest feature, face ID,
and pseudo-normal-derived gradient.

## Load validation

The loader validates checksum, major version, finite ordered bounds, finite
metadata, count-derived payload size, triangle indices, coefficient values,
leaf data ranges, full eight-child nodes, parent uniqueness, reachability,
acyclic topology, child depth, and representation-specific payload layout.

## Compatibility policy

The current format is 1.1. The loader remains compatible with 1.0 assets and
interprets their missing filter field as `AabbLipschitz`. Unknown major versions
and unknown newer minor versions are rejected, so the count table is never
parsed at the wrong byte offsets. Assets should be reproducibly regenerated
from source meshes when the major version changes.

## Last verified against

Date: 2026-08-13.

Sources: `src/asset.cpp`, `src/internal.hpp`.

Tests: dense/exact/adaptive round trips and checksum corruption in
`tests/unit_tests.cpp`; all-filter Gear round trips in
`tests/influence_tests.cpp`.

Commands: Release static and shared unit suites passed in this session.
