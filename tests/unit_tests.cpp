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

std::filesystem::path temporary_asset_path(const char* name);

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

void append_cube(
    nexsdf::SurfaceMesh& destination,
    nexsdf::Vec3 center,
    double half_extent,
    bool inward = false)
{
    nexsdf::SurfaceMesh source = cube_mesh(inward);
    const std::uint32_t vertex_offset =
        static_cast<std::uint32_t>(destination.vertices.size());
    const std::uint32_t face_offset =
        static_cast<std::uint32_t>(destination.triangles.size());
    for (nexsdf::Vec3 vertex : source.vertices)
        destination.vertices.push_back(center + half_extent * vertex);
    for (nexsdf::Triangle triangle : source.triangles)
    {
        for (std::uint32_t& vertex : triangle.vertex) vertex += vertex_offset;
        triangle.face_id += face_offset;
        destination.triangles.push_back(triangle);
    }
}

void test_composition_policies()
{
    nexsdf::SurfaceMesh disjoint;
    append_cube(disjoint, {-3.0, 0.0, 0.0}, 1.0);
    append_cube(disjoint, {3.0, 0.0, 0.0}, 1.0, true);
    bool separate_rejected = false;
    try
    {
        const nexsdf::ExactSurface ignored(disjoint);
        (void)ignored;
    }
    catch (const std::invalid_argument&)
    {
        separate_rejected = true;
    }
    check(separate_rejected,
        "separate-assets policy rejects an ambiguous multi-component asset");

    const nexsdf::ExactSurface disjoint_union(
        disjoint, nexsdf::CompositionPolicy::SolidUnion);
    near(disjoint_union.query({-3.0, 0.0, 0.0}).phi, -1.0, 1.0e-12,
        "solid union is negative inside its first component");
    near(disjoint_union.query({3.0, 0.0, 0.0}).phi, -1.0, 1.0e-12,
        "solid union is negative inside its second component");
    near(disjoint_union.query({0.0, 0.0, 0.0}).phi, 2.0, 1.0e-12,
        "solid union is positive between disjoint components");
    check(disjoint_union.component_count() == 2 &&
          disjoint_union.active_component_count() == 2,
        "disjoint solid union keeps both exterior boundaries");

    nexsdf::SurfaceMesh nested;
    append_cube(nested, {0.0, 0.0, 0.0}, 2.0);
    append_cube(nested, {0.0, 0.0, 0.0}, 1.0, true);
    const nexsdf::ExactSurface filled(
        nested, nexsdf::CompositionPolicy::SolidUnion);
    near(filled.query({0.0, 0.0, 0.0}).phi, -2.0, 1.0e-12,
        "solid union removes an enclosed redundant boundary");
    check(filled.active_component_count() == 1 &&
          filled.active_triangles().size() == 12,
        "solid union retains only the outer nested shell");

    const nexsdf::ExactSurface cavity(
        nested, nexsdf::CompositionPolicy::NestedParity);
    near(cavity.query({0.0, 0.0, 0.0}).phi, 1.0, 1.0e-12,
        "nested parity makes the inner shell a positive cavity");
    near(cavity.query({1.5, 0.0, 0.0}).phi, -0.5, 1.0e-12,
        "nested parity is negative in shell material");
    near(cavity.query({2.5, 0.0, 0.0}).phi, 0.5, 1.0e-12,
        "nested parity is positive outside the outer shell");
    const nexsdf::QueryResult inner_side = cavity.query({0.9, 0.2, 0.1});
    check(inner_side.phi > 0.0 && inner_side.raw_gradient.x < 0.0,
        "cavity boundary gradient points from material into positive cavity");

    nexsdf::SurfaceMesh intersecting;
    append_cube(intersecting, {0.0, 0.0, 0.0}, 1.0);
    append_cube(intersecting, {1.0, 0.0, 0.0}, 1.0);
    bool intersection_rejected = false;
    try
    {
        const nexsdf::ExactSurface ignored(
            intersecting, nexsdf::CompositionPolicy::SolidUnion);
        (void)ignored;
    }
    catch (const std::invalid_argument& error)
    {
        intersection_rejected =
            std::string(error.what()).find("intersect or touch") != std::string::npos;
    }
    check(intersection_rejected,
        "intersecting or touching shell components fail closed");

    nexsdf::BuildOptions options;
    options.representation = nexsdf::Representation::ExactInfluenceOctree;
    options.reconstruction = nexsdf::Reconstruction::Exact;
    options.composition = nexsdf::CompositionPolicy::NestedParity;
    options.maximum_depth = 3;
    options.start_depth = 1;
    options.maximum_triangles_per_leaf = 4;
    options.relative_padding = 0.25;
    const nexsdf::Asset source = nexsdf::build(nested, options);
    const std::filesystem::path path = temporary_asset_path(
        "nexsdf-composition-roundtrip.nsdf");
    source.save(path.string());
    const nexsdf::Asset loaded = nexsdf::Asset::load(path.string());
    check(loaded.info().composition == nexsdf::CompositionPolicy::NestedParity &&
          loaded.info().component_count == 2 &&
          loaded.info().active_component_count == 2,
        "NSDF round trip preserves composition metadata");
    nexsdf_asset* handle = nullptr;
    check(nexsdf_asset_open(path.string().c_str(), &handle) == NEXSDF_STATUS_OK,
        "C API opens a composed NSDF asset");
    nexsdf_asset_provenance provenance{};
    provenance.struct_size = sizeof(provenance);
    check(nexsdf_asset_get_provenance(handle, &provenance) == NEXSDF_STATUS_OK &&
          provenance.composition == 2 && provenance.component_count == 2 &&
          provenance.active_component_count == 2,
        "C API exposes composition and component metadata");
    nexsdf_asset_close(handle);
    for (const nexsdf::Vec3 point : {
             nexsdf::Vec3{0.0, 0.0, 0.0},
             nexsdf::Vec3{1.5, 0.2, 0.1},
             nexsdf::Vec3{2.2, 0.1, 0.2}})
    {
        const nexsdf::QueryResult before = source.query(point);
        const nexsdf::QueryResult after = loaded.query(point);
        check(before.phi == after.phi &&
              before.branch_signature == after.branch_signature &&
              nexsdf::norm(before.raw_gradient - after.raw_gradient) == 0.0,
            "composed exact NSDF query survives round trip bit-for-bit");
    }
    std::error_code error;
    std::filesystem::remove(path, error);
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

void test_parallel_and_batch_backends()
{
    nexsdf::BuildOptions scalar = grid_options(nexsdf::Reconstruction::Trilinear);
    scalar.resolution = {16, 16, 16};
    const nexsdf::Asset scalar_asset = nexsdf::build(cube_mesh(), scalar);
    nexsdf::BuildOptions parallel = scalar;
    parallel.backend = nexsdf::ComputeBackend::CpuParallel;
    parallel.worker_threads = 4;
    const nexsdf::Asset parallel_asset = nexsdf::build(cube_mesh(), parallel);
    const nexsdf::Asset parallel_repeat = nexsdf::build(cube_mesh(), parallel);
    check(parallel_asset.info().build_backend == nexsdf::ComputeBackend::CpuParallel &&
          parallel_asset.info().worker_threads == 4,
        "parallel build provenance records its deterministic worker count");

    const std::filesystem::path scalar_path = temporary_asset_path("nexsdf-scalar.nsdf");
    const std::filesystem::path parallel_path = temporary_asset_path("nexsdf-parallel.nsdf");
    const std::filesystem::path parallel_repeat_path =
        temporary_asset_path("nexsdf-parallel-repeat.nsdf");
    scalar_asset.save(scalar_path.string());
    parallel_asset.save(parallel_path.string());
    parallel_repeat.save(parallel_repeat_path.string());
    const nexsdf::Asset loaded = nexsdf::Asset::load(parallel_path.string());
    check(loaded.info().format_minor == 3 &&
          loaded.info().build_backend == nexsdf::ComputeBackend::CpuParallel &&
          loaded.info().worker_threads == 4,
        "NSDF 1.3 round trip preserves build backend provenance");
    nexsdf_asset* handle = nullptr;
    check(nexsdf_asset_open(parallel_path.string().c_str(), &handle) ==
              NEXSDF_STATUS_OK,
        "C API opens an NSDF 1.3 parallel asset");
    nexsdf_asset_provenance backend_provenance{};
    backend_provenance.struct_size = sizeof(backend_provenance);
    check(nexsdf_asset_get_provenance(handle, &backend_provenance) ==
              NEXSDF_STATUS_OK &&
          backend_provenance.build_backend == 1 &&
          backend_provenance.worker_threads == 4,
        "C API exposes NSDF 1.3 backend provenance");
    struct LegacyProvenance
    {
        std::uint32_t struct_size;
        std::uint32_t influence_filter;
        std::uint64_t candidate_index_count;
        std::uint32_t composition;
        std::uint32_t component_count;
        std::uint32_t active_component_count;
    } legacy{};
    legacy.struct_size = sizeof(legacy);
    check(nexsdf_asset_get_provenance(handle,
              reinterpret_cast<nexsdf_asset_provenance*>(&legacy)) ==
              NEXSDF_STATUS_OK && legacy.component_count == 1,
        "size-versioned C provenance remains compatible with the 1.2 prefix");
    nexsdf_asset_close(handle);

    std::ifstream scalar_input(scalar_path, std::ios::binary);
    std::ifstream parallel_input(parallel_path, std::ios::binary);
    std::ifstream parallel_repeat_input(parallel_repeat_path, std::ios::binary);
    std::vector<char> scalar_bytes{
        std::istreambuf_iterator<char>(scalar_input), std::istreambuf_iterator<char>()};
    std::vector<char> parallel_bytes{
        std::istreambuf_iterator<char>(parallel_input), std::istreambuf_iterator<char>()};
    std::vector<char> parallel_repeat_bytes{
        std::istreambuf_iterator<char>(parallel_repeat_input),
        std::istreambuf_iterator<char>()};
    check(scalar_bytes.size() == parallel_bytes.size(),
        "scalar and parallel assets have identical serialized shape");
    check(parallel_bytes == parallel_repeat_bytes,
        "repeated parallel builds serialize byte-for-byte identically");

    std::vector<nexsdf::Vec3> points;
    for (int index = 0; index < 257; ++index)
    {
        points.push_back({
            -1.1 + 2.2 * ((index * 17) % 257) / 256.0,
            -1.1 + 2.2 * ((index * 43) % 257) / 256.0,
            -1.1 + 2.2 * ((index * 97) % 257) / 256.0});
    }
    std::vector<nexsdf::QueryResult> scalar_results(points.size());
    std::vector<nexsdf::QueryResult> parallel_results(points.size());
    std::vector<nexsdf::QueryResult> simd_results(points.size());
    std::vector<nexsdf::QueryResult> default_results(points.size());
    nexsdf::BatchQueryOptions scalar_query;
    scalar_query.backend = nexsdf::BatchBackend::Scalar;
    scalar_asset.query_batch(points.data(), points.size(), scalar_results.data(), scalar_query);
    scalar_asset.query_batch(points.data(), points.size(), default_results.data());
    nexsdf::BatchQueryOptions parallel_query = scalar_query;
    parallel_query.worker_threads = 4;
    parallel_asset.query_batch(
        points.data(), points.size(), parallel_results.data(), parallel_query);
    nexsdf::BatchQueryOptions simd_query;
    simd_query.backend = nexsdf::BatchBackend::AutoSimd;
    parallel_asset.query_batch(points.data(), points.size(), simd_results.data(), simd_query);
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        near(parallel_results[index].phi, scalar_results[index].phi, 0.0,
            "parallel dense build/query preserves scalar phi bit-for-bit");
        near(default_results[index].phi, scalar_results[index].phi, 0.0,
            "default batch query preserves the legacy scalar evaluation order");
        near(nexsdf::norm(parallel_results[index].raw_gradient -
            scalar_results[index].raw_gradient), 0.0, 0.0,
            "parallel dense build/query preserves scalar gradient bit-for-bit");
        near(simd_results[index].phi, scalar_results[index].phi, 2.0e-15,
            "SIMD dense trilinear phi is scalar-equivalent");
        near(nexsdf::norm(simd_results[index].raw_gradient -
            scalar_results[index].raw_gradient), 0.0, 2.0e-14,
            "SIMD dense trilinear gradient is scalar-equivalent");
    }

    nexsdf::BuildOptions exact_scalar;
    exact_scalar.representation = nexsdf::Representation::ExactInfluenceOctree;
    exact_scalar.reconstruction = nexsdf::Reconstruction::Exact;
    exact_scalar.influence_filter = nexsdf::InfluenceFilter::PaperGjk;
    exact_scalar.maximum_depth = 3;
    exact_scalar.start_depth = 1;
    exact_scalar.maximum_triangles_per_leaf = 2;
    nexsdf::BuildOptions exact_parallel = exact_scalar;
    exact_parallel.backend = nexsdf::ComputeBackend::CpuParallel;
    exact_parallel.worker_threads = 4;
    const nexsdf::Asset exact_reference = nexsdf::build(cube_mesh(), exact_scalar);
    const nexsdf::Asset exact_threaded = nexsdf::build(cube_mesh(), exact_parallel);
    check(exact_reference.info().node_count == exact_threaded.info().node_count &&
          exact_reference.info().candidate_index_count ==
              exact_threaded.info().candidate_index_count,
        "parallel exact-octree child filtering preserves deterministic topology");
    exact_reference.query_batch(
        points.data(), points.size(), scalar_results.data(), scalar_query);
    exact_threaded.query_batch(
        points.data(), points.size(), parallel_results.data(), parallel_query);
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        near(parallel_results[index].phi, scalar_results[index].phi, 0.0,
            "parallel exact octree preserves scalar phi bit-for-bit");
        check(parallel_results[index].face_id == scalar_results[index].face_id &&
              parallel_results[index].feature == scalar_results[index].feature &&
              parallel_results[index].branch_signature ==
                  scalar_results[index].branch_signature,
            "parallel exact octree preserves feature and branch identity");
    }
    std::error_code error;
    std::filesystem::remove(scalar_path, error);
    std::filesystem::remove(parallel_path, error);
    std::filesystem::remove(parallel_repeat_path, error);
}

void test_exact_surface_simd_batch()
{
    const nexsdf::ExactSurface exact(cube_mesh());
    std::vector<nexsdf::Vec3> points;
    for (int index = 0; index < 259; ++index)
        points.push_back({
            -1.4 + 2.8 * ((index * 19) % 259) / 258.0,
            -1.4 + 2.8 * ((index * 47) % 259) / 258.0,
            -1.4 + 2.8 * ((index * 101) % 259) / 258.0});
    std::vector<nexsdf::QueryResult> batch(points.size());
    exact.query_batch(points.data(), points.size(), batch.data());
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const nexsdf::QueryResult scalar = exact.query(points[index]);
        near(batch[index].phi, scalar.phi, 0.0,
            "SIMD-friendly exact BVH batch preserves phi bit-for-bit");
        check(batch[index].face_id == scalar.face_id &&
              batch[index].feature == scalar.feature &&
              batch[index].branch_signature == scalar.branch_signature &&
              nexsdf::norm(batch[index].witness - scalar.witness) == 0.0,
            "SIMD-friendly exact BVH batch preserves witness and feature identity at " +
                std::to_string(index) + " batch_face=" +
                std::to_string(batch[index].face_id) + " scalar_face=" +
                std::to_string(scalar.face_id));
    }
}

} // namespace

int main()
{
    try
    {
        test_mesh_and_exact_query();
        test_composition_policies();
        test_dense_reconstructions();
        test_dense_continuity();
        test_exact_octree();
        test_adaptive_octree();
        test_serialization_and_c_api();
        test_octree_serialization();
        test_invalid_option_pairs();
        test_parallel_and_batch_backends();
        test_exact_surface_simd_batch();
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
