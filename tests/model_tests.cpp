#include "nexsdf/nexsdf.hpp"

#include <filesystem>
#include <fstream>
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

void check_stl(
    const std::string& relative,
    std::size_t expected_vertices,
    std::size_t expected_triangles)
{
    const nexsdf::SurfaceMesh mesh = nexsdf::load_stl(model(relative).string());
    check(mesh.vertices.size() == expected_vertices,
        relative + " deterministically welds duplicate coordinates");
    check(mesh.triangles.size() == expected_triangles,
        relative + " triangle count is preserved");
    check(nexsdf::validate_mesh(mesh).valid_for_signed_distance(),
        relative + " imports as a closed oriented manifold");
    const nexsdf::ExactSurface exact(mesh);
    check(exact.query({0.0, 0.0, 0.0}).phi <= 0.0,
        relative + " origin is inside/on-surface");
}

void check_cam_format_equivalence()
{
    const nexsdf::ExactSurface nsm(nexsdf::load_nsm_v1(
        model("sdfmodel/cam.nsm").string()));
    const nexsdf::ExactSurface stl(nexsdf::load_stl(
        model("sdfmodel/cam.stl").string()));
    double maximum = 0.0;
    for (int z = 0; z < 7; ++z)
    for (int y = 0; y < 11; ++y)
    for (int x = 0; x < 13; ++x)
    {
        const nexsdf::Vec3 point{
            -1.35 + 2.7 * x / 12.0,
            -1.35 + 2.7 * y / 10.0,
            -0.35 + 0.7 * z / 6.0};
        maximum = std::max(maximum,
            std::abs(nsm.query(point).phi - stl.query(point).phi));
    }
    check(maximum < 1.0e-7,
        "generated cam NSM and float32 STL agree at deterministic volume samples");
}

void check_corrupt_inputs_fail_closed()
{
    const std::filesystem::path corrupt_stl =
        std::filesystem::temp_directory_path() / "nexsdf-truncated.stl";
    const std::filesystem::path corrupt_eng =
        std::filesystem::temp_directory_path() / "nexsdf-truncated.eng";
    for (const auto& pair : {
             std::pair{model("sdfmodel/cam.stl"), corrupt_stl},
             std::pair{model("nagata/cone.eng"), corrupt_eng}})
    {
        std::ifstream input(pair.first, std::ios::binary);
        std::vector<char> bytes{
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        bytes.resize(bytes.size() / 2);
        std::ofstream output(pair.second, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    bool stl_rejected = false;
    try { (void)nexsdf::load_stl(corrupt_stl.string()); }
    catch (const std::exception&) { stl_rejected = true; }
    check(stl_rejected, "truncated binary STL fails closed");

    bool eng_rejected = false;
    try
    {
        const nexsdf::SurfaceMesh cone = nexsdf::load_nsm_v1(
            model("nagata/cone.nsm").string());
        (void)nexsdf::load_eng_v1(corrupt_eng.string(), cone);
    }
    catch (const std::exception&) { eng_rejected = true; }
    check(eng_rejected, "truncated ENG v1 fails closed");
    std::error_code error;
    std::filesystem::remove(corrupt_stl, error);
    std::filesystem::remove(corrupt_eng, error);
}

} // namespace

int main()
{
    try
    {
        check_obj("pycoco/obj_library/cube.obj", 684);
        check_obj("pycoco/obj_library/sphere.obj", 1520);
        check_obj("sdflib/Gear.obj", 6882);
        check_nsm("nagata/box.nsm", 128, false);
        check_nsm("nagata/sphere.nsm", 480, true);
        check_nsm("nagata/cone.nsm", 2432, true);
        check_nsm("sdfmodel/cam.nsm", 240, true);
        check_nsm("sdfmodel/gear.nsm", 96, true);
        check_stl("sdfmodel/cam.stl", 122, 240);
        check_cam_format_equivalence();
        check_corrupt_inputs_fail_closed();

        const nexsdf::SurfaceMesh ascii = nexsdf::load_stl(
            std::filesystem::path(NEXSDF_TEST_DATA_DIR).append("tetra_ascii.stl").string());
        check(ascii.vertices.size() == 4 && ascii.triangles.size() == 4,
            "ASCII STL importer welds the tetrahedron vertices");
        check(nexsdf::validate_mesh(ascii).valid_for_signed_distance(),
            "ASCII STL facet normals orient a valid tetrahedron");

        const nexsdf::SurfaceMesh cone = nexsdf::load_nsm_v1(
            model("nagata/cone.nsm").string());
        const std::vector<nexsdf::CreaseEdge> creases = nexsdf::load_eng_v1(
            model("nagata/cone.eng").string(), cone);
        check(creases.size() == 128,
            "ENG v1 imports all associated cone crease coefficients");

        const nexsdf::SurfaceMesh cube = nexsdf::load_obj(
            model("pycoco/obj_library/cube.obj").string());
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
