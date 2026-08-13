#include "nexsdf/nexsdf.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::filesystem::path model(const std::string& relative)
{
    return std::filesystem::path(NEXSDF_MODEL_DIR) / relative;
}

void check_obj(const std::string& relative, std::size_t expected_triangles)
{
    const nexsdf::SurfaceMesh mesh = nexsdf::load_obj(model(relative).string());
    check(mesh.triangles.size() == expected_triangles,
        relative + " triangle count is preserved");
    const nexsdf::ExactSurface exact(mesh);
    check(exact.validation().valid_for_signed_distance(),
        relative + " builds a signed-distance surface");
    const nexsdf::Vec3 center = (exact.bounds().minimum + exact.bounds().maximum) * 0.5;
    const nexsdf::QueryResult query = exact.query(center);
    check(query.valid && query.phi <= 0.0,
        relative + " center query is finite and inside/on-surface");
}

void check_nsm(
    const std::string& relative,
    std::size_t expected_triangles,
    bool indexed_validation_expected)
{
    const nexsdf::SurfaceMesh mesh = nexsdf::load_nsm_v1(model(relative).string());
    check(mesh.triangles.size() == expected_triangles,
        relative + " triangle count is preserved");
    check(!mesh.triangles.empty() && mesh.triangles.front().has_corner_normals,
        relative + " imports per-corner normals");
    check(nexsdf::validate_mesh(mesh).valid_for_signed_distance() == indexed_validation_expected,
        relative + " indexed topology expectation");
    const nexsdf::ExactSurface exact(mesh);
    check(exact.validation().valid_for_signed_distance(),
        relative + " builds after exact duplicate-coordinate welding");
    const nexsdf::Vec3 center = (exact.bounds().minimum + exact.bounds().maximum) * 0.5;
    check(exact.query(center).phi <= 0.0,
        relative + " center has non-positive signed distance");
}

} // namespace

int main()
{
    try
    {
        check_obj("pycoco/cube.obj", 684);
        check_obj("pycoco/sphere.obj", 1520);
        check_obj("sdflib/Gear.obj", 6882);
        check_nsm("nagata/box.nsm", 128, false);
        check_nsm("nagata/sphere.nsm", 480, true);
        check_nsm("nagata/cone.nsm", 2432, true);

        const nexsdf::SurfaceMesh cube = nexsdf::load_obj(model("pycoco/cube.obj").string());
        nexsdf::BuildOptions options;
        options.representation = nexsdf::Representation::ExactInfluenceOctree;
        options.reconstruction = nexsdf::Reconstruction::Exact;
        options.maximum_depth = 3;
        options.start_depth = 1;
        options.maximum_triangles_per_leaf = 32;
        const nexsdf::Asset asset = nexsdf::build(cube, options);
        check(asset.info().node_count > 1,
            "catalogued PyCoCo cube builds an exact influence octree");
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }
    if (failures != 0)
    {
        std::cerr << failures << " model checks failed\n";
        return 1;
    }
    std::cout << "all catalogued model checks passed\n";
    return 0;
}
