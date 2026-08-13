#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct Options
{
    std::filesystem::path root;
    std::optional<std::size_t> expected_files;
    std::optional<std::size_t> expected_ready;
};

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string clean(std::string value)
{
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

std::string next(int& index, int count, char** values, const char* option)
{
    if (++index >= count) throw std::invalid_argument(std::string("missing value for ") + option);
    return values[index];
}

std::size_t parse_size(const std::string& value, const char* option)
{
    const unsigned long long parsed = std::stoull(value);
    if (parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        throw std::invalid_argument(std::string("value is too large for ") + option);
    return static_cast<std::size_t>(parsed);
}

void usage()
{
    std::cerr << "usage: nexsdfmodelaudit MODEL_ROOT [--expect-files N] [--expect-ready N]\n";
}

Options parse_options(int argc, char** argv)
{
    if (argc < 2) throw std::invalid_argument("model root is required");
    Options options;
    options.root = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        const std::string option = argv[i];
        if (option == "--expect-files")
            options.expected_files = parse_size(next(i, argc, argv, option.c_str()), option.c_str());
        else if (option == "--expect-ready")
            options.expected_ready = parse_size(next(i, argc, argv, option.c_str()), option.c_str());
        else if (option == "--help")
        {
            usage();
            std::exit(0);
        }
        else
            throw std::invalid_argument("unknown option: " + option);
    }
    return options;
}

struct AuditRow
{
    std::filesystem::path path;
    std::string format;
    std::size_t vertices{0};
    std::size_t triangles{0};
    bool corner_normals{false};
    nexsdf::MeshValidation raw{};
    bool runtime_ready{false};
    std::string runtime_message;
};

AuditRow audit(const std::filesystem::path& root, const std::filesystem::path& path)
{
    AuditRow row;
    row.path = std::filesystem::relative(path, root);
    const std::string extension = lower(path.extension().string());
    row.format = extension == ".obj" ? "obj" : "nsm";
    nexsdf::SurfaceMesh mesh = extension == ".obj"
        ? nexsdf::load_obj(path.string())
        : nexsdf::load_nsm_v1(path.string());
    row.vertices = mesh.vertices.size();
    row.triangles = mesh.triangles.size();
    row.corner_normals = !mesh.triangles.empty()
        && std::all_of(mesh.triangles.begin(), mesh.triangles.end(),
            [](const nexsdf::Triangle& triangle) { return triangle.has_corner_normals; });
    row.raw = nexsdf::validate_mesh(mesh);
    try
    {
        const nexsdf::ExactSurface surface(std::move(mesh));
        row.runtime_ready = surface.validation().valid_for_signed_distance();
        row.runtime_message = surface.validation().message;
    }
    catch (const std::exception& error)
    {
        row.runtime_message = error.what();
    }
    return row;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--help")
    {
        usage();
        return 0;
    }
    try
    {
        const Options options = parse_options(argc, argv);
        if (!std::filesystem::is_directory(options.root))
            throw std::invalid_argument("model root is not a directory: " + options.root.string());

        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(options.root))
        {
            if (!entry.is_regular_file()) continue;
            const std::string extension = lower(entry.path().extension().string());
            if (extension == ".obj" || extension == ".nsm") paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());

        std::size_t parsed = 0;
        std::size_t ready = 0;
        std::size_t failures = 0;
        std::cout << "path\tformat\tvertices\ttriangles\tcorner_normals"
                     "\traw_ready\truntime_ready\tcomponents\tboundary_edges"
                     "\tnon_manifold_edges\torientation_mismatches\tmessage\n";
        for (const auto& path : paths)
        {
            try
            {
                const AuditRow row = audit(options.root, path);
                ++parsed;
                if (row.runtime_ready) ++ready;
                std::cout << row.path.generic_string() << '\t' << row.format << '\t'
                          << row.vertices << '\t' << row.triangles << '\t'
                          << (row.corner_normals ? 1 : 0) << '\t'
                          << (row.raw.valid_for_signed_distance() ? 1 : 0) << '\t'
                          << (row.runtime_ready ? 1 : 0) << '\t'
                          << row.raw.connected_components << '\t'
                          << row.raw.boundary_edges << '\t'
                          << row.raw.non_manifold_edges << '\t'
                          << row.raw.orientation_mismatches << '\t'
                          << clean(row.runtime_message) << '\n';
            }
            catch (const std::exception& error)
            {
                ++failures;
                std::cout << std::filesystem::relative(path, options.root).generic_string()
                          << "\tparse-error\t0\t0\t0\t0\t0\t0\t0\t0\t0\t"
                          << clean(error.what()) << '\n';
            }
        }
        std::cerr << "catalog_files=" << paths.size()
                  << " parsed=" << parsed
                  << " runtime_ready=" << ready
                  << " parse_failures=" << failures << '\n';
        if (failures != 0) return 1;
        if (options.expected_files && paths.size() != *options.expected_files)
        {
            std::cerr << "expected " << *options.expected_files << " catalog files\n";
            return 1;
        }
        if (options.expected_ready && ready != *options.expected_ready)
        {
            std::cerr << "expected " << *options.expected_ready << " runtime-ready files\n";
            return 1;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nexsdfmodelaudit: " << error.what() << '\n';
        return 1;
    }
}
