#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
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

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::uint32_t u32_at(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 4)
    {
        throw std::runtime_error("truncated binary data");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

float f32_at(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    const std::uint32_t bits = u32_at(bytes, offset);
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(bits), "unexpected float size");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

class StlBuilder
{
public:
    explicit StlBuilder(std::string path)
    {
        mesh_.source_path = std::move(path);
    }

    void add(Vec3 supplied_normal, const std::array<Vec3, 3>& positions)
    {
        for (const Vec3 point : positions)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                !std::isfinite(point.z))
            {
                throw std::runtime_error("STL contains a non-finite vertex");
            }
        }
        const Vec3 geometric = cross(
            positions[1] - positions[0], positions[2] - positions[0]);
        if (!(norm(geometric) > 1.0e-14))
        {
            throw std::runtime_error("STL contains a degenerate triangle");
        }

        Triangle triangle;
        triangle.face_id = static_cast<std::uint32_t>(mesh_.triangles.size());
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const Vec3 point = positions[corner];
            const auto key = std::make_tuple(point.x, point.y, point.z);
            const auto [entry, inserted] = vertices_.emplace(
                key, static_cast<std::uint32_t>(mesh_.vertices.size()));
            if (inserted)
            {
                mesh_.vertices.push_back(point);
            }
            triangle.vertex[corner] = entry->second;
        }

        if (std::isfinite(supplied_normal.x) &&
            std::isfinite(supplied_normal.y) &&
            std::isfinite(supplied_normal.z) &&
            norm(supplied_normal) > 1.0e-14 &&
            dot(geometric, supplied_normal) < 0.0)
        {
            std::swap(triangle.vertex[1], triangle.vertex[2]);
        }
        mesh_.triangles.push_back(triangle);
    }

    SurfaceMesh finish()
    {
        if (mesh_.triangles.empty())
        {
            throw std::runtime_error("STL contains no triangles");
        }
        return std::move(mesh_);
    }

private:
    SurfaceMesh mesh_;
    std::map<std::tuple<double, double, double>, std::uint32_t> vertices_;
};

void require_token(
    std::istream& input,
    const std::string& expected,
    const std::string& path)
{
    std::string token;
    if (!(input >> token) || lower(token) != expected)
    {
        throw std::runtime_error(
            "malformed ASCII STL (expected " + expected + "): " + path);
    }
}

SurfaceMesh load_ascii_stl(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("cannot open STL file: " + path);
    }
    require_token(input, "solid", path);
    std::string ignored_name;
    std::getline(input, ignored_name);

    StlBuilder builder(path);
    std::string token;
    while (input >> token)
    {
        token = lower(token);
        if (token == "endsolid")
        {
            std::getline(input, ignored_name);
            std::string trailing;
            if (input >> trailing)
            {
                throw std::runtime_error("ASCII STL contains trailing tokens: " + path);
            }
            return builder.finish();
        }
        if (token != "facet")
        {
            throw std::runtime_error("malformed ASCII STL (expected facet): " + path);
        }
        require_token(input, "normal", path);
        Vec3 normal;
        if (!(input >> normal.x >> normal.y >> normal.z))
        {
            throw std::runtime_error("malformed ASCII STL facet normal: " + path);
        }
        require_token(input, "outer", path);
        require_token(input, "loop", path);
        std::array<Vec3, 3> positions{};
        for (Vec3& point : positions)
        {
            require_token(input, "vertex", path);
            if (!(input >> point.x >> point.y >> point.z))
            {
                throw std::runtime_error("malformed ASCII STL vertex: " + path);
            }
        }
        require_token(input, "endloop", path);
        require_token(input, "endfacet", path);
        builder.add(normal, positions);
    }
    throw std::runtime_error("ASCII STL is missing endsolid: " + path);
}

SurfaceMesh load_binary_stl(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t triangle_count)
{
    if (triangle_count == 0 || triangle_count > 100000000u)
    {
        throw std::runtime_error("invalid binary STL triangle count");
    }
    StlBuilder builder(path);
    std::size_t offset = 84;
    for (std::uint32_t triangle = 0; triangle < triangle_count; ++triangle)
    {
        const Vec3 normal{
            static_cast<double>(f32_at(bytes, offset)),
            static_cast<double>(f32_at(bytes, offset + 4)),
            static_cast<double>(f32_at(bytes, offset + 8))};
        offset += 12;
        std::array<Vec3, 3> positions{};
        for (Vec3& point : positions)
        {
            point = {
                static_cast<double>(f32_at(bytes, offset)),
                static_cast<double>(f32_at(bytes, offset + 4)),
                static_cast<double>(f32_at(bytes, offset + 8))};
            offset += 12;
        }
        offset += 2; // Attribute byte count is intentionally ignored.
        builder.add(normal, positions);
    }
    return builder.finish();
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

SurfaceMesh load_stl(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        throw std::runtime_error("cannot open STL file: " + path);
    }
    const std::streamoff end = input.tellg();
    if (end < 0)
    {
        throw std::runtime_error("cannot determine STL file size: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty())
    {
        throw std::runtime_error("cannot read STL file: " + path);
    }

    if (bytes.size() >= 84)
    {
        const std::uint32_t count = u32_at(bytes, 80);
        const std::uint64_t expected = 84ull + 50ull * count;
        if (expected == bytes.size())
        {
            return load_binary_stl(path, bytes, count);
        }
    }
    return load_ascii_stl(path);
}

SurfaceMesh load_surface_mesh(const std::string& path)
{
    const std::string extension = lower(
        std::filesystem::path(path).extension().string());
    if (extension == ".obj") return load_obj(path);
    if (extension == ".nsm") return load_nsm_v1(path);
    if (extension == ".stl") return load_stl(path);
    throw std::invalid_argument(
        "unsupported mesh extension (expected .obj, .nsm, or .stl): " + path);
}

std::vector<CreaseEdge> load_eng_v1(
    const std::string& path,
    const SurfaceMesh& associated_mesh)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        throw std::runtime_error("cannot open ENG file: " + path);
    }
    const std::streamoff end = input.tellg();
    if (end < 16)
    {
        throw std::runtime_error("truncated ENG v1 header: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input || std::memcmp(bytes.data(), "ENG\0", 4) != 0)
    {
        throw std::runtime_error("invalid ENG v1 header: " + path);
    }
    const std::uint32_t version = u32_at(bytes, 4);
    const std::uint32_t edge_count = u32_at(bytes, 8);
    const std::uint32_t reserved = u32_at(bytes, 12);
    if (version != 1)
    {
        throw std::runtime_error("unsupported ENG version: " + std::to_string(version));
    }
    if (reserved != 0 || edge_count > 100000000u ||
        16ull + 20ull * edge_count != bytes.size())
    {
        throw std::runtime_error("invalid ENG v1 payload size or reserved field");
    }

    std::set<std::array<std::uint32_t, 2>> mesh_edges;
    for (const Triangle& triangle : associated_mesh.triangles)
    {
        for (std::size_t edge = 0; edge < 3; ++edge)
        {
            std::array<std::uint32_t, 2> key{
                triangle.vertex[edge], triangle.vertex[(edge + 1) % 3]};
            if (key[0] > key[1]) std::swap(key[0], key[1]);
            mesh_edges.insert(key);
        }
    }

    std::vector<CreaseEdge> result;
    result.reserve(edge_count);
    std::set<std::array<std::uint32_t, 2>> seen;
    std::size_t offset = 16;
    for (std::uint32_t index = 0; index < edge_count; ++index)
    {
        CreaseEdge crease;
        crease.vertex = {u32_at(bytes, offset), u32_at(bytes, offset + 4)};
        crease.c_sharp = {
            static_cast<double>(f32_at(bytes, offset + 8)),
            static_cast<double>(f32_at(bytes, offset + 12)),
            static_cast<double>(f32_at(bytes, offset + 16))};
        offset += 20;
        if (crease.vertex[0] >= associated_mesh.vertices.size() ||
            crease.vertex[1] >= associated_mesh.vertices.size() ||
            crease.vertex[0] >= crease.vertex[1])
        {
            throw std::runtime_error("ENG edge has invalid or unsorted vertex indices");
        }
        if (!std::isfinite(crease.c_sharp.x) ||
            !std::isfinite(crease.c_sharp.y) ||
            !std::isfinite(crease.c_sharp.z))
        {
            throw std::runtime_error("ENG contains a non-finite c_sharp vector");
        }
        if (mesh_edges.count(crease.vertex) == 0)
        {
            throw std::runtime_error("ENG edge does not exist in the associated mesh");
        }
        if (!seen.insert(crease.vertex).second)
        {
            throw std::runtime_error("ENG contains a duplicate crease edge");
        }
        result.push_back(crease);
    }
    return result;
}

} // namespace nexsdf
