#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{

void usage()
{
    std::cerr << "usage: nexsdfgen INPUT.(obj|nsm) OUTPUT.nsdf [options]\n"
              << "  --representation grid|exact-octree|adaptive-octree\n"
              << "  --reconstruction trilinear|tricubic|gradient|exact\n"
              << "  --resolution N  --max-depth N  --start-depth N\n"
              << "  --max-triangles N  --tolerance X  --padding X\n"
              << "  --absolute-padding X  --derivative-step X\n";
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

nexsdf::Representation representation(const std::string& value)
{
    if (value == "grid") return nexsdf::Representation::DenseGrid;
    if (value == "exact-octree") return nexsdf::Representation::ExactInfluenceOctree;
    if (value == "adaptive-octree") return nexsdf::Representation::AdaptivePiecewiseOctree;
    throw std::invalid_argument("unknown representation: " + value);
}

nexsdf::Reconstruction reconstruction(const std::string& value)
{
    if (value == "trilinear") return nexsdf::Reconstruction::Trilinear;
    if (value == "tricubic") return nexsdf::Reconstruction::TricubicHermite;
    if (value == "gradient") return nexsdf::Reconstruction::GradientTaylor;
    if (value == "exact") return nexsdf::Reconstruction::Exact;
    throw std::invalid_argument("unknown reconstruction: " + value);
}

std::string next(int& index, int count, char** values, const char* option)
{
    if (++index >= count) throw std::invalid_argument(std::string("missing value for ") + option);
    return values[index];
}

std::uint32_t u32(const std::string& value, const char* option)
{
    const unsigned long long parsed = std::stoull(value);
    if (parsed > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument(std::string("value is too large for ") + option);
    return static_cast<std::uint32_t>(parsed);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--help")
    {
        usage();
        return 0;
    }
    if (argc < 3)
    {
        usage();
        return 2;
    }
    try
    {
        const bool trace = std::getenv("NEXSDF_TRACE") != nullptr;
        const auto stage = [trace](const char* name) {
            if (trace) std::cerr << "[nexsdfgen] " << name << '\n';
        };
        const std::string input_path = argv[1];
        const std::string output_path = argv[2];
        nexsdf::BuildOptions options;
        for (int i = 3; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--representation") options.representation = representation(next(i, argc, argv, option.c_str()));
            else if (option == "--reconstruction") options.reconstruction = reconstruction(next(i, argc, argv, option.c_str()));
            else if (option == "--resolution")
            {
                const auto value = u32(next(i, argc, argv, option.c_str()), option.c_str());
                options.resolution = {value, value, value};
            }
            else if (option == "--max-depth") options.maximum_depth = u32(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--start-depth") options.start_depth = u32(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--max-triangles") options.maximum_triangles_per_leaf = u32(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--tolerance") options.error_tolerance = std::stod(next(i, argc, argv, option.c_str()));
            else if (option == "--padding") options.relative_padding = std::stod(next(i, argc, argv, option.c_str()));
            else if (option == "--absolute-padding") options.absolute_padding = std::stod(next(i, argc, argv, option.c_str()));
            else if (option == "--derivative-step") options.derivative_step = std::stod(next(i, argc, argv, option.c_str()));
            else if (option == "--help") { usage(); return 0; }
            else throw std::invalid_argument("unknown option: " + option);
        }

        const std::string extension = lower(std::filesystem::path(input_path).extension().string());
        nexsdf::SurfaceMesh mesh;
        stage("load mesh");
        if (extension == ".obj") mesh = nexsdf::load_obj(input_path);
        else if (extension == ".nsm") mesh = nexsdf::load_nsm_v1(input_path);
        else throw std::invalid_argument("input extension must be .obj or .nsm");

        stage("validate mesh");
        stage("build asset");
        nexsdf::Asset asset = nexsdf::build(mesh, options);
        stage("save asset");
        asset.save(output_path);
        stage("complete");
        const auto& info = asset.info();
        std::cout << "wrote " << output_path << "\n"
                  << "triangles=" << info.triangle_count
                  << " nodes=" << info.node_count
                  << " coefficients=" << info.coefficient_count
                  << " measured_max_error=" << info.measured_maximum_error << "\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nexsdfgen: " << error.what() << '\n';
        return 1;
    }
}
