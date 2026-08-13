#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace nexsdf
{
namespace
{

struct ObjCorner
{
    std::uint32_t vertex{0};
    std::int64_t normal{-1};
};

std::string trim_comment(std::string line)
{
    const std::size_t comment = line.find('#');
    if (comment != std::string::npos)
    {
        line.erase(comment);
    }
    const std::size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const std::size_t last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1);
}

std::uint32_t obj_index(
    const std::string& token,
    std::size_t count,
    const std::string& path,
    std::size_t line)
{
    if (token.empty())
    {
        throw std::runtime_error(path + ":" + std::to_string(line) +
            ": empty OBJ index");
    }
    const long long raw = std::stoll(token);
    if (raw == 0)
    {
        throw std::runtime_error(path + ":" + std::to_string(line) +
            ": OBJ indices are one-based and cannot be zero");
    }
    const long long resolved = raw > 0 ? raw - 1 :
        static_cast<long long>(count) + raw;
    if (resolved < 0 || resolved >= static_cast<long long>(count))
    {
        throw std::runtime_error(path + ":" + std::to_string(line) +
            ": OBJ index is out of range");
    }
    return static_cast<std::uint32_t>(resolved);
}

ObjCorner parse_corner(
    const std::string& token,
    std::size_t vertex_count,
    std::size_t normal_count,
    const std::string& path,
    std::size_t line)
{
    ObjCorner result;
    const std::size_t first = token.find('/');
    result.vertex = obj_index(
        token.substr(0, first), vertex_count, path, line);
    if (first == std::string::npos)
    {
        return result;
    }
    const std::size_t second = token.find('/', first + 1);
    if (second == std::string::npos)
    {
        return result;
    }
    const std::string normal = token.substr(second + 1);
    if (!normal.empty())
    {
        result.normal = static_cast<std::int64_t>(
            obj_index(normal, normal_count, path, line));
    }
    return result;
}

std::uint32_t read_u32_le(std::istream& stream, const char* label)
{
    std::array<std::uint8_t, 4> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!stream)
    {
        throw std::runtime_error(std::string("truncated NSM while reading ") + label);
    }
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

double read_f64_le(std::istream& stream, const char* label)
{
    std::uint64_t bits = 0;
    for (unsigned byte = 0; byte < 8; ++byte)
    {
        const int value = stream.get();
        if (value == std::char_traits<char>::eof())
        {
            throw std::runtime_error(std::string("truncated NSM while reading ") + label);
        }
        bits |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(value)) << (8u * byte);
    }
    double value = 0.0;
    static_assert(sizeof(value) == sizeof(bits), "unexpected double size");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void require_reasonable_count(std::uint32_t count, const char* label)
{
    constexpr std::uint32_t limit = 100000000u;
    if (count == 0 || count > limit)
    {
        throw std::runtime_error(std::string("invalid NSM ") + label + " count");
    }
}

} // namespace

SurfaceMesh load_obj(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("cannot open OBJ file: " + path);
    }

    SurfaceMesh mesh;
    mesh.source_path = path;
    std::vector<Vec3> normals;
    std::string line_text;
    std::size_t line_number = 0;
    std::uint32_t next_face_id = 0;
    while (std::getline(input, line_text))
    {
        ++line_number;
        line_text = trim_comment(std::move(line_text));
        if (line_text.empty())
        {
            continue;
        }
        std::istringstream line(line_text);
        std::string keyword;
        line >> keyword;
        if (keyword == "v")
        {
            Vec3 vertex;
            if (!(line >> vertex.x >> vertex.y >> vertex.z))
            {
                throw std::runtime_error(path + ":" +
                    std::to_string(line_number) + ": malformed vertex");
            }
            mesh.vertices.push_back(vertex);
        }
        else if (keyword == "vn")
        {
            Vec3 normal;
            if (!(line >> normal.x >> normal.y >> normal.z) ||
                norm(normal) <= 1.0e-14)
            {
                throw std::runtime_error(path + ":" +
                    std::to_string(line_number) + ": malformed normal");
            }
            normals.push_back(normalized(normal));
        }
        else if (keyword == "f")
        {
            std::vector<ObjCorner> polygon;
            std::string corner_text;
            while (line >> corner_text)
            {
                polygon.push_back(parse_corner(
                    corner_text,
                    mesh.vertices.size(),
                    normals.size(),
                    path,
                    line_number));
            }
            if (polygon.size() < 3)
            {
                throw std::runtime_error(path + ":" +
                    std::to_string(line_number) + ": face has fewer than three corners");
            }
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
            {
                const std::array<ObjCorner, 3> corners{
                    polygon[0], polygon[i], polygon[i + 1]};
                Triangle triangle;
                triangle.face_id = next_face_id;
                triangle.has_corner_normals = true;
                for (std::size_t c = 0; c < 3; ++c)
                {
                    triangle.vertex[c] = corners[c].vertex;
                    if (corners[c].normal < 0)
                    {
                        triangle.has_corner_normals = false;
                    }
                    else
                    {
                        triangle.corner_normal[c] =
                            normals[static_cast<std::size_t>(corners[c].normal)];
                    }
                }
                mesh.triangles.push_back(triangle);
            }
            ++next_face_id;
        }
    }
    if (mesh.vertices.empty() || mesh.triangles.empty())
    {
        throw std::runtime_error("OBJ contains no triangle mesh: " + path);
    }
    return mesh;
}

SurfaceMesh load_nsm_v1(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("cannot open NSM file: " + path);
    }

    std::array<char, 64> header{};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!input || std::memcmp(header.data(), "NSM\0", 4) != 0)
    {
        throw std::runtime_error("invalid NSM v1 header: " + path);
    }
    std::uint32_t version = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    const auto header_u32 = [&header](std::size_t offset) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(header.data() + offset);
        return static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[3]) << 24u);
    };
    version = header_u32(4);
    vertex_count = header_u32(8);
    triangle_count = header_u32(12);
    if (version != 1)
    {
        throw std::runtime_error("unsupported NSM version: " + std::to_string(version));
    }
    require_reasonable_count(vertex_count, "vertex");
    require_reasonable_count(triangle_count, "triangle");

    SurfaceMesh mesh;
    mesh.source_path = path;
    mesh.vertices.resize(vertex_count);
    mesh.triangles.resize(triangle_count);
    for (Vec3& vertex : mesh.vertices)
    {
        vertex.x = read_f64_le(input, "vertex x");
        vertex.y = read_f64_le(input, "vertex y");
        vertex.z = read_f64_le(input, "vertex z");
    }
    for (Triangle& triangle : mesh.triangles)
    {
        for (std::uint32_t& index : triangle.vertex)
        {
            index = read_u32_le(input, "triangle index");
            if (index >= vertex_count)
            {
                throw std::runtime_error("NSM triangle index is out of range");
            }
        }
    }
    for (Triangle& triangle : mesh.triangles)
    {
        triangle.face_id = read_u32_le(input, "face id");
    }
    for (Triangle& triangle : mesh.triangles)
    {
        triangle.has_corner_normals = true;
        for (Vec3& normal : triangle.corner_normal)
        {
            normal.x = read_f64_le(input, "corner normal x");
            normal.y = read_f64_le(input, "corner normal y");
            normal.z = read_f64_le(input, "corner normal z");
            if (norm(normal) <= 1.0e-14 ||
                !std::isfinite(normal.x) || !std::isfinite(normal.y) ||
                !std::isfinite(normal.z))
            {
                throw std::runtime_error("NSM contains an invalid corner normal");
            }
            normal = normalized(normal);
        }
    }
    char trailing = 0;
    if (input.read(&trailing, 1))
    {
        throw std::runtime_error("NSM file contains unexpected trailing bytes");
    }
    return mesh;
}

} // namespace nexsdf
