#!/usr/bin/env python3
"""Rebuild the first-party cam/gear fixtures without third-party packages.

The profiles and dimensions come from SDFmodel's multibody_benchmark.py.  Its
experimental cap construction referenced the lower ring from the upper cap,
so this maintained generator closes the rings with modular indexing and emits
explicit surface face identifiers and per-corner normals.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct
import sys
import tempfile


GENERATOR_VERSION = "nexsdf-reference-models-v1"


def normalize(vector: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(sum(value * value for value in vector))
    if not length:
        raise ValueError("zero-length normal")
    return tuple(value / length for value in vector)  # type: ignore[return-value]


def subtract(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def smooth_profile_normals(profile):
    result = []
    count = len(profile)
    for index in range(count):
        previous = profile[(index - 1) % count]
        point = profile[index]
        following = profile[(index + 1) % count]
        incoming = normalize((point[1] - previous[1], previous[0] - point[0], 0.0))
        outgoing = normalize((following[1] - point[1], point[0] - following[0], 0.0))
        result.append(normalize(tuple(incoming[i] + outgoing[i] for i in range(3))))
    return result


def extrude(profile, thickness: float, smooth_sides: bool):
    count = len(profile)
    low, high = -0.5 * thickness, 0.5 * thickness
    vertices = [(x, y, low) for x, y in profile]
    vertices += [(x, y, high) for x, y in profile]
    center_x = sum(x for x, _ in profile) / count
    center_y = sum(y for _, y in profile) / count
    bottom_center = len(vertices)
    vertices.append((center_x, center_y, low))
    top_center = len(vertices)
    vertices.append((center_x, center_y, high))

    triangles = []
    face_ids = []
    normals = []
    smooth = smooth_profile_normals(profile) if smooth_sides else None
    for index in range(count):
        following = (index + 1) % count
        triangles.append((bottom_center, following, index))
        face_ids.append(0)
        normals.append(((0.0, 0.0, -1.0),) * 3)

        triangles.append((top_center, count + index, count + following))
        face_ids.append(1)
        normals.append(((0.0, 0.0, 1.0),) * 3)

        first = (index, following, count + following)
        second = (index, count + following, count + index)
        triangles.extend((first, second))
        face_ids.extend((2 + index, 2 + index))
        if smooth is not None:
            normals.extend((
                (smooth[index], smooth[following], smooth[following]),
                (smooth[index], smooth[following], smooth[index]),
            ))
        else:
            x0, y0 = profile[index]
            x1, y1 = profile[following]
            side = normalize((y1 - y0, x0 - x1, 0.0))
            normals.extend(((side, side, side), (side, side, side)))
    return vertices, triangles, face_ids, normals


def cam():
    radius, eccentricity, segments, thickness = 1.0, 0.2, 60, 0.4
    profile = []
    for index in range(segments):
        theta = 2.0 * math.pi * index / segments
        radial = radius + eccentricity * math.cos(theta)
        profile.append((radial * math.cos(theta), radial * math.sin(theta)))
    return extrude(profile, thickness, True)


def gear():
    teeth, outer, root, thickness = 12, 1.0, 0.78, 0.4
    profile = []
    pitch = 2.0 * math.pi / teeth
    for index in range(teeth):
        theta = index * pitch
        profile.append((outer * math.cos(theta), outer * math.sin(theta)))
        root_theta = theta + 0.5 * pitch
        profile.append((root * math.cos(root_theta), root * math.sin(root_theta)))
    return extrude(profile, thickness, False)


def nsm_bytes(model) -> bytes:
    vertices, triangles, face_ids, normals = model
    output = bytearray(b"NSM\x00")
    output += struct.pack("<III", 1, len(vertices), len(triangles))
    output += bytes(48)
    for vertex in vertices:
        output += struct.pack("<ddd", *vertex)
    for triangle in triangles:
        output += struct.pack("<III", *triangle)
    for face_id in face_ids:
        output += struct.pack("<I", face_id)
    for triangle_normals in normals:
        for normal in triangle_normals:
            output += struct.pack("<ddd", *normal)
    return bytes(output)


def binary_stl_bytes(model, name: str) -> bytes:
    vertices, triangles, _, _ = model
    header = (f"NexDynSdf {GENERATOR_VERSION} {name}").encode("ascii")[:80]
    output = bytearray(header.ljust(80, b"\x00"))
    output += struct.pack("<I", len(triangles))
    for triangle in triangles:
        a, b, c = (vertices[index] for index in triangle)
        normal = normalize(cross(subtract(b, a), subtract(c, a)))
        output += struct.pack("<fff", *normal)
        for vertex in (a, b, c):
            output += struct.pack("<fff", *vertex)
        output += struct.pack("<H", 0)
    return bytes(output)


def products():
    cam_model = cam()
    return {
        "cam.nsm": nsm_bytes(cam_model),
        "cam.stl": binary_stl_bytes(cam_model, "cam"),
        "gear.nsm": nsm_bytes(gear()),
    }


def write(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for name, content in products().items():
        (output / name).write_bytes(content)


def check(output: Path) -> bool:
    valid = True
    for name, expected in products().items():
        path = output / name
        if not path.is_file() or path.read_bytes() != expected:
            print(f"stale generated model: {path}", file=sys.stderr)
            valid = False
    return valid


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    if arguments.check:
        return 0 if check(arguments.output_dir) else 1
    write(arguments.output_dir)
    print(f"wrote {len(products())} models using {GENERATOR_VERSION}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
