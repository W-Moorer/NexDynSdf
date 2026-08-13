#include "nexsdf/c_api.h"
#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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

void near(double actual, double expected, double tolerance, const std::string& message)
{
    check(std::abs(actual - expected) <= tolerance,
        message + " (actual=" + std::to_string(actual) +
        ", expected=" + std::to_string(expected) + ")");
}

nexsdf::SurfaceMesh cube_mesh(bool inward = false)
{
    nexsdf::SurfaceMesh mesh;
    mesh.vertices = {
        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
        {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1}};
    const std::uint32_t indices[][3] = {
        {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7},
        {0,1,5}, {0,5,4}, {3,7,6}, {3,6,2},
        {0,4,7}, {0,7,3}, {1,2,6}, {1,6,5}};
    for (std::uint32_t face = 0; face < 12; ++face)
    {
        nexsdf::Triangle triangle;
        triangle.vertex = {indices[face][0], indices[face][1], indices[face][2]};
        if (inward) std::swap(triangle.vertex[1], triangle.vertex[2]);
        triangle.face_id = face;
        mesh.triangles.push_back(triangle);
    }
    return mesh;
}

void test_mesh_and_exact_query()
{
    const nexsdf::SurfaceMesh cube = cube_mesh();
    const nexsdf::MeshValidation validation = nexsdf::validate_mesh(cube);
    check(validation.valid_for_signed_distance(), "cube is a valid signed-distance mesh");

    nexsdf::SurfaceMesh open = cube;
    open.triangles.pop_back();
    const nexsdf::MeshValidation open_validation = nexsdf::validate_mesh(open);
    check(!open_validation.valid_for_signed_distance(), "open mesh is rejected");
    check(open_validation.boundary_edges != 0, "open mesh reports boundary edges");

    nexsdf::SurfaceMesh disconnected = cube;
    const std::uint32_t offset = static_cast<std::uint32_t>(disconnected.vertices.size());
    nexsdf::SurfaceMesh second = cube_mesh();
    for (nexsdf::Vec3& vertex : second.vertices) vertex.x += 4.0;
    disconnected.vertices.insert(
        disconnected.vertices.end(), second.vertices.begin(), second.vertices.end());
    for (nexsdf::Triangle triangle : second.triangles)
    {
        for (std::uint32_t& vertex : triangle.vertex) vertex += offset;
        disconnected.triangles.push_back(triangle);
    }
    const nexsdf::MeshValidation disconnected_validation =
        nexsdf::validate_mesh(disconnected);
    check(disconnected_validation.connected_components == 2 &&
          !disconnected_validation.valid_for_signed_distance(),
        "multiple surface components are rejected instead of using ambiguous local sign");

    const nexsdf::ExactSurface surface(cube);
    const nexsdf::QueryResult outside = surface.query({2.0, 0.2, 0.1});
    near(outside.phi, 1.0, 1.0e-12, "exact outside distance");
    near(outside.raw_gradient.x, 1.0, 1.0e-12, "exact outside gradient");
    check(outside.exact && outside.has_witness, "exact query exposes witness");

    const nexsdf::QueryResult inside = surface.query({0.0, 0.2, 0.1});
    near(inside.phi, -0.8, 1.0e-12, "exact inside distance");
    near(nexsdf::norm(inside.raw_gradient), 1.0, 1.0e-12, "exact gradient is unit length");

    const nexsdf::ExactSurface reversed(cube_mesh(true));
    near(reversed.query({0.0, 0.0, 0.0}).phi, -1.0, 1.0e-12,
        "globally reversed winding is canonicalized outward");

    const nexsdf::QueryResult edge = surface.query({2.0, 2.0, 0.0});
    check(edge.feature == nexsdf::Feature::Edge, "edge Voronoi feature is reported");
    near(edge.phi, std::sqrt(2.0), 1.0e-12, "edge distance");
    const nexsdf::QueryResult vertex = surface.query({2.0, 2.0, 2.0});
    check(vertex.feature == nexsdf::Feature::Vertex, "vertex Voronoi feature is reported");
    near(vertex.phi, std::sqrt(3.0), 1.0e-12, "vertex distance");
}

nexsdf::BuildOptions grid_options(nexsdf::Reconstruction reconstruction)
{
    nexsdf::BuildOptions options;
    options.representation = nexsdf::Representation::DenseGrid;
    options.reconstruction = reconstruction;
    options.resolution = {6, 6, 6};
    options.relative_padding = 0.25;
    return options;
}

void test_dense_reconstructions()
{
    const nexsdf::ExactSurface exact(cube_mesh());
    const std::vector<nexsdf::Vec3> points{
        {1.2, 0.17, -0.11}, {-1.1, 0.23, 0.31}, {0.1, 0.2, 0.3}};
    for (const nexsdf::Reconstruction reconstruction : {
             nexsdf::Reconstruction::Trilinear,
             nexsdf::Reconstruction::TricubicHermite,
             nexsdf::Reconstruction::GradientTaylor})
    {
        const nexsdf::Asset asset = nexsdf::build(cube_mesh(), grid_options(reconstruction));
        check(asset.info().coefficient_count != 0, "dense reconstruction stores coefficients");
        for (const nexsdf::Vec3 point : points)
        {
            const nexsdf::QueryResult result = asset.query(point);
            const nexsdf::QueryResult reference = exact.query(point);
            check(result.valid && result.in_domain && !result.exact,
                "dense query returns valid approximate result");
            check(!result.has_measured_error,
                "dense query does not claim an unmeasured error value");
            check(std::abs(result.phi - reference.phi) < 0.36,
                "dense reconstruction " +
                std::to_string(static_cast<std::uint32_t>(reconstruction)) +
                " stays within coarse-grid error envelope at (" +
                std::to_string(point.x) + "," + std::to_string(point.y) + "," +
                std::to_string(point.z) + "), error=" +
                std::to_string(std::abs(result.phi - reference.phi)));
            near(nexsdf::norm(result.unit_normal), 1.0, 1.0e-10,
                "dense reconstruction returns separate unit normal");
        }
    }
}

void test_dense_continuity()
{
    for (const nexsdf::Reconstruction reconstruction : {
             nexsdf::Reconstruction::Trilinear,
             nexsdf::Reconstruction::TricubicHermite})
    {
        nexsdf::BuildOptions options = grid_options(reconstruction);
        options.resolution = {4, 4, 4};
        const nexsdf::Asset asset = nexsdf::build(cube_mesh(), options);
        const double interface_x = asset.info().domain.minimum.x +
            2.0 * asset.info().domain.extent().x / options.resolution[0];
        const double epsilon = 1.0e-9;
        const nexsdf::QueryResult left = asset.query({interface_x - epsilon, 0.17, -0.21});
        const nexsdf::QueryResult right = asset.query({interface_x + epsilon, 0.17, -0.21});
        check(std::abs(left.phi - right.phi) < 1.0e-7,
            "dense scalar field is C0 across cell faces");
        if (reconstruction == nexsdf::Reconstruction::TricubicHermite)
        {
            check(nexsdf::norm(left.raw_gradient - right.raw_gradient) < 1.0e-5,
                "tricubic Hermite gradient is C1 across cell faces, jump=" +
                std::to_string(nexsdf::norm(left.raw_gradient - right.raw_gradient)) +
                ", left=(" + std::to_string(left.raw_gradient.x) + "," +
                std::to_string(left.raw_gradient.y) + "," +
                std::to_string(left.raw_gradient.z) + "), right=(" +
                std::to_string(right.raw_gradient.x) + "," +
                std::to_string(right.raw_gradient.y) + "," +
                std::to_string(right.raw_gradient.z) + ")");
        }
    }
}

void test_exact_octree()
{
    const nexsdf::SurfaceMesh cube = cube_mesh();
    const nexsdf::ExactSurface brute(cube);
    std::vector<std::uint32_t> all_triangles(cube.triangles.size());
    for (std::size_t i = 0; i < all_triangles.size(); ++i)
        all_triangles[i] = static_cast<std::uint32_t>(i);
    for (const nexsdf::InfluenceFilter filter : {
             nexsdf::InfluenceFilter::AabbLipschitz,
             nexsdf::InfluenceFilter::PaperGjk,
             nexsdf::InfluenceFilter::PaperFrankWolfe})
    {
        nexsdf::BuildOptions options;
        options.representation = nexsdf::Representation::ExactInfluenceOctree;
        options.reconstruction = nexsdf::Reconstruction::Exact;
        options.influence_filter = filter;
        options.maximum_depth = 4;
        options.start_depth = 1;
        options.maximum_triangles_per_leaf = 2;
        options.relative_padding = 0.25;
        const nexsdf::Asset tree = nexsdf::build(cube, options);
        check(tree.info().node_count > 1, "exact influence tree subdivides");
        check(tree.info().influence_filter == filter &&
              tree.info().candidate_index_count != 0,
            "exact influence tree records filter provenance");
        for (int z = -4; z <= 4; ++z)
        for (int y = -4; y <= 4; ++y)
        for (int x = -4; x <= 4; ++x)
        {
            const nexsdf::Vec3 point{0.3 * x, 0.3 * y, 0.3 * z};
            const nexsdf::QueryResult accelerated = tree.query(point);
            const nexsdf::QueryResult reference = brute.query(point);
            const nexsdf::QueryResult exhaustive = brute.query_subset(
                point, all_triangles.data(), all_triangles.size());
            near(reference.phi, exhaustive.phi, 1.0e-12,
                "exact-surface BVH agrees with exhaustive triangle scan");
            near(accelerated.phi, reference.phi, 1.0e-12,
                "exact influence filter agrees with brute-force triangle distance");
            const bool unique_reference =
                std::abs(std::abs(point.x) - std::abs(point.y)) > 1.0e-12 &&
                std::abs(std::abs(point.x) - std::abs(point.z)) > 1.0e-12 &&
                std::abs(std::abs(point.y) - std::abs(point.z)) > 1.0e-12;
            if (unique_reference)
            {
                check(accelerated.face_id == reference.face_id &&
                      accelerated.feature == reference.feature &&
                      nexsdf::norm(accelerated.witness - reference.witness) <= 1.0e-12,
                    "exact influence filter preserves unique nearest feature and witness");
            }
        }
    }
}

void test_adaptive_octree()
{
    for (const nexsdf::Reconstruction reconstruction : {
             nexsdf::Reconstruction::Trilinear,
             nexsdf::Reconstruction::TricubicHermite})
    {
        nexsdf::BuildOptions options;
        options.representation = nexsdf::Representation::AdaptivePiecewiseOctree;
        options.reconstruction = reconstruction;
        options.start_depth = 1;
        options.maximum_depth = 3;
        options.error_tolerance = 0.06;
        options.relative_padding = 0.25;
        const nexsdf::Asset asset = nexsdf::build(cube_mesh(), options);
        check(asset.info().node_count >= 9, "adaptive octree honors start depth");
        check(std::isfinite(asset.info().measured_maximum_error),
            "adaptive octree reports finite measured error");
        for (const nexsdf::Vec3 point : {
                 nexsdf::Vec3{1.15, 0.13, 0.27},
                 nexsdf::Vec3{0.11, -1.17, 0.19},
                 nexsdf::Vec3{0.2, 0.3, 0.4}})
        {
            const nexsdf::QueryResult result = asset.query(point);
            check(result.valid, "adaptive query is valid");
            check(result.measured_leaf_error >= 0.0,
                "adaptive query exposes measured leaf error");
            check(result.has_measured_error,
                "adaptive query marks measured error as available");
        }

        bool checked_interface = false;
        const nexsdf::Aabb domain = asset.info().domain;
        const nexsdf::Vec3 extent = domain.extent();
        const double epsilon = 1.0e-8 * std::max({extent.x, extent.y, extent.z});
        for (std::size_t axis = 0; axis < 3 && !checked_interface; ++axis)
        for (int a = 1; a < 80 && !checked_interface; ++a)
        for (int b = 1; b < 20 && !checked_interface; ++b)
        for (int c = 1; c < 20 && !checked_interface; ++c)
        {
            nexsdf::Vec3 point{
                domain.minimum.x + extent.x * b / 20.0,
                domain.minimum.y + extent.y * c / 20.0,
                domain.minimum.z + extent.z * 0.37};
            point[axis] = domain.minimum[axis] + extent[axis] * a / 80.0;
            nexsdf::Vec3 left_point = point;
            nexsdf::Vec3 right_point = point;
            left_point[axis] -= epsilon;
            right_point[axis] += epsilon;
            const nexsdf::QueryResult left = asset.query(left_point);
            const nexsdf::QueryResult right = asset.query(right_point);
            if (left.branch_signature == right.branch_signature ||
                left.cell_depth == right.cell_depth) continue;
            checked_interface = true;
            check(std::abs(left.phi - right.phi) < 2.0e-5,
                "adaptive scalar reconstruction is continuous across leaf interfaces: reconstruction=" +
                std::to_string(static_cast<std::uint32_t>(reconstruction)) +
                ", axis=" + std::to_string(axis) + ", point=(" +
                std::to_string(point.x) + "," + std::to_string(point.y) + "," +
                std::to_string(point.z) + "), depths=" +
                std::to_string(left.cell_depth) + "/" +
                std::to_string(right.cell_depth) + ", values=" +
                std::to_string(left.phi) + "/" + std::to_string(right.phi));
            if (reconstruction == nexsdf::Reconstruction::TricubicHermite)
            {
                check(nexsdf::norm(left.raw_gradient - right.raw_gradient) < 2.0e-3,
                    "adaptive tricubic gradient is continuous across leaf interfaces: jump=" +
                    std::to_string(nexsdf::norm(left.raw_gradient - right.raw_gradient)));
            }
        }
        check(checked_interface, "adaptive test locates a coarse/fine leaf interface");
    }
}

std::filesystem::path temporary_asset_path(const char* name)
{
    static std::atomic<std::uint64_t> counter{0};
    const std::filesystem::path requested(name);
#ifdef _WIN32
    const auto process_id = static_cast<unsigned long long>(_getpid());
#else
    const auto process_id = static_cast<unsigned long long>(getpid());
#endif
    const std::string unique = requested.stem().string() + "-" +
        std::to_string(process_id) + "-" +
        std::to_string(counter.fetch_add(1)) + requested.extension().string();
    return std::filesystem::temp_directory_path() / unique;
}

void test_serialization_and_c_api()
{
    const std::filesystem::path path = temporary_asset_path("nexsdf-unit.nsdf");
    const std::filesystem::path corrupt = temporary_asset_path("nexsdf-unit-corrupt.nsdf");
    const nexsdf::Asset source = nexsdf::build(
        cube_mesh(), grid_options(nexsdf::Reconstruction::TricubicHermite));
    source.save(path.string());
    const nexsdf::Asset loaded = nexsdf::Asset::load(path.string());
    check(loaded.info().node_count == source.info().node_count,
        "NSDF round trip preserves logical grid cell count");
    for (const nexsdf::Vec3 point : {
             nexsdf::Vec3{1.2, 0.1, 0.2}, nexsdf::Vec3{0.1, 0.2, 0.3}})
    {
        near(loaded.query(point).phi, source.query(point).phi, 0.0,
            "NSDF round trip preserves query values bit-for-bit");
    }

    nexsdf_asset* handle = nullptr;
    const nexsdf_status open_status = nexsdf_asset_open(path.string().c_str(), &handle);
    check(open_status == NEXSDF_STATUS_OK && handle != nullptr, "C API opens NSDF asset");
    nexsdf_asset_info info{};
    const nexsdf_status info_status = nexsdf_asset_get_info(handle, &info);
    check(info_status == NEXSDF_STATUS_OK && info.coefficient_count != 0,
        "C API returns asset metadata");
    check(info.has_measured_error == 0,
        "dense C API metadata distinguishes unmeasured error from zero error");
    nexsdf_asset_provenance provenance{};
    provenance.struct_size = sizeof(provenance);
    check(nexsdf_asset_get_provenance(handle, &provenance) == NEXSDF_STATUS_OK &&
          provenance.influence_filter == 0,
        "C API exposes size-versioned asset provenance without growing legacy info");
    const double points[2][4] = {{1.2, 0.1, 0.2, 99.0}, {0.1, 0.2, 0.3, 99.0}};
    nexsdf_query_result results[2]{};
    const nexsdf_status batch_status = nexsdf_query_batch(
        handle, 2, &points[0][0], sizeof(points[0]), results);
    check(batch_status == NEXSDF_STATUS_OK, "C API supports strided batch queries");
    check((results[0].flags & NEXSDF_QUERY_VALID) != 0,
        "C API sets query flags");
    const double outside[3] = {100.0, 100.0, 100.0};
    nexsdf_query_result outside_result{};
    const nexsdf_status outside_status = nexsdf_query(handle, outside, &outside_result);
    check(outside_status == NEXSDF_STATUS_OUT_OF_DOMAIN,
        "C API returns explicit out-of-domain status");
    check(std::string(nexsdf_last_error()).find("outside") != std::string::npos,
        "C API records thread-local error details");
    nexsdf_asset_close(handle);

    nexsdf_asset* missing_handle = nullptr;
    const nexsdf_status missing_status = nexsdf_asset_open(
        temporary_asset_path("nexsdf-does-not-exist.nsdf").string().c_str(),
        &missing_handle);
    check(missing_status == NEXSDF_STATUS_IO_ERROR && missing_handle == nullptr,
        "C API distinguishes missing-file I/O errors");

    std::ifstream input(path, std::ios::binary);
    std::vector<char> bytes(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    check(bytes.size() > 32, "serialized test asset has content");
    if (bytes.size() > 32)
    {
        bytes[24] ^= 0x01;
        std::ofstream output(corrupt, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        nexsdf_asset* corrupt_handle = nullptr;
        const nexsdf_status corrupt_status = nexsdf_asset_open(
            corrupt.string().c_str(), &corrupt_handle);
        check(corrupt_status == NEXSDF_STATUS_CORRUPT_ASSET && corrupt_handle == nullptr,
            "C API distinguishes corrupt NSDF assets");
        bool rejected = false;
        try
        {
            const nexsdf::Asset ignored = nexsdf::Asset::load(corrupt.string());
            (void)ignored;
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        check(rejected, "checksum rejects a modified NSDF asset");
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(corrupt, error);
}

void test_octree_serialization()
{
    const nexsdf::Vec3 point{1.17, 0.13, -0.19};
    for (const nexsdf::Representation representation : {
             nexsdf::Representation::ExactInfluenceOctree,
             nexsdf::Representation::AdaptivePiecewiseOctree})
    {
        nexsdf::BuildOptions options;
        options.representation = representation;
        options.reconstruction = representation == nexsdf::Representation::ExactInfluenceOctree
            ? nexsdf::Reconstruction::Exact
            : nexsdf::Reconstruction::TricubicHermite;
        options.maximum_depth = 3;
        options.start_depth = 1;
        options.maximum_triangles_per_leaf = 2;
        options.error_tolerance = 0.06;
        options.relative_padding = 0.25;
        if (representation == nexsdf::Representation::ExactInfluenceOctree)
            options.influence_filter = nexsdf::InfluenceFilter::PaperFrankWolfe;
        const nexsdf::Asset source = nexsdf::build(cube_mesh(), options);
        const std::filesystem::path path = temporary_asset_path(
            representation == nexsdf::Representation::ExactInfluenceOctree
                ? "nexsdf-exact-roundtrip.nsdf"
                : "nexsdf-adaptive-roundtrip.nsdf");
        source.save(path.string());
        const nexsdf::Asset loaded = nexsdf::Asset::load(path.string());
        const nexsdf::QueryResult before = source.query(point);
        const nexsdf::QueryResult after = loaded.query(point);
        near(after.phi, before.phi, 0.0, "octree NSDF preserves scalar query bit-for-bit");
        near(nexsdf::norm(after.raw_gradient - before.raw_gradient), 0.0, 0.0,
            "octree NSDF preserves gradient query bit-for-bit");
        check(after.exact == before.exact &&
              after.has_witness == before.has_witness &&
              after.has_measured_error == before.has_measured_error,
            "octree NSDF preserves query capability flags");
        check(loaded.info().triangle_count == source.info().triangle_count,
            "octree NSDF preserves source triangle metadata");
        check(loaded.info().influence_filter == source.info().influence_filter &&
              loaded.info().candidate_index_count == source.info().candidate_index_count,
            "octree NSDF preserves influence provenance");
        check(after.face_id == before.face_id && after.feature == before.feature &&
              after.branch_signature == before.branch_signature &&
              nexsdf::norm(after.witness - before.witness) == 0.0,
            "exact octree round trip preserves feature, witness, and branch bit-for-bit");
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

void test_invalid_option_pairs()
{
    nexsdf::BuildOptions invalid;
    invalid.representation = nexsdf::Representation::ExactInfluenceOctree;
    invalid.reconstruction = nexsdf::Reconstruction::Trilinear;
    bool rejected = false;
    try
    {
        const nexsdf::Asset ignored = nexsdf::build(cube_mesh(), invalid);
        (void)ignored;
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    check(rejected, "invalid representation/reconstruction pair is rejected");
}

} // namespace

int main()
{
    try
    {
        test_mesh_and_exact_query();
        test_dense_reconstructions();
        test_dense_continuity();
        test_exact_octree();
        test_adaptive_octree();
        test_serialization_and_c_api();
        test_octree_serialization();
        test_invalid_option_pairs();
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }
    if (failures != 0)
    {
        std::cerr << failures << " unit checks failed\n";
        return 1;
    }
    std::cout << "all unit checks passed\n";
    return 0;
}
